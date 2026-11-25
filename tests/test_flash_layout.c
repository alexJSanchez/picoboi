// Host tests for storage/flash_layout.h: the compile-time guards are in the header itself;
// these check the write-guard functions at and around the region boundaries.
#include <stdio.h>

#include "flash_layout.h"

static int failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                            \
    }                                                        \
  } while (0)

#define S PB_FLASH_SECTOR_SIZE
#define P PB_FLASH_PAGE_SIZE

int main(void) {
  // Sizes derived from the layout.
  CHECK(PB_ROM_AREA_SIZE == 0x8FF000u);
  CHECK(PB_SAVE_SLOTS_SIZE == 0x1FE000u);
  CHECK(PB_SAVE_SLOTS_END == 0xC00000u);

  // Erase: only sectors in settings + save slots.
  CHECK(pb_flash_erase_allowed(PB_SETTINGS_OFFSET, S));
  CHECK(pb_flash_erase_allowed(PB_SETTINGS_OFFSET, PB_SETTINGS_SIZE + PB_SAVE_SLOTS_SIZE));  // whole area
  CHECK(pb_flash_erase_allowed(PB_SAVE_SLOTS_END - S, S));                                    // last sector
  CHECK(!pb_flash_erase_allowed(PB_SAVE_SLOTS_END, S));                                       // first sector past it
  CHECK(!pb_flash_erase_allowed(PB_SAVE_SLOTS_END - S, 2 * S));                               // runs past the end
  CHECK(!pb_flash_erase_allowed(PB_SETTINGS_OFFSET - S, S));                                  // last ROM-area sector
  CHECK(!pb_flash_erase_allowed(PB_SETTINGS_OFFSET - S, 2 * S));                              // straddles ROM area and settings
  CHECK(!pb_flash_erase_allowed(PB_SETTINGS_OFFSET + 1, S));                                  // not sector aligned
  CHECK(!pb_flash_erase_allowed(PB_SETTINGS_OFFSET, S + 1));                                  // length not a sector multiple
  CHECK(!pb_flash_erase_allowed(PB_SETTINGS_OFFSET, 0));                                      // empty

  // Never the firmware, the index, the ROM area, or the SDK's top 4 MiB.
  CHECK(!pb_flash_erase_allowed(PB_FW_OFFSET, S));
  CHECK(!pb_flash_erase_allowed(PB_LIB_INDEX_OFFSET, S));
  CHECK(!pb_flash_erase_allowed(PB_ROM_AREA_OFFSET, S));
  CHECK(!pb_flash_erase_allowed(0xC00000u, S));
  CHECK(!pb_flash_erase_allowed(0xE00000u, S));  // where the old plan had the saves
  CHECK(!pb_flash_erase_allowed(PB_FLASH_TOTAL_SIZE - S, S));

  // Program: whole pages in the same area.
  CHECK(pb_flash_program_allowed(PB_SAVE_SLOTS_OFFSET, P));
  CHECK(pb_flash_program_allowed(PB_SAVE_SLOTS_END - P, P));
  CHECK(!pb_flash_program_allowed(PB_SAVE_SLOTS_END - P, 2 * P));
  CHECK(!pb_flash_program_allowed(PB_SAVE_SLOTS_OFFSET + 1, P));
  CHECK(!pb_flash_program_allowed(PB_SAVE_SLOTS_OFFSET, P - 1));
  CHECK(!pb_flash_program_allowed(PB_ROM_AREA_OFFSET, P));
  CHECK(!pb_flash_program_allowed(PB_SETTINGS_OFFSET - P, P));

  // Arithmetic overflow cannot turn a bad range into a good one.
  CHECK(!pb_flash_in_writable_area(PB_SAVE_SLOTS_OFFSET, 0xFFFFFFFFu));
  CHECK(!pb_flash_in_writable_area(0xFFFFFF00u, 0x200u));
  CHECK(!pb_flash_in_writable_area(PB_SETTINGS_OFFSET, 0xFFFFFFFFu - PB_SETTINGS_OFFSET + 8u));

  if (failures == 0) printf("flash layout tests passed\n");
  return failures ? 1 : 0;
}
