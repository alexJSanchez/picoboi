#include "flash_test.h"

#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "picosystem.hpp"

#include "crc32.h"
#include "flash_layout.h"
#include "rom_library.h"

// End of the firmware image in flash (from the linker script).
extern "C" char __flash_binary_end;

namespace {

const uint8_t *flash_at(uint32_t offset) { return (const uint8_t *)(PB_FLASH_XIP_BASE + offset); }

// ---- settings marker: one 256-byte page at the start of the settings region ----
constexpr char MARKER_MAGIC[16] = "PBFLASHTEST-v1";
struct Marker {
  char magic[16];
  uint8_t pattern[236];
  uint32_t crc;  // CRC-32 of everything before this field
};
static_assert(sizeof(Marker) == PB_FLASH_PAGE_SIZE, "marker must be exactly one flash page");

enum MarkerState { M_VALID, M_BLANK, M_UNKNOWN };

Marker g_page;  // RAM copy used for programming (flash functions must not read it from flash)

void build_marker(Marker *m) {
  memset(m, 0, sizeof(*m));
  memcpy(m->magic, MARKER_MAGIC, sizeof(MARKER_MAGIC));
  for (unsigned i = 0; i < sizeof(m->pattern); i++) m->pattern[i] = (uint8_t)(i * 7 + 3);
  m->crc = pb_crc32(m, offsetof(Marker, crc));
}

MarkerState marker_state() {
  const uint8_t *p = flash_at(PB_SETTINGS_OFFSET);
  bool blank = true;
  for (unsigned i = 0; i < PB_FLASH_PAGE_SIZE; i++)
    if (p[i] != 0xFF) blank = false;
  if (blank) return M_BLANK;
  Marker want;
  build_marker(&want);
  return memcmp(p, &want, sizeof(want)) == 0 ? M_VALID : M_UNKNOWN;
}

void erase_and_program(void *) {  // runs from RAM with interrupts off (flash_safe_execute)
  flash_range_erase(PB_SETTINGS_OFFSET, PB_FLASH_SECTOR_SIZE);
  flash_range_program(PB_SETTINGS_OFFSET, (const uint8_t *)&g_page, PB_FLASH_PAGE_SIZE);
}

// ---- results of the checks ----
struct Report {
  uint32_t fw_len, fw_crc;
  pb_lib_status_t lib_status;
  uint32_t lib_count, lib_bad_entry;
  uint32_t rom_ok, rom_bad;
  MarkerState marker_before, marker_after;
  bool wrote;
  int write_result;  // PICO_OK (0) or an error code, valid if wrote
  uint32_t save_crc;
  bool save_blank;
} g_rep;

constexpr unsigned MAX_SHOWN = 8;
struct Shown {
  char name[PB_LIB_NAME_LEN];
  uint32_t offset, size, flags, idx_crc, flash_crc;
} g_shown[MAX_SHOWN];
unsigned g_n_shown = 0;

const char *lib_status_name(pb_lib_status_t s) {
  switch (s) {
    case PB_LIB_OK: return "OK";
    case PB_LIB_EMPTY: return "EMPTY (erased)";
    case PB_LIB_ERR_TOO_SHORT: return "TOO SHORT";
    case PB_LIB_ERR_MAGIC: return "NO LIBRARY (bad magic)";
    case PB_LIB_ERR_VERSION: return "BAD VERSION";
    case PB_LIB_ERR_FORMAT: return "BAD FORMAT";
    case PB_LIB_ERR_CRC: return "BAD CRC";
    case PB_LIB_ERR_ENTRY: return "BAD ENTRY";
  }
  return "?";
}

const char *marker_name(MarkerState s) {
  return s == M_VALID ? "valid" : (s == M_BLANK ? "blank" : "UNKNOWN DATA");
}

// ---- read-only map of the first 12 MiB: runs of blank (all 0xFF) and used 4 KiB sectors ----
struct Run {
  uint32_t start, end;  // flash offsets, [start, end)
  bool blank;
};
constexpr unsigned MAX_RUNS = 64;
Run g_runs[MAX_RUNS];
unsigned g_n_runs = 0;
bool g_runs_overflow = false;
uint32_t g_used_sectors = 0;

bool sector_blank(uint32_t offset) {
  const uint32_t *w = (const uint32_t *)flash_at(offset);
  for (unsigned i = 0; i < PB_FLASH_SECTOR_SIZE / 4; i++)
    if (w[i] != 0xFFFFFFFFu) return false;
  return true;
}

void scan_flash() {
  for (uint32_t off = 0; off < PB_FLASH_USER_END; off += PB_FLASH_SECTOR_SIZE) {
    const bool blank = sector_blank(off);
    if (!blank) g_used_sectors++;
    if (g_n_runs && g_runs[g_n_runs - 1].blank == blank) {
      g_runs[g_n_runs - 1].end = off + PB_FLASH_SECTOR_SIZE;
    } else if (g_n_runs < MAX_RUNS) {
      g_runs[g_n_runs++] = {off, off + PB_FLASH_SECTOR_SIZE, blank};
    } else {
      g_runs_overflow = true;
    }
  }
}

// First bytes of the two areas that were not blank, to see what they are.
uint8_t g_head_settings[16], g_head_saves[16];

uint32_t g_next_print_ms = 0;

}  // namespace

void flash_test_init() {
  // 1. The firmware image: its length comes from the linker, its CRC must not change when
  //    only the library is flashed.
  g_rep.fw_len = (uint32_t)(&__flash_binary_end) - PB_FLASH_XIP_BASE;
  g_rep.fw_crc = pb_crc32(flash_at(0), g_rep.fw_len);

  // 2. The library index, and every ROM re-read from flash and compared with the recorded CRC.
  pb_lib_t lib;
  g_rep.lib_status = pb_lib_open(&lib, flash_at(PB_LIB_INDEX_OFFSET), PB_LIB_INDEX_SIZE, &g_rep.lib_bad_entry);
  g_rep.lib_count = lib.count;
  if (g_rep.lib_status == PB_LIB_OK) {
    for (uint32_t i = 0; i < lib.count; i++) {
      pb_lib_entry_t e;
      pb_lib_get(&lib, i, &e);
      const uint32_t crc = pb_crc32(flash_at(e.flash_offset), e.size);
      (crc == e.rom_crc32 ? g_rep.rom_ok : g_rep.rom_bad)++;
      if (g_n_shown < MAX_SHOWN) {
        Shown &s = g_shown[g_n_shown++];
        memcpy(s.name, e.name, sizeof(s.name));
        s.offset = e.flash_offset;
        s.size = e.size;
        s.flags = e.flags;
        s.idx_crc = e.rom_crc32;
        s.flash_crc = crc;
      }
    }
  }

  // 3. The settings marker: write it if the page is blank; never touch unknown data.
  g_rep.marker_before = marker_state();
  g_rep.marker_after = g_rep.marker_before;
  if (g_rep.marker_before == M_BLANK) {
    if (pb_flash_erase_allowed(PB_SETTINGS_OFFSET, PB_FLASH_SECTOR_SIZE) &&
        pb_flash_program_allowed(PB_SETTINGS_OFFSET, PB_FLASH_PAGE_SIZE)) {
      build_marker(&g_page);
      g_rep.wrote = true;
      g_rep.write_result = flash_safe_execute(erase_and_program, nullptr, 1000);
      g_rep.marker_after = marker_state();
    } else {
      g_rep.write_result = -1000;  // our own guard refused: should be impossible
    }
  }

  // 4. The save area must still be as we left it (blank on a fresh device).
  const uint8_t *s = flash_at(PB_SAVE_SLOTS_OFFSET);
  g_rep.save_blank = true;
  for (unsigned i = 0; i < PB_FLASH_SECTOR_SIZE; i++)
    if (s[i] != 0xFF) g_rep.save_blank = false;
  g_rep.save_crc = pb_crc32(s, PB_FLASH_SECTOR_SIZE);

  // 5. Read-only map of what is stored in the first 12 MiB, and a peek at the first bytes.
  scan_flash();
  memcpy(g_head_settings, flash_at(PB_SETTINGS_OFFSET), sizeof(g_head_settings));
  memcpy(g_head_saves, flash_at(PB_SAVE_SLOTS_OFFSET), sizeof(g_head_saves));
}

void flash_test_update() {
  const uint32_t now = picosystem::time();
  if (now < g_next_print_ms) return;
  g_next_print_ms = now + 3000;

  printf("flashtest: build %s %s\n", __DATE__, __TIME__);
  printf("flashtest: firmware image %u bytes, crc32 %08x\n", (unsigned)g_rep.fw_len, (unsigned)g_rep.fw_crc);
  printf("flashtest: library sector (offset 0x%06x): %s", (unsigned)PB_LIB_INDEX_OFFSET, lib_status_name(g_rep.lib_status));
  if (g_rep.lib_status == PB_LIB_OK) printf(", %u ROMs\n", (unsigned)g_rep.lib_count);
  else if (g_rep.lib_status == PB_LIB_ERR_ENTRY) printf(" (entry %u)\n", (unsigned)g_rep.lib_bad_entry);
  else printf("\n");
  for (unsigned i = 0; i < g_n_shown; i++) {
    const Shown &s = g_shown[i];
    printf("flashtest:   [%u] %-12s offset 0x%06x size %7u flags 0x%04x index crc %08x flash crc %08x %s\n",
           i, s.name, (unsigned)s.offset, (unsigned)s.size, (unsigned)s.flags, (unsigned)s.idx_crc,
           (unsigned)s.flash_crc, s.idx_crc == s.flash_crc ? "MATCH" : "MISMATCH");
  }
  if (g_rep.lib_status == PB_LIB_OK) printf("flashtest: ROM data read back from flash: %u match, %u mismatch\n", (unsigned)g_rep.rom_ok, (unsigned)g_rep.rom_bad);
  printf("flashtest: settings marker at 0x%06x: was %s", (unsigned)PB_SETTINGS_OFFSET, marker_name(g_rep.marker_before));
  if (g_rep.wrote) printf(", wrote it now (flash_safe_execute returned %d), now %s", g_rep.write_result, marker_name(g_rep.marker_after));
  printf("\n");
  printf("flashtest: first save-area sector at 0x%06x: %s, crc32 %08x\n", (unsigned)PB_SAVE_SLOTS_OFFSET,
         g_rep.save_blank ? "blank" : "HAS DATA", (unsigned)g_rep.save_crc);

  auto hex = [](const char *label, const uint8_t *b) {
    printf("flashtest: first 16 bytes at %s:", label);
    for (int i = 0; i < 16; i++) printf(" %02x", b[i]);
    printf("  |");
    for (int i = 0; i < 16; i++) printf("%c", (b[i] >= 0x20 && b[i] < 0x7F) ? b[i] : '.');
    printf("|\n");
  };
  hex("settings 0xa00000", g_head_settings);
  hex("save area 0xa02000", g_head_saves);

  printf("flashtest: map of the first 12 MiB (%u of 3072 sectors hold data):\n", (unsigned)g_used_sectors);
  for (unsigned i = 0; i < g_n_runs; i++)
    printf("flashtest:   %-5s 0x%06x - 0x%06x  (%u KiB)\n", g_runs[i].blank ? "blank" : "USED",
           (unsigned)g_runs[i].start, (unsigned)g_runs[i].end, (unsigned)((g_runs[i].end - g_runs[i].start) / 1024));
  if (g_runs_overflow) printf("flashtest:   (map truncated: more than %u runs)\n", MAX_RUNS);
}

void flash_test_draw() {
  using namespace picosystem;
  pen(0, 0, 2);
  clear();
  pen(15, 15, 15);
  text("FLASH TEST", 8, 8);
  char line[40];
  int y = 28;
  auto put = [&](const char *s) {
    text(s, 8, y);
    y += 12;
  };
  snprintf(line, sizeof(line), "FW CRC %08x", (unsigned)g_rep.fw_crc);
  put(line);
  snprintf(line, sizeof(line), "LIB: %s", lib_status_name(g_rep.lib_status));
  put(line);
  if (g_rep.lib_status == PB_LIB_OK) {
    snprintf(line, sizeof(line), "%u ROMs, data ok %u bad %u", (unsigned)g_rep.lib_count, (unsigned)g_rep.rom_ok,
             (unsigned)g_rep.rom_bad);
    put(line);
    for (unsigned i = 0; i < g_n_shown && i < 5; i++) {
      snprintf(line, sizeof(line), "%u %.14s %s", i, g_shown[i].name, g_shown[i].idx_crc == g_shown[i].flash_crc ? "ok" : "BAD");
      put(line);
    }
  }
  y += 6;
  snprintf(line, sizeof(line), "MARKER was %s", marker_name(g_rep.marker_before));
  put(line);
  if (g_rep.wrote) {
    snprintf(line, sizeof(line), "wrote: result %d, now %s", g_rep.write_result, marker_name(g_rep.marker_after));
    put(line);
  }
  snprintf(line, sizeof(line), "SAVES: %s %08x", g_rep.save_blank ? "blank" : "DATA", (unsigned)g_rep.save_crc);
  put(line);
  snprintf(line, sizeof(line), "12 MiB: %u sectors used", (unsigned)g_used_sectors);
  put(line);
}
