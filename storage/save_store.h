// storage/save_store: battery saves (cartridge RAM) in the flash save region, safe against power
// loss. Pure logic: flash is reached through a small ops table, so the same code runs on the
// device (XIP reads, flash_safe_execute writes) and on the PC against a simulated flash, where
// the tests cut the power at every step of a save.
//
// Layout (docs/FLASH_LAYOUT.md): the claim sector at PB_SETTINGS_OFFSET marks the region as ours;
// the save slots region (PB_SAVE_SLOTS_OFFSET ...) holds "copies". A copy is one header sector
// followed by the payload in whole sectors. Each game (keyed by its ROM CRC-32) has two copies
// that are rewritten alternately. A save erases and programs the OLDER copy, writing its header
// LAST, so until the header is complete the newer copy is untouched and still wins; a header
// that is incomplete or whose payload CRC does not match is simply ignored.
//
// The store never erases or programs outside the settings + save regions (flash_layout.h guards),
// and never touches a region it has not been told to claim (pb_save_format).
#ifndef PB_SAVE_STORE_H
#define PB_SAVE_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flash_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

// Flash access. `read` returns a pointer to the flash contents at `offset` (offsets are from the
// start of flash); `erase` and `program` return false on failure.
typedef struct {
  const uint8_t *(*read)(void *ctx, uint32_t offset);
  bool (*erase)(void *ctx, uint32_t offset, uint32_t len);                          // whole sectors
  bool (*program)(void *ctx, uint32_t offset, const uint8_t *data, uint32_t len);   // whole pages, data in RAM
  void *ctx;
} pb_flash_ops_t;

typedef enum {
  PB_SAVE_OK = 0,
  PB_SAVE_BLANK,        // region never used: pb_save_format() may claim it without asking
  PB_SAVE_FOREIGN,      // region holds data that is not ours: ask before pb_save_format()
  PB_SAVE_NOT_FOUND,    // no valid save for this key
  PB_SAVE_ERR_FULL,     // no room for another save
  PB_SAVE_ERR_FLASH,    // an erase or program failed, or a guard refused the range
  PB_SAVE_ERR_ARGS      // bad arguments or the store is not ready
} pb_save_status_t;

#define PB_SAVE_CLAIM_MAGIC "PBSV"
#define PB_SAVE_COPY_MAGIC "PBSC"
#define PB_SAVE_VERSION 1
#define PB_SAVE_CLAIM_SIZE 16   // bytes used in the claim sector (rest stays erased)
#define PB_SAVE_HEADER_SIZE 32  // bytes used in a copy's header sector
#define PB_SAVE_SLOT_SECTORS (PB_SAVE_SLOTS_SIZE / PB_FLASH_SECTOR_SIZE)  // 510
#define PB_SAVE_MAX_COPIES 64
#define PB_SAVE_MAX_PAYLOAD (1024u * 1024u)

typedef struct {
  uint32_t key;           // ROM CRC-32
  uint32_t seq;           // increases with every save (compared with wrap-around)
  uint32_t first_sector;  // header sector, counted from the start of the save slots region
  uint32_t n_sectors;     // header + payload sectors
  uint32_t payload_len;
} pb_save_copy_t;

typedef struct {
  pb_flash_ops_t ops;
  bool ready;             // the region is claimed (ours) and scanned
  uint32_t store_id;      // random id written at format; copy headers carry it
  uint32_t n_copies;
  bool truncated;         // more copies on flash than PB_SAVE_MAX_COPIES were found
  pb_save_copy_t copies[PB_SAVE_MAX_COPIES];
  uint8_t page[PB_FLASH_PAGE_SIZE];  // scratch for programming (RAM, never flash)
} pb_save_store_t;

// Looks at the claim sector. PB_SAVE_OK: it is ours; the copies are scanned and the store is
// ready. PB_SAVE_BLANK / PB_SAVE_FOREIGN: not ready; nothing was modified.
pb_save_status_t pb_save_open(pb_save_store_t *s, const pb_flash_ops_t *ops);

// Claims the region: erases the claim sector and writes a header with `store_id` (any value, for
// example the microsecond clock). Existing copies that other data left in the slots region cannot
// match the new id, so they are ignored (and erased on demand when the space is reused). The
// caller decides whether the author has agreed to this for a FOREIGN region.
pb_save_status_t pb_save_format(pb_save_store_t *s, const pb_flash_ops_t *ops, uint32_t store_id);

// Copies the newest valid save for `key` into `buf` (capacity `cap`). PB_SAVE_NOT_FOUND if none
// (the caller starts with empty RAM).
pb_save_status_t pb_save_load(pb_save_store_t *s, uint32_t key, uint8_t *buf, uint32_t cap, uint32_t *len_out);

// Saves `len` bytes for `key`. Safe against power loss at any point.
pb_save_status_t pb_save_write(pb_save_store_t *s, uint32_t key, const uint8_t *data, uint32_t len);

// Deletes every stored copy of the save for `key` (nothing else is touched). PB_SAVE_NOT_FOUND if
// there was none (no flash access happens). Safe against power loss: the oldest copy goes first and
// the newest last, so an interrupted delete never brings an older save back; it leaves either the
// newest save intact (try again) or nothing. Each copy is erased starting with its header sector,
// then read back. On PB_SAVE_ERR_FLASH the table is rescanned from flash.
pb_save_status_t pb_save_delete(pb_save_store_t *s, uint32_t key);

#ifdef __cplusplus
}
#endif

#endif  // PB_SAVE_STORE_H
