// storage/rom_library: reads and validates the ROM library index written by
// tools/pack_library.py. Pure logic (no hardware, no emulator): it works on a pointer to the
// 4 KiB index sector, which on the device is the flash itself (XIP), so nothing is copied.
// Format v1 is described in docs/FLASH_LAYOUT.md and checked against a golden file.
#ifndef PB_ROM_LIBRARY_H
#define PB_ROM_LIBRARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flash_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PB_LIB_VERSION 1
#define PB_LIB_HEADER_SIZE 16
#define PB_LIB_ENTRY_SIZE 48
#define PB_LIB_NAME_LEN 24
#define PB_LIB_MAX_ENTRIES ((PB_LIB_INDEX_SIZE - PB_LIB_HEADER_SIZE) / PB_LIB_ENTRY_SIZE)  // 85

// Entry flags (same values as tools/pack_library.py).
#define PB_LIB_FLAG_UNSUPPORTED_MAPPER 0x0001u  // the emulator core cannot run this cartridge type
#define PB_LIB_FLAG_CGB_ONLY 0x0002u            // Game Boy Color only; this build is DMG only
#define PB_LIB_FLAG_BAD_HEADER 0x0004u          // header checksum wrong or unusable header values
#define PB_LIB_FLAG_BAD_SIZE 0x0008u            // file size does not match the header
#define PB_LIB_FLAG_CGB_ENHANCED 0x0010u        // informational: runs in DMG mode
#define PB_LIB_FLAGS_BLOCKING                                                              \
  (PB_LIB_FLAG_UNSUPPORTED_MAPPER | PB_LIB_FLAG_CGB_ONLY | PB_LIB_FLAG_BAD_HEADER | \
   PB_LIB_FLAG_BAD_SIZE)

typedef enum {
  PB_LIB_OK = 0,
  PB_LIB_EMPTY,          // erased sector: no library has been flashed yet
  PB_LIB_ERR_TOO_SHORT,  // fewer bytes than a header
  PB_LIB_ERR_MAGIC,      // not a PicoBoy library
  PB_LIB_ERR_VERSION,    // written by a newer tool
  PB_LIB_ERR_FORMAT,     // entry size or count out of range
  PB_LIB_ERR_CRC,        // entries damaged
  PB_LIB_ERR_ENTRY       // one entry is invalid (index reported separately)
} pb_lib_status_t;

typedef struct {
  char name[PB_LIB_NAME_LEN];  // NUL terminated, printable ASCII
  uint32_t flash_offset;       // start of the ROM, from the start of flash (4 KiB aligned)
  uint32_t size;               // ROM size in bytes
  uint32_t rom_crc32;
  uint32_t ram_size;           // save RAM the cartridge needs, in bytes
  uint16_t rom_banks;
  uint16_t flags;              // PB_LIB_FLAG_*
  uint8_t mapper;              // 0 none, 1, 2, 3, 5 = MBC number, 0xFF = unsupported
  uint8_t cart_type;           // header byte 0x147
} pb_lib_entry_t;

// An opened (validated) index. Holds only a pointer and the count.
typedef struct {
  const uint8_t *base;
  uint32_t count;
} pb_lib_t;

// Validates the index sector (`len` bytes at `sector`, normally PB_LIB_INDEX_SIZE) and fills
// `lib`. On PB_LIB_ERR_ENTRY, `*bad_entry` (if not NULL) is the number of the first bad entry.
pb_lib_status_t pb_lib_open(pb_lib_t *lib, const uint8_t *sector, size_t len, uint32_t *bad_entry);

// Decodes entry `i` (0 <= i < lib->count) into `out`.
void pb_lib_get(const pb_lib_t *lib, uint32_t i, pb_lib_entry_t *out);

// True if the emulator can run this entry (none of the blocking flags is set).
static inline bool pb_lib_entry_launchable(const pb_lib_entry_t *e) {
  return (e->flags & PB_LIB_FLAGS_BLOCKING) == 0;
}

// CPU address of the ROM when it is read in place (XIP).
static inline uint32_t pb_lib_entry_xip_address(const pb_lib_entry_t *e) {
  return PB_FLASH_XIP_BASE + e->flash_offset;
}

#ifdef __cplusplus
}
#endif

#endif  // PB_ROM_LIBRARY_H
