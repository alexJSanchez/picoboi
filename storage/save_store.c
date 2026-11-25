#include "save_store.h"

#include <string.h>

#include "crc32.h"

// Claim header (page 0 of the sector at PB_SETTINGS_OFFSET), 16 bytes, little-endian:
//   magic "PBSV", version u16, reserved u16, store_id u32, crc32 of the previous 12 bytes.
// Copy header (page 0 of a copy's first sector), 32 bytes:
//   magic "PBSC", version u16, n_sectors u16, store_id u32, key u32, seq u32, payload_len u32,
//   payload_crc32 u32, crc32 of the previous 28 bytes.
// A copy is its header sector followed by ceil(payload_len / 4096) payload sectors.

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t *p, uint32_t v) {
  wr16(p, v);
  wr16(p + 2, v >> 16);
}

static uint32_t slot_offset(uint32_t sector) { return PB_SAVE_SLOTS_OFFSET + sector * PB_FLASH_SECTOR_SIZE; }

// Sequence numbers wrap around: a is newer than b if the signed difference is positive.
static bool seq_newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

static bool is_blank(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (p[i] != 0xFF) return false;
  return true;
}

// Returns the number of sectors of a valid copy header at `h` (for sector `sec`), or 0.
static uint32_t copy_header_sectors(const pb_save_store_t *s, const uint8_t *h, uint32_t sec) {
  if (memcmp(h, PB_SAVE_COPY_MAGIC, 4) != 0) return 0;
  if (rd16(h + 4) != PB_SAVE_VERSION) return 0;
  if (pb_crc32(h, 28) != rd32(h + 28)) return 0;
  if (rd32(h + 8) != s->store_id) return 0;  // data left by something else cannot match our id
  const uint32_t n = rd16(h + 6);
  const uint32_t len = rd32(h + 20);
  if (n < 2 || sec + n > PB_SAVE_SLOT_SECTORS) return 0;
  if (len == 0 || len > (n - 1) * PB_FLASH_SECTOR_SIZE) return 0;
  return n;
}

static void scan(pb_save_store_t *s) {
  s->n_copies = 0;
  s->truncated = false;
  uint32_t sec = 0;
  while (sec < PB_SAVE_SLOT_SECTORS) {
    const uint8_t *h = s->ops.read(s->ops.ctx, slot_offset(sec));
    const uint32_t n = copy_header_sectors(s, h, sec);
    if (n == 0) {
      sec++;
      continue;
    }
    if (s->n_copies < PB_SAVE_MAX_COPIES) {
      pb_save_copy_t *c = &s->copies[s->n_copies++];
      c->key = rd32(h + 12);
      c->seq = rd32(h + 16);
      c->first_sector = sec;
      c->n_sectors = n;
      c->payload_len = rd32(h + 20);
    } else {
      s->truncated = true;
    }
    sec += n;  // the payload sectors are not scanned for headers
  }
}

static bool ops_ok(const pb_flash_ops_t *ops) {
  return ops && ops->read && ops->erase && ops->program;
}

pb_save_status_t pb_save_open(pb_save_store_t *s, const pb_flash_ops_t *ops) {
  if (!s || !ops_ok(ops)) return PB_SAVE_ERR_ARGS;
  memset(s, 0, sizeof(*s));
  s->ops = *ops;
  const uint8_t *c = ops->read(ops->ctx, PB_SETTINGS_OFFSET);
  if (is_blank(c, PB_FLASH_SECTOR_SIZE)) return PB_SAVE_BLANK;
  if (memcmp(c, PB_SAVE_CLAIM_MAGIC, 4) == 0 && rd16(c + 4) == PB_SAVE_VERSION &&
      pb_crc32(c, 12) == rd32(c + 12)) {
    s->store_id = rd32(c + 8);
    scan(s);
    s->ready = true;
    return PB_SAVE_OK;
  }
  return PB_SAVE_FOREIGN;
}

pb_save_status_t pb_save_format(pb_save_store_t *s, const pb_flash_ops_t *ops, uint32_t store_id) {
  if (!s || !ops_ok(ops)) return PB_SAVE_ERR_ARGS;
  memset(s, 0, sizeof(*s));
  s->ops = *ops;
  if (!pb_flash_erase_allowed(PB_SETTINGS_OFFSET, PB_FLASH_SECTOR_SIZE)) return PB_SAVE_ERR_FLASH;
  if (!ops->erase(ops->ctx, PB_SETTINGS_OFFSET, PB_FLASH_SECTOR_SIZE)) return PB_SAVE_ERR_FLASH;

  memset(s->page, 0xFF, sizeof(s->page));
  memcpy(s->page, PB_SAVE_CLAIM_MAGIC, 4);
  wr16(s->page + 4, PB_SAVE_VERSION);
  wr16(s->page + 6, 0);
  wr32(s->page + 8, store_id);
  wr32(s->page + 12, pb_crc32(s->page, 12));
  if (!pb_flash_program_allowed(PB_SETTINGS_OFFSET, PB_FLASH_PAGE_SIZE)) return PB_SAVE_ERR_FLASH;
  if (!ops->program(ops->ctx, PB_SETTINGS_OFFSET, s->page, PB_FLASH_PAGE_SIZE)) return PB_SAVE_ERR_FLASH;
  // Read it back: a chip that accepts the commands but does not change (write protection, a
  // failed write) must not be reported as success.
  if (memcmp(ops->read(ops->ctx, PB_SETTINGS_OFFSET), s->page, PB_SAVE_CLAIM_SIZE) != 0) return PB_SAVE_ERR_FLASH;

  s->store_id = store_id;
  s->ready = true;
  return PB_SAVE_OK;
}

// Picks the newest copy of `key` that is not yet marked in `used`, or -1.
static int newest_unused(const pb_save_store_t *s, uint32_t key, const bool *used) {
  int best = -1;
  for (uint32_t i = 0; i < s->n_copies; i++) {
    if (s->copies[i].key != key || used[i]) continue;
    if (best < 0 || seq_newer(s->copies[i].seq, s->copies[best].seq)) best = (int)i;
  }
  return best;
}

static bool payload_matches(const pb_save_store_t *s, const pb_save_copy_t *c) {
  const uint8_t *h = s->ops.read(s->ops.ctx, slot_offset(c->first_sector));
  const uint8_t *p = s->ops.read(s->ops.ctx, slot_offset(c->first_sector) + PB_FLASH_SECTOR_SIZE);
  return pb_crc32(p, c->payload_len) == rd32(h + 24);
}

pb_save_status_t pb_save_load(pb_save_store_t *s, uint32_t key, uint8_t *buf, uint32_t cap, uint32_t *len_out) {
  if (!s || !s->ready || !buf || !len_out) return PB_SAVE_ERR_ARGS;
  bool used[PB_SAVE_MAX_COPIES];
  memset(used, 0, sizeof(used));
  for (;;) {
    const int i = newest_unused(s, key, used);
    if (i < 0) return PB_SAVE_NOT_FOUND;
    used[i] = true;
    const pb_save_copy_t *c = &s->copies[i];
    if (c->payload_len > cap) continue;
    if (!payload_matches(s, c)) continue;  // damaged: fall back to the older copy
    memcpy(buf, s->ops.read(s->ops.ctx, slot_offset(c->first_sector) + PB_FLASH_SECTOR_SIZE), c->payload_len);
    *len_out = c->payload_len;
    return PB_SAVE_OK;
  }
}

// First run of `need` sectors that overlaps no known copy.
static bool find_free_run(const pb_save_store_t *s, uint32_t need, uint32_t *start_out) {
  uint32_t start = 0;
  while (start + need <= PB_SAVE_SLOT_SECTORS) {
    bool clash = false;
    uint32_t next = start + 1;
    for (uint32_t i = 0; i < s->n_copies; i++) {
      const pb_save_copy_t *c = &s->copies[i];
      if (start < c->first_sector + c->n_sectors && c->first_sector < start + need) {
        clash = true;
        if (c->first_sector + c->n_sectors > next) next = c->first_sector + c->n_sectors;
      }
    }
    if (!clash) {
      *start_out = start;
      return true;
    }
    start = next;
  }
  return false;
}

static void remove_copy(pb_save_store_t *s, uint32_t i) {
  s->copies[i] = s->copies[s->n_copies - 1];
  s->n_copies--;
}

pb_save_status_t pb_save_write(pb_save_store_t *s, uint32_t key, const uint8_t *data, uint32_t len) {
  if (!s || !s->ready || !data || len == 0 || len > PB_SAVE_MAX_PAYLOAD) return PB_SAVE_ERR_ARGS;
  const uint32_t need = 1 + (len + PB_FLASH_SECTOR_SIZE - 1) / PB_FLASH_SECTOR_SIZE;
  if (need > PB_SAVE_SLOT_SECTORS) return PB_SAVE_ERR_FULL;

  // The newest copy by header decides the next sequence number; the newest copy whose payload is
  // intact must survive this write, so the target is some other copy.
  int newest = -1;
  for (uint32_t i = 0; i < s->n_copies; i++)
    if (s->copies[i].key == key && (newest < 0 || seq_newer(s->copies[i].seq, s->copies[newest].seq))) newest = (int)i;

  int good = -1;
  {
    bool used[PB_SAVE_MAX_COPIES];
    memset(used, 0, sizeof(used));
    for (;;) {
      const int i = newest_unused(s, key, used);
      if (i < 0) break;
      used[i] = true;
      if (payload_matches(s, &s->copies[i])) {
        good = i;
        break;
      }
    }
  }

  // Target: the oldest other copy of this key with the right size; else a free run.
  int target = -1;
  for (uint32_t i = 0; i < s->n_copies; i++) {
    if (s->copies[i].key != key || (int)i == good || s->copies[i].n_sectors != need) continue;
    if (target < 0 || seq_newer(s->copies[target].seq, s->copies[i].seq)) target = (int)i;
  }
  uint32_t first;
  if (target >= 0) {
    first = s->copies[target].first_sector;
  } else if (!find_free_run(s, need, &first)) {
    return PB_SAVE_ERR_FULL;
  }
  const uint32_t seq = newest >= 0 ? s->copies[newest].seq + 1 : 1;
  const uint32_t base = slot_offset(first);

  // From here the target's old contents are gone: forget it before erasing, so the table is
  // right even if a step fails (a failed write is rediscovered correctly by pb_save_open).
  if (target >= 0) remove_copy(s, (uint32_t)target);

  // 1. Erase the whole copy (header sector and payload sectors).
  if (!pb_flash_erase_allowed(base, need * PB_FLASH_SECTOR_SIZE)) return PB_SAVE_ERR_FLASH;
  if (!s->ops.erase(s->ops.ctx, base, need * PB_FLASH_SECTOR_SIZE)) return PB_SAVE_ERR_FLASH;

  // 2. Program the payload: whole pages straight from the caller's RAM, then the last partial page.
  const uint32_t full = len / PB_FLASH_PAGE_SIZE * PB_FLASH_PAGE_SIZE;
  if (full) {
    if (!pb_flash_program_allowed(base + PB_FLASH_SECTOR_SIZE, full)) return PB_SAVE_ERR_FLASH;
    if (!s->ops.program(s->ops.ctx, base + PB_FLASH_SECTOR_SIZE, data, full)) return PB_SAVE_ERR_FLASH;
  }
  if (len > full) {
    memset(s->page, 0xFF, sizeof(s->page));
    memcpy(s->page, data + full, len - full);
    if (!pb_flash_program_allowed(base + PB_FLASH_SECTOR_SIZE + full, PB_FLASH_PAGE_SIZE)) return PB_SAVE_ERR_FLASH;
    if (!s->ops.program(s->ops.ctx, base + PB_FLASH_SECTOR_SIZE + full, s->page, PB_FLASH_PAGE_SIZE)) return PB_SAVE_ERR_FLASH;
  }

  // 3. Program the header LAST: this is the commit. Before it is complete the copy is invalid.
  memset(s->page, 0xFF, sizeof(s->page));
  memcpy(s->page, PB_SAVE_COPY_MAGIC, 4);
  wr16(s->page + 4, PB_SAVE_VERSION);
  wr16(s->page + 6, need);
  wr32(s->page + 8, s->store_id);
  wr32(s->page + 12, key);
  wr32(s->page + 16, seq);
  wr32(s->page + 20, len);
  wr32(s->page + 24, pb_crc32(data, len));
  wr32(s->page + 28, pb_crc32(s->page, 28));
  if (!pb_flash_program_allowed(base, PB_FLASH_PAGE_SIZE)) return PB_SAVE_ERR_FLASH;
  if (!s->ops.program(s->ops.ctx, base, s->page, PB_FLASH_PAGE_SIZE)) return PB_SAVE_ERR_FLASH;

  // 4. Read it all back from flash. Only a copy that reads back exactly as written counts as saved;
  //    otherwise the caller must not tell the player "saved". (A failed copy is simply not
  //    valid, and the previous save is untouched.)
  if (memcmp(s->ops.read(s->ops.ctx, base), s->page, PB_SAVE_HEADER_SIZE) != 0) return PB_SAVE_ERR_FLASH;
  if (memcmp(s->ops.read(s->ops.ctx, base + PB_FLASH_SECTOR_SIZE), data, len) != 0) return PB_SAVE_ERR_FLASH;

  if (s->n_copies < PB_SAVE_MAX_COPIES) {
    pb_save_copy_t *c = &s->copies[s->n_copies++];
    c->key = key;
    c->seq = seq;
    c->first_sector = first;
    c->n_sectors = need;
    c->payload_len = len;
  } else {
    s->truncated = true;
  }
  return PB_SAVE_OK;
}
