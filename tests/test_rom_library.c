// Host tests for storage/crc32 and storage/rom_library. The golden index is produced by
// tools/pack_library.py from synthetic ROMs (tests/synthetic_roms.py); this proves that the
// PC tool and the firmware reader agree on the format, and that damaged indexes are refused.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "rom_library.h"

static int failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                            \
    }                                                        \
  } while (0)

typedef struct {
  const char *name;
  uint32_t offset, size, crc, ram, banks, flags, mapper, cart_type;
} expect_t;

// Printed by the Python tool for tests/golden/index_v1.bin.
static const expect_t kGolden[] = {
    {"TONE-ONLY", 0x00101000, 32768, 0x8B259BE9, 0, 2, 0x0000, 0, 0x00},
    {"MBC1 GAME", 0x00109000, 131072, 0x28B3383F, 8192, 8, 0x0000, 1, 0x03},
    {"BIG MBC3", 0x00129000, 1048576, 0x1F4ED328, 32768, 64, 0x0000, 3, 0x13},
    {"MBC2 THING", 0x00229000, 262144, 0xA7A90BEA, 512, 16, 0x0000, 2, 0x06},
    {"CGB ONLY", 0x00269000, 524288, 0x4FB2A539, 0, 32, 0x0002, 5, 0x19},
    {"CAMERA", 0x002E9000, 32768, 0xC4A54A8C, 0, 2, 0x0001, 255, 0xFC},
    {"BAD SIZE", 0x002F1000, 40960, 0x6458F4EB, 0, 4, 0x0008, 0, 0x00},
};
#define N_GOLDEN (sizeof(kGolden) / sizeof(kGolden[0]))

static uint8_t g_golden[PB_LIB_INDEX_SIZE];

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

// Recompute the entries CRC after a test edited an entry, so that only the entry itself is bad.
static void fix_crc(uint8_t *idx) {
  const uint32_t count = idx[6] | (idx[7] << 8);
  put32(idx + 12, pb_crc32(idx + PB_LIB_HEADER_SIZE, (size_t)count * PB_LIB_ENTRY_SIZE));
}
static uint8_t *entry(uint8_t *idx, unsigned i) { return idx + PB_LIB_HEADER_SIZE + i * PB_LIB_ENTRY_SIZE; }

static pb_lib_status_t open_copy(const uint8_t *src, size_t len, uint32_t *bad) {
  static uint8_t buf[PB_LIB_INDEX_SIZE];
  static pb_lib_t lib;
  memcpy(buf, src, len);
  return pb_lib_open(&lib, buf, len, bad);
}

int main(void) {
  // ---- CRC-32 ----
  CHECK(pb_crc32("123456789", 9) == 0xCBF43926u);  // the standard check value
  CHECK(pb_crc32("", 0) == 0);
  {
    uint8_t data[1000];
    for (int i = 0; i < 1000; i++) data[i] = (uint8_t)(i * 31 + 7);
    const uint32_t whole = pb_crc32(data, 1000);
    uint32_t part = pb_crc32_update(0, data, 1);
    part = pb_crc32_update(part, data + 1, 333);
    part = pb_crc32_update(part, data + 334, 666);
    CHECK(whole == part);
  }

  // ---- golden index ----
  FILE *f = fopen(GOLDEN_PATH, "rb");
  if (!f || fread(g_golden, 1, sizeof(g_golden), f) != sizeof(g_golden)) {
    printf("FAIL: cannot read %s\n", GOLDEN_PATH);
    return 1;
  }
  fclose(f);

  pb_lib_t lib;
  uint32_t bad = 99;
  CHECK(pb_lib_open(&lib, g_golden, sizeof(g_golden), &bad) == PB_LIB_OK);
  CHECK(lib.count == N_GOLDEN);
  CHECK(PB_LIB_MAX_ENTRIES == 85);
  for (uint32_t i = 0; i < lib.count && i < N_GOLDEN; i++) {
    pb_lib_entry_t e;
    pb_lib_get(&lib, i, &e);
    const expect_t *x = &kGolden[i];
    CHECK(strcmp(e.name, x->name) == 0);
    CHECK(e.flash_offset == x->offset);
    CHECK(e.size == x->size);
    CHECK(e.rom_crc32 == x->crc);
    CHECK(e.ram_size == x->ram);
    CHECK(e.rom_banks == x->banks);
    CHECK(e.flags == x->flags);
    CHECK(e.mapper == x->mapper);
    CHECK(e.cart_type == x->cart_type);
    CHECK(pb_lib_entry_launchable(&e) == (x->flags == 0));
    CHECK(pb_lib_entry_xip_address(&e) == 0x10000000u + x->offset);
  }

  // ---- no library / wrong file / damaged header ----
  uint8_t blank[PB_LIB_INDEX_SIZE];
  memset(blank, 0xFF, sizeof(blank));
  CHECK(open_copy(blank, sizeof(blank), NULL) == PB_LIB_EMPTY);
  memset(blank, 0x00, sizeof(blank));
  CHECK(open_copy(blank, sizeof(blank), NULL) == PB_LIB_ERR_MAGIC);
  CHECK(open_copy(g_golden, 8, NULL) == PB_LIB_ERR_TOO_SHORT);

  uint8_t t[PB_LIB_INDEX_SIZE];
  memcpy(t, g_golden, sizeof(t));
  t[0] = 'X';
  CHECK(open_copy(t, sizeof(t), NULL) == PB_LIB_ERR_MAGIC);
  memcpy(t, g_golden, sizeof(t));
  put16(t + 4, 2);
  CHECK(open_copy(t, sizeof(t), NULL) == PB_LIB_ERR_VERSION);
  memcpy(t, g_golden, sizeof(t));
  put16(t + 6, PB_LIB_MAX_ENTRIES + 1);
  CHECK(open_copy(t, sizeof(t), NULL) == PB_LIB_ERR_FORMAT);
  memcpy(t, g_golden, sizeof(t));
  put16(t + 8, PB_LIB_ENTRY_SIZE - 1);
  CHECK(open_copy(t, sizeof(t), NULL) == PB_LIB_ERR_FORMAT);
  CHECK(open_copy(g_golden, PB_LIB_HEADER_SIZE + 3 * PB_LIB_ENTRY_SIZE, NULL) == PB_LIB_ERR_FORMAT);  // count 7 but sector cut short

  // ---- any damaged entry byte without fixing the CRC is caught by the CRC ----
  memcpy(t, g_golden, sizeof(t));
  t[PB_LIB_HEADER_SIZE + 3 * PB_LIB_ENTRY_SIZE + 36] ^= 0x01;
  CHECK(open_copy(t, sizeof(t), NULL) == PB_LIB_ERR_CRC);

  // ---- structurally bad entries (CRC repaired so the entry itself is judged) ----
#define BAD_ENTRY(label, idx_no, edit_stmt)                                   \
  do {                                                                         \
    memcpy(t, g_golden, sizeof(t));                                            \
    uint8_t *e_ = entry(t, (idx_no));                                          \
    edit_stmt;                                                                 \
    fix_crc(t);                                                                \
    uint32_t bad_ = 99;                                                        \
    const pb_lib_status_t st_ = open_copy(t, sizeof(t), &bad_);                \
    if (st_ != PB_LIB_ERR_ENTRY || bad_ != (idx_no)) {                         \
      printf("FAIL %s:%d: %s (status %d, entry %u)\n", __FILE__, __LINE__, label, (int)st_, (unsigned)bad_); \
      failures++;                                                              \
    }                                                                          \
  } while (0)

  BAD_ENTRY("unaligned offset", 1, put32(e_ + 24, PB_ROM_AREA_OFFSET + 0x9000 + 1));
  BAD_ENTRY("offset below the ROM area", 1, put32(e_ + 24, PB_LIB_INDEX_OFFSET));
  BAD_ENTRY("offset at the area end", 1, put32(e_ + 24, PB_ROM_AREA_END));
  BAD_ENTRY("ROM runs past the area end", 1, (put32(e_ + 24, PB_ROM_AREA_END - 0x1000), put32(e_ + 28, 0x1001)));
  BAD_ENTRY("size zero", 1, put32(e_ + 28, 0));
  BAD_ENTRY("size smaller than a header", 1, put32(e_ + 28, 0x14F));
  BAD_ENTRY("size so large it would overflow", 1, put32(e_ + 28, 0xFFFFFFFFu));
  BAD_ENTRY("overlaps the previous ROM", 2, put32(e_ + 24, 0x00109000 + 0x1000));  // inside entry 1 (128 KiB)
  BAD_ENTRY("identical to an earlier ROM", 3, memcpy(e_ + 24, entry(t, 0) + 24, 8));
  BAD_ENTRY("empty name", 1, e_[0] = 0);
  BAD_ENTRY("name without terminating NUL", 1, memset(e_, 'A', PB_LIB_NAME_LEN));
  BAD_ENTRY("name with a control character", 1, e_[2] = 0x07);

  // A ROM ending exactly at the area end is fine (boundary), and so is a ROM right after another.
  memcpy(t, g_golden, sizeof(t));
  put32(entry(t, 6) + 24, PB_ROM_AREA_END - 0xA000);  // 40960 bytes = 10 sectors = 0xA000
  fix_crc(t);
  CHECK(open_copy(t, sizeof(t), &bad) == PB_LIB_OK);

  // Unknown future flag bits do not make an index invalid.
  memcpy(t, g_golden, sizeof(t));
  put16(entry(t, 0) + 42, 0x8000);
  fix_crc(t);
  CHECK(open_copy(t, sizeof(t), &bad) == PB_LIB_OK);

  // An empty library (count 0, CRC of nothing) is valid and has no entries.
  memset(t, 0xFF, sizeof(t));
  memcpy(t, "PBLB", 4);
  put16(t + 4, 1);
  put16(t + 6, 0);
  put16(t + 8, PB_LIB_ENTRY_SIZE);
  put16(t + 10, 0);
  put32(t + 12, 0);
  CHECK(open_copy(t, sizeof(t), &bad) == PB_LIB_OK);

  // A full library (85 entries) with unique ROM slots is accepted.
  memset(t, 0xFF, sizeof(t));
  memcpy(t, "PBLB", 4);
  put16(t + 4, 1);
  put16(t + 6, PB_LIB_MAX_ENTRIES);
  put16(t + 8, PB_LIB_ENTRY_SIZE);
  put16(t + 10, 0);
  for (unsigned i = 0; i < PB_LIB_MAX_ENTRIES; i++) {
    uint8_t *e = entry(t, i);
    memset(e, 0, PB_LIB_ENTRY_SIZE);
    memcpy(e, "ROM", 3);
    put32(e + 24, PB_ROM_AREA_OFFSET + i * 0x8000);
    put32(e + 28, 0x8000);
  }
  fix_crc(t);
  CHECK(open_copy(t, sizeof(t), &bad) == PB_LIB_OK);

  if (failures == 0) printf("rom library tests passed\n");
  return failures ? 1 : 0;
}
