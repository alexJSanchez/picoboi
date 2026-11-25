// Host tests for launcher/launcher_model: selection, wrap-around, scrolling and labels.
#include <stdio.h>
#include <string.h>

#include "launcher_model.h"

static int failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                            \
    }                                                        \
  } while (0)

// Invariants that must hold after every move.
static void check_window(const lm_t *m, uint32_t rows, int line) {
  if (m->count == 0) {
    if (m->selected != 0 || m->top != 0) { printf("FAIL line %d: empty list state\n", line); failures++; }
    return;
  }
  if (m->selected >= m->count) { printf("FAIL line %d: selected out of range\n", line); failures++; }
  if (m->selected < m->top || m->selected >= m->top + rows) { printf("FAIL line %d: selection not visible (sel %u top %u)\n", line, (unsigned)m->selected, (unsigned)m->top); failures++; }
  if (m->count > rows && m->top + rows > m->count) { printf("FAIL line %d: window runs past the end\n", line); failures++; }
  if (m->count <= rows && m->top != 0) { printf("FAIL line %d: needless scrolling\n", line); failures++; }
}

int main(void) {
  lm_t m;

  // Empty list: moving does nothing and nothing breaks.
  lm_init(&m, 0);
  lm_move(&m, 1, 6);
  lm_move(&m, -3, 6);
  check_window(&m, 6, __LINE__);

  // One game: stays on it.
  lm_init(&m, 1);
  lm_move(&m, 1, 6);
  CHECK(m.selected == 0);
  lm_move(&m, -1, 6);
  CHECK(m.selected == 0);

  // Fewer games than rows: wraps, never scrolls.
  lm_init(&m, 3);
  lm_move(&m, -1, 6);
  CHECK(m.selected == 2 && m.top == 0);
  lm_move(&m, 1, 6);
  CHECK(m.selected == 0 && m.top == 0);

  // More games than rows: the window follows the selection down and up, wrapping at the ends.
  lm_init(&m, 10);
  for (int i = 0; i < 9; i++) {
    lm_move(&m, 1, 4);
    check_window(&m, 4, __LINE__);
  }
  CHECK(m.selected == 9 && m.top == 6);  // last four games visible
  lm_move(&m, 1, 4);                     // wraps to the first game
  CHECK(m.selected == 0 && m.top == 0);
  lm_move(&m, -1, 4);                    // wraps to the last game
  CHECK(m.selected == 9 && m.top == 6);
  for (int i = 0; i < 9; i++) {
    lm_move(&m, -1, 4);
    check_window(&m, 4, __LINE__);
  }
  CHECK(m.selected == 0 && m.top == 0);

  // Large jumps in either direction (paging) stay valid.
  lm_init(&m, 7);
  lm_move(&m, 4, 3);
  check_window(&m, 3, __LINE__);
  lm_move(&m, -100, 3);
  check_window(&m, 3, __LINE__);
  lm_move(&m, 1000000, 3);
  check_window(&m, 3, __LINE__);
  lm_move(&m, -2147483647, 3);
  check_window(&m, 3, __LINE__);

  // Every count and window size, random-ish walks: invariants always hold.
  for (uint32_t count = 1; count <= 12; count++)
    for (uint32_t rows = 1; rows <= 8; rows++) {
      lm_init(&m, count);
      int32_t step = 1;
      for (int i = 0; i < 60; i++) {
        lm_move(&m, (i % 5 == 4) ? -(step + 2) : step, rows);
        step = step % 3 + 1;
        check_window(&m, rows, __LINE__);
      }
    }

  // Reasons a game cannot start (priority order) and the RAM limit.
  pb_lib_entry_t e;
  memset(&e, 0, sizeof(e));
  CHECK(lm_unavailable_reason(&e, 32768) == NULL);
  e.ram_size = 32768;
  CHECK(lm_unavailable_reason(&e, 32768) == NULL);        // exactly fits
  e.ram_size = 32769;
  CHECK(lm_unavailable_reason(&e, 32768) != NULL);        // one byte too much
  e.ram_size = 0;
  e.flags = PB_LIB_FLAG_CGB_ENHANCED;                      // informational flag does not block
  CHECK(lm_unavailable_reason(&e, 32768) == NULL);
  e.flags = PB_LIB_FLAG_CGB_ONLY;
  CHECK(lm_unavailable_reason(&e, 32768) && strstr(lm_unavailable_reason(&e, 32768), "Color"));
  e.flags = PB_LIB_FLAG_UNSUPPORTED_MAPPER;
  CHECK(lm_unavailable_reason(&e, 32768) && strstr(lm_unavailable_reason(&e, 32768), "type"));
  e.flags = PB_LIB_FLAG_BAD_HEADER;
  CHECK(lm_unavailable_reason(&e, 32768) != NULL);
  e.flags = PB_LIB_FLAG_BAD_SIZE;
  CHECK(lm_unavailable_reason(&e, 32768) != NULL);
  e.flags = PB_LIB_FLAG_CGB_ONLY | PB_LIB_FLAG_UNSUPPORTED_MAPPER;  // colour is named first
  CHECK(strstr(lm_unavailable_reason(&e, 32768), "Color") != NULL);
  // Whatever pb_lib_entry_launchable says must agree with "no reason" (apart from the RAM limit).
  for (uint32_t f = 0; f < 0x20; f++) {
    e.flags = (uint16_t)f;
    e.ram_size = 0;
    CHECK((lm_unavailable_reason(&e, 32768) == NULL) == pb_lib_entry_launchable(&e));
  }

  // Size labels.
  char buf[8];
  lm_format_bytes(512, buf, sizeof(buf));        CHECK(strcmp(buf, "512B") == 0);
  lm_format_bytes(8192, buf, sizeof(buf));       CHECK(strcmp(buf, "8K") == 0);
  lm_format_bytes(32768, buf, sizeof(buf));      CHECK(strcmp(buf, "32K") == 0);
  lm_format_bytes(524288, buf, sizeof(buf));     CHECK(strcmp(buf, "512K") == 0);
  lm_format_bytes(1048576, buf, sizeof(buf));    CHECK(strcmp(buf, "1M") == 0);
  lm_format_bytes(8388608, buf, sizeof(buf));    CHECK(strcmp(buf, "8M") == 0);
  lm_format_bytes(40960, buf, sizeof(buf));      CHECK(strcmp(buf, "40K") == 0);
  lm_format_bytes(0, buf, sizeof(buf));          CHECK(strcmp(buf, "0B") == 0);
  lm_format_bytes(1500000, buf, sizeof(buf));    CHECK(strcmp(buf, "1500000B") != 0 || strlen(buf) < 8);  // truncated, never overflows
  CHECK(strlen(buf) < 8);

  if (failures == 0) printf("launcher model tests passed\n");
  return failures ? 1 : 0;
}
