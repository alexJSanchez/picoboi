// storage/flash_layout.h: the single source of truth for flash addresses (CLAUDE.md).
// No other file may hard-code a flash offset. Documented in docs/FLASH_LAYOUT.md; the
// decision to stay inside the first 12 MiB is recorded in docs/DECISIONS.md.
//
// Offsets are from the start of flash (add PB_FLASH_XIP_BASE for the CPU address).
//
//   0x000000 +--------------------+
//            | firmware (1 MiB)   |  code and constants; the linker is capped here
//   0x100000 +--------------------+
//            | library index 4 K  |  one sector, written by tools/pack_library.py
//   0x101000 +--------------------+
//            | ROM area (~9 MiB)  |  ROMs back to back, 4 KiB aligned, read in place (XIP)
//   0xA00000 +--------------------+
//            | settings (8 K)     |  written by the firmware
//   0xA02000 +--------------------+
//            | save slots         |  written by the firmware
//   0xC00000 +--------------------+  end of the user flash the SDK linker script allows
//            | NOT OURS (4 MiB)   |  reserved by the PicoSystem SDK for its FAT filesystem
//  0x1000000 +--------------------+
#ifndef PB_FLASH_LAYOUT_H
#define PB_FLASH_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#define PB_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define PB_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#define PB_FLASH_XIP_BASE 0x10000000u
#define PB_FLASH_TOTAL_SIZE 0x01000000u  // 16 MiB chip
#define PB_FLASH_SECTOR_SIZE 4096u       // smallest erasable unit
#define PB_FLASH_PAGE_SIZE 256u          // smallest programmable unit

// Everything we may use ends here; the top 4 MiB belong to the SDK (FAT).
#define PB_FLASH_USER_END 0x00C00000u

#define PB_FW_OFFSET 0x00000000u
#define PB_FW_SIZE 0x00100000u

#define PB_LIB_INDEX_OFFSET 0x00100000u
#define PB_LIB_INDEX_SIZE 0x00001000u

#define PB_ROM_AREA_OFFSET 0x00101000u
#define PB_ROM_AREA_END 0x00A00000u
#define PB_ROM_AREA_SIZE (PB_ROM_AREA_END - PB_ROM_AREA_OFFSET)

#define PB_SETTINGS_OFFSET 0x00A00000u
#define PB_SETTINGS_SIZE 0x00002000u  // two sectors (A/B copies)

#define PB_SAVE_SLOTS_OFFSET 0x00A02000u
#define PB_SAVE_SLOTS_END PB_FLASH_USER_END
#define PB_SAVE_SLOTS_SIZE (PB_SAVE_SLOTS_END - PB_SAVE_SLOTS_OFFSET)

// ---- compile-time guards: regions do not overlap, fit in the user flash, and are aligned ----
PB_STATIC_ASSERT(PB_FW_OFFSET + PB_FW_SIZE == PB_LIB_INDEX_OFFSET, "firmware must end where the index starts");
PB_STATIC_ASSERT(PB_LIB_INDEX_OFFSET + PB_LIB_INDEX_SIZE == PB_ROM_AREA_OFFSET, "index must end where the ROM area starts");
PB_STATIC_ASSERT(PB_ROM_AREA_END == PB_SETTINGS_OFFSET, "ROM area must end where settings start");
PB_STATIC_ASSERT(PB_SETTINGS_OFFSET + PB_SETTINGS_SIZE == PB_SAVE_SLOTS_OFFSET, "settings must end where save slots start");
PB_STATIC_ASSERT(PB_SAVE_SLOTS_END <= PB_FLASH_USER_END, "regions must stay inside the user flash");
PB_STATIC_ASSERT(PB_FLASH_USER_END < PB_FLASH_TOTAL_SIZE, "the top of the chip must stay with the SDK");
// Decision 2026-11-25 (docs/DECISIONS.md): stay inside the 12 MiB the SDK linker script calls
// user flash (`FLASH` in memmap_picosystem.ld); the FAT region above it is not ours.
PB_STATIC_ASSERT(PB_FLASH_USER_END == 12u * 1024u * 1024u, "usable flash must stay at 12 MiB");
PB_STATIC_ASSERT(PB_ROM_AREA_OFFSET < PB_ROM_AREA_END, "ROM area must not be empty");
PB_STATIC_ASSERT(PB_FW_OFFSET % PB_FLASH_SECTOR_SIZE == 0, "firmware not sector aligned");
PB_STATIC_ASSERT(PB_FW_SIZE % PB_FLASH_SECTOR_SIZE == 0, "firmware size not sector aligned");
PB_STATIC_ASSERT(PB_LIB_INDEX_OFFSET % PB_FLASH_SECTOR_SIZE == 0, "index not sector aligned");
PB_STATIC_ASSERT(PB_LIB_INDEX_SIZE % PB_FLASH_SECTOR_SIZE == 0, "index size not sector aligned");
PB_STATIC_ASSERT(PB_ROM_AREA_OFFSET % PB_FLASH_SECTOR_SIZE == 0, "ROM area not sector aligned");
PB_STATIC_ASSERT(PB_ROM_AREA_END % PB_FLASH_SECTOR_SIZE == 0, "ROM area end not sector aligned");
PB_STATIC_ASSERT(PB_SETTINGS_OFFSET % PB_FLASH_SECTOR_SIZE == 0, "settings not sector aligned");
PB_STATIC_ASSERT(PB_SETTINGS_SIZE % PB_FLASH_SECTOR_SIZE == 0, "settings size not sector aligned");
PB_STATIC_ASSERT(PB_SAVE_SLOTS_OFFSET % PB_FLASH_SECTOR_SIZE == 0, "save slots not sector aligned");
PB_STATIC_ASSERT(PB_SAVE_SLOTS_SIZE % PB_FLASH_SECTOR_SIZE == 0, "save slots size not sector aligned");

// ---- write guards ----
// The firmware only ever erases or programs the settings and save-slot regions. The index
// and ROM area are written from the PC (UF2); the firmware region by flashing.

// True if [offset, offset + len) lies completely inside [region_start, region_end).
static inline bool pb_flash_inside(uint32_t offset, uint32_t len, uint32_t region_start,
                                   uint32_t region_end) {
  if (len == 0 || offset < region_start || offset >= region_end) return false;
  return len <= region_end - offset;  // no overflow: offset < region_end
}

// True if the range is in the area the firmware may modify (settings + save slots).
static inline bool pb_flash_in_writable_area(uint32_t offset, uint32_t len) {
  return pb_flash_inside(offset, len, PB_SETTINGS_OFFSET, PB_SAVE_SLOTS_END);
}

// Erase: whole sectors, inside the writable area.
static inline bool pb_flash_erase_allowed(uint32_t offset, uint32_t len) {
  return pb_flash_in_writable_area(offset, len) && offset % PB_FLASH_SECTOR_SIZE == 0 &&
         len % PB_FLASH_SECTOR_SIZE == 0;
}

// Program: whole pages, inside the writable area.
static inline bool pb_flash_program_allowed(uint32_t offset, uint32_t len) {
  return pb_flash_in_writable_area(offset, len) && offset % PB_FLASH_PAGE_SIZE == 0 &&
         len % PB_FLASH_PAGE_SIZE == 0;
}

#ifdef __cplusplus
}
#endif

#endif  // PB_FLASH_LAYOUT_H
