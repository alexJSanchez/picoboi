#include "launcher_model.h"

#include <stdio.h>

void lm_init(lm_t *m, uint32_t count) {
  m->count = count;
  m->selected = 0;
  m->top = 0;
}

void lm_move(lm_t *m, int32_t delta, uint32_t rows) {
  if (m->count == 0 || rows == 0) {
    m->selected = 0;
    m->top = 0;
    return;
  }
  // Wrap with 64-bit arithmetic so any delta is safe.
  int64_t sel = ((int64_t)m->selected + delta) % (int64_t)m->count;
  if (sel < 0) sel += m->count;
  m->selected = (uint32_t)sel;

  // Keep the window [top, top + rows) around the selection and inside the list.
  if (m->selected < m->top) m->top = m->selected;
  if (m->selected >= m->top + rows) m->top = m->selected - rows + 1;
  if (m->count > rows) {
    if (m->top > m->count - rows) m->top = m->count - rows;
  } else {
    m->top = 0;
  }
}

const char *lm_unavailable_reason(const pb_lib_entry_t *e, uint32_t max_ram) {
  if (e->flags & PB_LIB_FLAG_CGB_ONLY) return "Game Boy Color only";
  if (e->flags & PB_LIB_FLAG_UNSUPPORTED_MAPPER) return "cartridge type not supported";
  if (e->flags & PB_LIB_FLAG_BAD_HEADER) return "damaged ROM header";
  if (e->flags & PB_LIB_FLAG_BAD_SIZE) return "ROM size does not match";
  if (e->ram_size > max_ram) return "needs too much save RAM";
  return NULL;
}

void lm_format_bytes(uint32_t bytes, char *out, size_t out_size) {
  if (bytes >= 1024u * 1024u && bytes % (1024u * 1024u) == 0)
    snprintf(out, out_size, "%uM", (unsigned)(bytes / (1024u * 1024u)));
  else if (bytes >= 1024u && bytes % 1024u == 0)
    snprintf(out, out_size, "%uK", (unsigned)(bytes / 1024u));
  else
    snprintf(out, out_size, "%uB", (unsigned)bytes);
}
