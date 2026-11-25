// Host tests for storage/save_store against a simulated flash. The simulation is strict: it counts
// a violation whenever the store erases or programs outside the settings + save regions, uses
// unaligned ranges, or programs bytes that were not erased first. Power loss is injected at every
// flash operation of a save, with several amounts of partial damage.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "save_store.h"

static int failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                            \
    }                                                        \
  } while (0)

// ---------------------------------------------------------------------------------------------
// Simulated flash (the first 12 MiB of the chip)
// ---------------------------------------------------------------------------------------------
typedef struct {
  uint8_t *mem;
  int ops_until_fail;  // -1: never; the operation reached when it hits 0 fails
  int progress;        // 0..3: how much of the failing operation completes (quarters)
  uint32_t tear;       // bytes of the torn page that were programmed before the power went
  long ops;            // operations attempted since the last reset of the counter
  bool crashed;        // power is gone: all operations are refused until reboot()
  long violations;
} sim_t;

static uint8_t g_mem[PB_FLASH_USER_END];
static sim_t g_sim = {g_mem, -1, 0, 128, 0, false, 0};

static const uint8_t *sim_read(void *ctx, uint32_t off) { return ((sim_t *)ctx)->mem + off; }

// Returns true if the operation may go ahead; false if the (power-loss) failure triggers now.
static bool step(sim_t *s) {
  s->ops++;
  if (s->crashed) return false;
  if (s->ops_until_fail < 0) return true;
  if (s->ops_until_fail == 0) {
    s->crashed = true;
    return false;
  }
  s->ops_until_fail--;
  return true;
}

static bool in_region(uint32_t off, uint32_t len) {
  return off >= PB_SETTINGS_OFFSET && (uint64_t)off + len <= PB_SAVE_SLOTS_END;
}

static bool sim_erase(void *ctx, uint32_t off, uint32_t len) {
  sim_t *s = (sim_t *)ctx;
  if (off % PB_FLASH_SECTOR_SIZE || len % PB_FLASH_SECTOR_SIZE || len == 0 || !in_region(off, len)) {
    s->violations++;
    return false;
  }
  const bool failing_now = s->crashed ? false : (s->ops_until_fail == 0);
  if (!step(s)) {
    if (failing_now) {  // partial erase: first sectors done, the next one left as garbage
      const uint32_t n = len / PB_FLASH_SECTOR_SIZE;
      const uint32_t done = n * (uint32_t)s->progress / 4;
      memset(s->mem + off, 0xFF, done * PB_FLASH_SECTOR_SIZE);
      if (done < n)
        for (uint32_t i = 0; i < PB_FLASH_SECTOR_SIZE; i++) s->mem[off + done * PB_FLASH_SECTOR_SIZE + i] = (uint8_t)(i * 13 + 5);
    }
    return false;
  }
  memset(s->mem + off, 0xFF, len);
  return true;
}

static bool sim_program(void *ctx, uint32_t off, const uint8_t *data, uint32_t len) {
  sim_t *s = (sim_t *)ctx;
  if (off % PB_FLASH_PAGE_SIZE || len % PB_FLASH_PAGE_SIZE || len == 0 || !in_region(off, len)) {
    s->violations++;
    return false;
  }
  for (uint32_t i = 0; i < len; i++)
    if (s->mem[off + i] != 0xFF) {  // real flash can only clear bits; the store must erase first
      s->violations++;
      break;
    }
  const bool failing_now = s->crashed ? false : (s->ops_until_fail == 0);
  if (!step(s)) {
    if (failing_now) {  // some pages complete, the next one torn in the middle
      const uint32_t pages = len / PB_FLASH_PAGE_SIZE;
      const uint32_t done = pages * (uint32_t)s->progress / 4;
      memcpy(s->mem + off, data, done * PB_FLASH_PAGE_SIZE);
      if (done < pages) memcpy(s->mem + off + done * PB_FLASH_PAGE_SIZE, data + done * PB_FLASH_PAGE_SIZE, s->tear);
    }
    return false;
  }
  for (uint32_t i = 0; i < len; i++) s->mem[off + i] &= data[i];
  return true;
}

static pb_flash_ops_t ops(void) {
  pb_flash_ops_t o = {sim_read, sim_erase, sim_program, &g_sim};
  return o;
}

// Outside our regions the flash holds a recognisable pattern (the firmware, the library, ...)
// that must never change.
static uint8_t g_pattern[PB_SETTINGS_OFFSET];
static bool outside_unchanged(void) { return memcmp(g_mem, g_pattern, PB_SETTINGS_OFFSET) == 0; }

static void sim_reset_blank(void) {
  static bool pattern_ready = false;
  if (!pattern_ready) {
    for (uint32_t i = 0; i < PB_SETTINGS_OFFSET; i++) g_pattern[i] = (uint8_t)(i * 7 + (i >> 8));
    pattern_ready = true;
  }
  memcpy(g_mem, g_pattern, PB_SETTINGS_OFFSET);
  memset(g_mem + PB_SETTINGS_OFFSET, 0xFF, PB_SAVE_SLOTS_END - PB_SETTINGS_OFFSET);
  g_sim.ops_until_fail = -1;
  g_sim.progress = 0;
  g_sim.ops = 0;
  g_sim.crashed = false;
}

static void reboot(void) {
  g_sim.crashed = false;
  g_sim.ops_until_fail = -1;
}

static void fill(uint8_t *b, uint32_t n, uint32_t seed) {
  uint32_t x = seed * 2654435761u + 12345u;
  for (uint32_t i = 0; i < n; i++) {
    x = x * 1664525u + 1013904223u;
    b[i] = (uint8_t)(x >> 24);
  }
}

static pb_save_store_t g_store;

static pb_save_status_t open_store(void) {
  pb_flash_ops_t o = ops();
  return pb_save_open(&g_store, &o);
}
static void format_store(uint32_t id) {
  pb_flash_ops_t o = ops();
  CHECK(pb_save_format(&g_store, &o, id) == PB_SAVE_OK);
}

static uint8_t g_buf[PB_SAVE_MAX_PAYLOAD / 16];  // 64 KiB scratch

// Loads and returns the status; the data (if found) is in g_buf.
static pb_save_status_t load(uint32_t key, uint32_t *len) {
  return pb_save_load(&g_store, key, g_buf, sizeof(g_buf), len);
}

static bool load_equals(uint32_t key, const uint8_t *want, uint32_t n) {
  uint32_t len = 0;
  if (load(key, &len) != PB_SAVE_OK) return false;
  return len == n && memcmp(g_buf, want, n) == 0;
}

static uint32_t count_key(uint32_t key) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < g_store.n_copies; i++) n += g_store.copies[i].key == key;
  return n;
}

// ---------------------------------------------------------------------------------------------
static void test_claim_and_foreign(void) {
  sim_reset_blank();
  CHECK(open_store() == PB_SAVE_BLANK);
  format_store(0x1234);
  CHECK(open_store() == PB_SAVE_OK);
  CHECK(g_store.n_copies == 0 && g_store.store_id == 0x1234);

  // A region with other data: foreign; formatting touches only the claim sector, and copy
  // headers left behind by someone else (valid CRC, other store id) are ignored.
  sim_reset_blank();
  for (uint32_t i = PB_SETTINGS_OFFSET; i < PB_SAVE_SLOTS_END; i++) g_mem[i] = (uint8_t)(i * 31 + 7);
  uint8_t fake[PB_SAVE_HEADER_SIZE];
  memset(fake, 0, sizeof(fake));
  memcpy(fake, PB_SAVE_COPY_MAGIC, 4);
  fake[4] = PB_SAVE_VERSION;
  fake[6] = 2;                         // n_sectors
  fake[8] = 0x99; fake[9] = 0x99;      // some other store id
  fake[20] = 100;                      // payload_len
  uint32_t c = pb_crc32(fake, 28);
  memcpy(fake + 28, &c, 4);            // little-endian host
  memcpy(g_mem + PB_SAVE_SLOTS_OFFSET + 5 * PB_FLASH_SECTOR_SIZE, fake, sizeof(fake));
  static uint8_t before[PB_FLASH_USER_END];
  memcpy(before, g_mem, PB_FLASH_USER_END);
  CHECK(open_store() == PB_SAVE_FOREIGN);
  CHECK(memcmp(before, g_mem, PB_FLASH_USER_END) == 0);  // opening changes nothing
  uint8_t d[10];
  uint32_t n;
  CHECK(pb_save_write(&g_store, 1, d, 10) == PB_SAVE_ERR_ARGS);  // not ready
  CHECK(pb_save_load(&g_store, 1, g_buf, 100, &n) == PB_SAVE_ERR_ARGS);
  format_store(0xBEEF);
  CHECK(open_store() == PB_SAVE_OK && g_store.n_copies == 0);
  // Only the claim sector changed.
  for (uint32_t i = 0; i < PB_FLASH_USER_END; i++) {
    const bool in_claim = i >= PB_SETTINGS_OFFSET && i < PB_SETTINGS_OFFSET + PB_FLASH_SECTOR_SIZE;
    if (!in_claim && before[i] != g_mem[i]) {
      CHECK(!"format changed a byte outside the claim sector");
      break;
    }
  }
  // A claim header with a bad CRC is not ours either.
  sim_reset_blank();
  format_store(7);
  g_mem[PB_SETTINGS_OFFSET + 9] ^= 1;
  CHECK(open_store() == PB_SAVE_FOREIGN);
}

static void test_roundtrip_and_alternation(void) {
  static const uint32_t sizes[] = {1, 100, 255, 256, 257, 512, 4095, 4096, 4097, 8192, 32768};
  sim_reset_blank();
  format_store(1);
  static uint8_t data[32768], more[32768];
  for (unsigned si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
    const uint32_t n = sizes[si], key = 0x1000 + si;
    fill(data, n, si + 1);
    CHECK(pb_save_write(&g_store, key, data, n) == PB_SAVE_OK);
    CHECK(load_equals(key, data, n));
  }
  uint32_t len;
  CHECK(load(0xDEAD, &len) == PB_SAVE_NOT_FOUND);
  // A buffer that is too small cannot take the save: not found, never a partial copy.
  CHECK(pb_save_load(&g_store, 0x1000 + 10, g_buf, 100, &len) == PB_SAVE_NOT_FOUND);

  // Alternation: the two copies of a key are rewritten in turn, the newest always wins.
  const uint32_t key = 0x7777;
  for (uint32_t v = 1; v <= 5; v++) {
    fill(more, 8192, 100 + v);
    CHECK(pb_save_write(&g_store, key, more, 8192) == PB_SAVE_OK);
    CHECK(load_equals(key, more, 8192));
    CHECK(count_key(key) == (v == 1 ? 1u : 2u));
  }
  uint32_t seqs[2], k = 0, firsts[2];
  for (uint32_t i = 0; i < g_store.n_copies; i++)
    if (g_store.copies[i].key == key) { seqs[k] = g_store.copies[i].seq; firsts[k++] = g_store.copies[i].first_sector; }
  CHECK((seqs[0] == 5 && seqs[1] == 4) || (seqs[0] == 4 && seqs[1] == 5));
  CHECK(firsts[0] != firsts[1]);

  // Everything is rediscovered from flash after a reboot (open scans the region again).
  CHECK(open_store() == PB_SAVE_OK);
  CHECK(load_equals(key, more, 8192));
  for (unsigned si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
    fill(data, sizes[si], si + 1);
    CHECK(load_equals(0x1000 + si, data, sizes[si]));
  }
  CHECK(g_sim.violations == 0);
  CHECK(outside_unchanged());
}

// Counts the flash operations of one write, so the power-loss tests can hit every one of them.
static long ops_of_write(uint32_t key, const uint8_t *d, uint32_t n) {
  const long before = g_sim.ops;
  CHECK(pb_save_write(&g_store, key, d, n) == PB_SAVE_OK);
  return g_sim.ops - before;
}

static void power_loss_case(uint32_t size) {
  static uint8_t p1[32768], p2[32768], p3[32768];
  const uint32_t key = 0xABCD0001;
  fill(p1, size, 11);
  fill(p2, size, 22);
  fill(p3, size, 33);

  // How many operations does a write take (for this size)?
  sim_reset_blank();
  format_store(5);
  const long nops = ops_of_write(key, p1, size);
  CHECK(nops >= 3 && nops <= 5);

  for (int stage = 1; stage <= 3; stage++) {
    for (long k = 0; k <= nops; k++) {
      for (int pt = 0; pt < 4 * 4; pt++) {
        const int progress = pt / 4;
        static const uint32_t tears[4] = {1, 16, 31, 200};  // inside the 32-byte header, and beyond it
        g_sim.tear = tears[pt % 4];
        // Build the history: stage 1 = first ever write, 2 = second write, 3 = third write.
        sim_reset_blank();
        format_store(5);
        if (stage >= 2) CHECK(pb_save_write(&g_store, key, p1, size) == PB_SAVE_OK);
        if (stage >= 3) CHECK(pb_save_write(&g_store, key, p2, size) == PB_SAVE_OK);
        const uint8_t *newv = stage == 1 ? p1 : (stage == 2 ? p2 : p3);
        const uint8_t *oldv = stage == 1 ? NULL : (stage == 2 ? p1 : p2);

        g_sim.ops_until_fail = (int)k;  // the k-th operation of this write fails (k == nops: none)
        g_sim.progress = progress;
        g_sim.tear = tears[pt % 4];
        const pb_save_status_t st = pb_save_write(&g_store, key, newv, size);
        const bool completed = st == PB_SAVE_OK;
        CHECK(completed == (k >= nops));

        // Power comes back: reopen from flash only.
        reboot();
        CHECK(open_store() == PB_SAVE_OK);
        uint32_t len = 0;
        const pb_save_status_t ls = load(key, &len);
        if (ls == PB_SAVE_OK) {
          const bool is_new = len == size && memcmp(g_buf, newv, size) == 0;
          const bool is_old = oldv && len == size && memcmp(g_buf, oldv, size) == 0;
          if (!(is_new || is_old)) {
            printf("FAIL: size %u stage %d k %ld progress %d: loaded data is neither the old nor the new save\n",
                   (unsigned)size, stage, k, progress);
            failures++;
          }
          if (completed && !is_new) { printf("FAIL: completed write not visible (stage %d k %ld)\n", stage, k); failures++; }
        } else {
          // Nothing found is only acceptable for the very first write.
          if (stage != 1 || completed) {
            printf("FAIL: save lost (size %u stage %d k %ld progress %d)\n", (unsigned)size, stage, k, progress);
            failures++;
          }
        }
        // The store must recover: the next write works and is then what a load returns.
        CHECK(pb_save_write(&g_store, key, p3, size) == PB_SAVE_OK);
        CHECK(load_equals(key, p3, size));
        CHECK(open_store() == PB_SAVE_OK && load_equals(key, p3, size));
        if (!outside_unchanged()) {
          printf("FAIL: flash outside the save regions was modified (size %u stage %d k %ld)\n", (unsigned)size, stage, k);
          failures++;
          memcpy(g_mem, g_pattern, PB_SETTINGS_OFFSET);
        }
        if (g_sim.violations) {
          printf("FAIL: flash rule violated (size %u stage %d k %ld progress %d)\n", (unsigned)size, stage, k, progress);
          failures++;
          g_sim.violations = 0;
        }
      }
    }
  }
}

static void test_power_loss(void) {
  power_loss_case(512);
  power_loss_case(4096);
  power_loss_case(8192);
  power_loss_case(32768);
  power_loss_case(300);  // not a multiple of the page size

  // Power loss while claiming the region: afterwards it is blank or foreign, never a half-valid
  // claim that would be trusted; and a second format works.
  for (int progress = 0; progress < 4; progress++)
    for (int k = 0; k <= 2; k++) {  // format is two operations: erase, program; k == 2: no failure
      sim_reset_blank();
      g_sim.ops_until_fail = k;
      g_sim.progress = progress;
      pb_flash_ops_t o = ops();
      pb_save_status_t st = pb_save_format(&g_store, &o, 9);
      CHECK((st == PB_SAVE_OK) == (k == 2));
      reboot();
      const pb_save_status_t os = open_store();
      CHECK(os == PB_SAVE_BLANK || os == PB_SAVE_FOREIGN || os == PB_SAVE_OK);
      if (os != PB_SAVE_OK) {
        CHECK(pb_save_format(&g_store, &o, 10) == PB_SAVE_OK);
        CHECK(open_store() == PB_SAVE_OK);
      }
    }
}

static void test_corruption(void) {
  static uint8_t p1[8192], p2[8192], p3[8192];
  const uint32_t key = 0x55;
  fill(p1, 8192, 1);
  fill(p2, 8192, 2);
  fill(p3, 8192, 3);
  sim_reset_blank();
  format_store(3);
  CHECK(pb_save_write(&g_store, key, p1, 8192) == PB_SAVE_OK);
  CHECK(pb_save_write(&g_store, key, p2, 8192) == PB_SAVE_OK);

  uint32_t newest = 0;
  for (uint32_t i = 0; i < g_store.n_copies; i++)
    if (g_store.copies[i].seq == 2) newest = g_store.copies[i].first_sector;
  const uint32_t newest_off = PB_SAVE_SLOTS_OFFSET + newest * PB_FLASH_SECTOR_SIZE;

  // A damaged payload in the newest copy: fall back to the older save.
  g_mem[newest_off + PB_FLASH_SECTOR_SIZE + 100] ^= 0x10;
  CHECK(load_equals(key, p1, 8192));
  // A write now replaces the damaged copy (never the good older one) and wins.
  CHECK(pb_save_write(&g_store, key, p3, 8192) == PB_SAVE_OK);
  CHECK(load_equals(key, p3, 8192));
  CHECK(open_store() == PB_SAVE_OK && load_equals(key, p3, 8192));

  // Both copies damaged: nothing to load, and a write still works.
  sim_reset_blank();
  format_store(3);
  CHECK(pb_save_write(&g_store, key, p1, 8192) == PB_SAVE_OK);
  CHECK(pb_save_write(&g_store, key, p2, 8192) == PB_SAVE_OK);
  for (uint32_t i = 0; i < g_store.n_copies; i++)
    g_mem[PB_SAVE_SLOTS_OFFSET + g_store.copies[i].first_sector * PB_FLASH_SECTOR_SIZE + PB_FLASH_SECTOR_SIZE + 7] ^= 1;
  uint32_t len;
  CHECK(load(key, &len) == PB_SAVE_NOT_FOUND);
  CHECK(pb_save_write(&g_store, key, p3, 8192) == PB_SAVE_OK);
  CHECK(load_equals(key, p3, 8192));

  // A damaged header (bad CRC) hides that copy at the next open.
  sim_reset_blank();
  format_store(3);
  CHECK(pb_save_write(&g_store, key, p1, 8192) == PB_SAVE_OK);
  CHECK(pb_save_write(&g_store, key, p2, 8192) == PB_SAVE_OK);
  uint32_t n_off = 0, o_off = 0;
  for (uint32_t i = 0; i < g_store.n_copies; i++) {
    const uint32_t off = PB_SAVE_SLOTS_OFFSET + g_store.copies[i].first_sector * PB_FLASH_SECTOR_SIZE;
    if (g_store.copies[i].seq == 2) n_off = off; else o_off = off;
  }
  g_mem[n_off + 16] ^= 0x80;  // seq field of the newest header
  CHECK(open_store() == PB_SAVE_OK);
  CHECK(count_key(key) == 1 && load_equals(key, p1, 8192));
  g_mem[o_off + 12] ^= 0x01;  // key field of the older header
  CHECK(open_store() == PB_SAVE_OK);
  CHECK(count_key(key) == 0 && load(key, &len) == PB_SAVE_NOT_FOUND);
}

// Builds a valid copy directly in the simulated flash (for crafted situations).
static void craft_copy(uint32_t store_id, uint32_t key, uint32_t seq, uint32_t first_sector,
                       const uint8_t *payload, uint32_t len) {
  const uint32_t n = 1 + (len + PB_FLASH_SECTOR_SIZE - 1) / PB_FLASH_SECTOR_SIZE;
  const uint32_t base = PB_SAVE_SLOTS_OFFSET + first_sector * PB_FLASH_SECTOR_SIZE;
  memset(g_mem + base, 0xFF, n * PB_FLASH_SECTOR_SIZE);
  memcpy(g_mem + base + PB_FLASH_SECTOR_SIZE, payload, len);
  uint8_t *h = g_mem + base;
  memcpy(h, PB_SAVE_COPY_MAGIC, 4);
  h[4] = PB_SAVE_VERSION; h[5] = 0;
  h[6] = (uint8_t)n; h[7] = (uint8_t)(n >> 8);
  memcpy(h + 8, &store_id, 4);
  memcpy(h + 12, &key, 4);
  memcpy(h + 16, &seq, 4);
  memcpy(h + 20, &len, 4);
  const uint32_t pc = pb_crc32(payload, len);
  memcpy(h + 24, &pc, 4);
  const uint32_t hc = pb_crc32(h, 28);
  memcpy(h + 28, &hc, 4);
}

static void test_seq_wraparound(void) {
  static uint8_t a[4096], b[4096], c[4096];
  fill(a, 4096, 1);
  fill(b, 4096, 2);
  fill(c, 4096, 3);
  sim_reset_blank();
  format_store(0x42);
  craft_copy(0x42, 0x99, 0xFFFFFFFEu, 10, a, 4096);
  craft_copy(0x42, 0x99, 0xFFFFFFFFu, 20, b, 4096);
  CHECK(open_store() == PB_SAVE_OK);
  CHECK(load_equals(0x99, b, 4096));  // 0xFFFFFFFF is newer than 0xFFFFFFFE
  CHECK(pb_save_write(&g_store, 0x99, c, 4096) == PB_SAVE_OK);  // seq wraps to 0, replaces 0xFFFFFFFE
  CHECK(load_equals(0x99, c, 4096));
  uint32_t seen_zero = 0, seen_ffff = 0;
  for (uint32_t i = 0; i < g_store.n_copies; i++) {
    seen_zero += g_store.copies[i].seq == 0;
    seen_ffff += g_store.copies[i].seq == 0xFFFFFFFFu;
  }
  CHECK(seen_zero == 1 && seen_ffff == 1);
  CHECK(open_store() == PB_SAVE_OK && load_equals(0x99, c, 4096));
  CHECK(outside_unchanged());
}

static void test_full_region(void) {
  static uint8_t data[32768];
  sim_reset_blank();
  format_store(8);
  uint32_t stored = 0;
  uint32_t key = 1;
  for (;; key++) {
    fill(data, 32768, key);
    const pb_save_status_t st = pb_save_write(&g_store, key, data, 32768);
    if (st == PB_SAVE_ERR_FULL) break;
    CHECK(st == PB_SAVE_OK);
    if (st != PB_SAVE_OK || key > 200) break;
    stored++;
  }
  CHECK(stored >= 50 && stored <= 56);  // 9 sectors per copy: at most 510 / 9 = 56
  // A failed write damaged nothing: every earlier save is intact, also after a reboot.
  CHECK(open_store() == PB_SAVE_OK);
  for (uint32_t k = 1; k <= stored; k++) {
    fill(data, 32768, k);
    CHECK(load_equals(k, data, 32768));
  }
  // An existing game can no longer get its second copy (no room) but keeps its first.
  fill(data, 32768, 999);
  CHECK(pb_save_write(&g_store, 1, data, 32768) == PB_SAVE_ERR_FULL);
  fill(data, 32768, 1);
  CHECK(load_equals(1, data, 32768));
  CHECK(g_sim.violations == 0);
  CHECK(outside_unchanged());
}

int main(void) {
  test_claim_and_foreign();
  test_roundtrip_and_alternation();
  test_power_loss();
  test_corruption();
  test_seq_wraparound();
  test_full_region();

  CHECK(outside_unchanged());  // after the last test (the others check as they go)
  CHECK(g_sim.violations == 0);

  if (failures == 0) printf("save store tests passed\n");
  return failures ? 1 : 0;
}
