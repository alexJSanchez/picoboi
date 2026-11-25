#include "rom_library.h"

#include "crc32.h"

// Header: magic[4] "PBLB", version u16, count u16, entry_size u16, reserved u16, entries_crc32 u32.
// Entry (48 bytes): name[24], offset u32, size u32, rom_crc32 u32, ram_size u32,
//                   rom_banks u16, flags u16, mapper u8, cart_type u8, reserved[2].
// All little-endian, read byte by byte (the Cortex-M0+ faults on unaligned word access).

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool name_ok(const uint8_t *name) {
  uint32_t n = 0;
  while (n < PB_LIB_NAME_LEN && name[n] != 0) {
    if (name[n] < 0x20 || name[n] > 0x7E) return false;  // printable ASCII only
    n++;
  }
  return n > 0 && n < PB_LIB_NAME_LEN;  // not empty, and a NUL was found inside the field
}

// Sectors a ROM occupies in the ROM area, as a byte length (rounded up to whole sectors).
static uint64_t allocated_bytes(uint32_t size) {
  return ((uint64_t)size + PB_FLASH_SECTOR_SIZE - 1) / PB_FLASH_SECTOR_SIZE * PB_FLASH_SECTOR_SIZE;
}

static bool entry_ok(const uint8_t *e) {
  if (!name_ok(e)) return false;
  const uint32_t offset = rd32(e + 24);
  const uint32_t size = rd32(e + 28);
  if (size < 0x150) return false;  // not even a Game Boy header
  if (offset % PB_FLASH_SECTOR_SIZE != 0) return false;
  if (offset < PB_ROM_AREA_OFFSET || offset >= PB_ROM_AREA_END) return false;
  return (uint64_t)offset + allocated_bytes(size) <= PB_ROM_AREA_END;
}

pb_lib_status_t pb_lib_open(pb_lib_t *lib, const uint8_t *sector, size_t len, uint32_t *bad_entry) {
  lib->base = NULL;
  lib->count = 0;
  if (len < PB_LIB_HEADER_SIZE) return PB_LIB_ERR_TOO_SHORT;

  bool erased = true;
  for (unsigned i = 0; i < PB_LIB_HEADER_SIZE; i++)
    if (sector[i] != 0xFF) erased = false;
  if (erased) return PB_LIB_EMPTY;

  if (sector[0] != 'P' || sector[1] != 'B' || sector[2] != 'L' || sector[3] != 'B') return PB_LIB_ERR_MAGIC;
  if (rd16(sector + 4) != PB_LIB_VERSION) return PB_LIB_ERR_VERSION;
  const uint32_t count = rd16(sector + 6);
  if (rd16(sector + 8) != PB_LIB_ENTRY_SIZE || count > PB_LIB_MAX_ENTRIES) return PB_LIB_ERR_FORMAT;
  if (PB_LIB_HEADER_SIZE + (size_t)count * PB_LIB_ENTRY_SIZE > len) return PB_LIB_ERR_FORMAT;

  const uint8_t *entries = sector + PB_LIB_HEADER_SIZE;
  if (pb_crc32(entries, (size_t)count * PB_LIB_ENTRY_SIZE) != rd32(sector + 12)) return PB_LIB_ERR_CRC;

  for (uint32_t i = 0; i < count; i++) {
    const uint8_t *e = entries + (size_t)i * PB_LIB_ENTRY_SIZE;
    bool ok = entry_ok(e);
    // No two ROMs may share a sector.
    for (uint32_t j = 0; ok && j < i; j++) {
      const uint8_t *o = entries + (size_t)j * PB_LIB_ENTRY_SIZE;
      const uint64_t a0 = rd32(e + 24), a1 = a0 + allocated_bytes(rd32(e + 28));
      const uint64_t b0 = rd32(o + 24), b1 = b0 + allocated_bytes(rd32(o + 28));
      if (a0 < b1 && b0 < a1) ok = false;
    }
    if (!ok) {
      if (bad_entry) *bad_entry = i;
      return PB_LIB_ERR_ENTRY;
    }
  }

  lib->base = entries;
  lib->count = count;
  return PB_LIB_OK;
}

void pb_lib_get(const pb_lib_t *lib, uint32_t i, pb_lib_entry_t *out) {
  const uint8_t *e = lib->base + (size_t)i * PB_LIB_ENTRY_SIZE;
  for (unsigned k = 0; k < PB_LIB_NAME_LEN; k++) out->name[k] = (char)e[k];
  out->flash_offset = rd32(e + 24);
  out->size = rd32(e + 28);
  out->rom_crc32 = rd32(e + 32);
  out->ram_size = rd32(e + 36);
  out->rom_banks = (uint16_t)rd16(e + 40);
  out->flags = (uint16_t)rd16(e + 42);
  out->mapper = e[44];
  out->cart_type = e[45];
}
