#include "save_manager.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "picosystem.hpp"

#include "audio.h"
#include "crc32.h"
#include "flash_ops.h"
#include "save_policy.h"
#include "save_store.h"

namespace {

pb_save_store_t g_store;
SaveState g_state = SaveState::Unavailable;
int g_last_error = 0;
pb_savepol_t g_pol;

bool g_in_game = false;
uint32_t g_key = 0;
uint8_t *g_ram = nullptr;
uint32_t g_ram_size = 0;

uint32_t g_blocked_until_ms = 0;  // after a failed save, do not retry for a while
bool g_blocked = false;
// Which 8 KiB RAM banks count when deciding that the game saved (bit n = bank n). A few games use
// a bank as scratch space while playing, so "the RAM changed" is not the same as "the player
// saved". Pokemon Red/Blue/Yellow (and the Japanese Green) decompress sprites into bank 0 and
// keep the real save in banks 1 to 3; measured on a scripted play-through: the intro and play
// change only bank 0, the in-game SAVE changes bank 1 (docs/COMPAT.md). Everything else: all banks.
uint32_t g_bank_mask = 0xFFFF;

uint32_t trigger_banks_for(const char *title) {
  static const char *const scratch_bank0[] = {"POKEMON RED", "POKEMON BLUE", "POKEMON YELLOW", "POKEMON GREEN"};
  for (const char *t : scratch_bank0)
    if (title && strcmp(title, t) == 0) return 0xFFFE;
  return 0xFFFF;
}

uint32_t g_synced_crc = 0;      // CRC of the RAM as last loaded from or written to flash
bool g_synced_valid = false;    // false: nothing stored yet for this game
uint32_t g_overlay_until_ms = 0;
const char *g_overlay_text = nullptr;
bool g_overlay_error = false;

void show(const char *text, bool error, uint32_t now, uint32_t duration_ms) {
  g_overlay_text = text;
  g_overlay_error = error;
  g_overlay_until_ms = now + duration_ms;
}

uint32_t new_store_id() {
  // Any value that differs between claims is enough; mix the clock so it is not a constant.
  return time_us_32() ^ 0x5B1E5AFEu;
}

void flush(const pb_core_t *core) {
  const uint32_t now = picosystem::time();
  const uint32_t gen = pb_core_cart_ram_generation_banks(core, g_bank_mask);

  // The game rewrote its save RAM but the contents are what is already stored (or was loaded):
  // nothing to save, so no flash write and no pause.
  const uint32_t crc = pb_crc32(g_ram, g_ram_size);
  if (g_synced_valid && crc == g_synced_crc) {
    pb_savepol_saved(&g_pol, gen);
    printf("save: RAM unchanged since the last save, nothing written\n");
    return;
  }

  audio_reset();  // flash writes stall the CPU: do not let the speaker loop stale sound
  const uint32_t t0 = time_us_32();
  const pb_save_status_t st = pb_save_write(&g_store, g_key, g_ram, g_ram_size);
  const uint32_t ms = (time_us_32() - t0) / 1000;
  if (st == PB_SAVE_OK) {
    g_synced_crc = crc;
    g_synced_valid = true;
    pb_savepol_saved(&g_pol, gen);
    g_blocked = false;
    show("SAVED", false, now, 1500);
    printf("save: wrote %u bytes for ROM %08x in %u ms\n", (unsigned)g_ram_size, (unsigned)g_key, (unsigned)ms);
  } else {
    g_blocked = true;
    g_blocked_until_ms = now + 15000;
    show(st == PB_SAVE_ERR_FULL ? "SAVE AREA FULL" : "SAVE FAILED", true, now, 4000);
    printf("save: FAILED (status %d) for ROM %08x after %u ms\n", (int)st, (unsigned)g_key, (unsigned)ms);
  }
}

}  // namespace

void saves_init() {
  const pb_flash_ops_t ops = flash_ops_device();
  const pb_save_status_t st = pb_save_open(&g_store, &ops);
  if (st == PB_SAVE_OK) {
    g_state = SaveState::Ready;
    printf("saves: ready, %u saved games found\n", (unsigned)g_store.n_copies);
  } else if (st == PB_SAVE_BLANK) {  // never used: nothing to lose, claim it
    if (pb_save_format(&g_store, &ops, new_store_id()) == PB_SAVE_OK) {
      g_state = SaveState::Ready;
      printf("saves: region was blank, claimed it\n");
    } else {
      g_state = SaveState::Unavailable;
      printf("saves: could not claim the blank region\n");
    }
  } else if (st == PB_SAVE_FOREIGN) {
    g_state = SaveState::NeedsConfirm;
    printf("saves: the save region holds other data, asking the player\n");
  } else {
    g_state = SaveState::Unavailable;
    printf("saves: unavailable (status %d)\n", (int)st);
  }
}

SaveState saves_state() { return g_state; }

bool saves_claim() {
  if (g_state != SaveState::NeedsConfirm) return g_state == SaveState::Ready;
  const pb_flash_ops_t ops = flash_ops_device();
  const pb_save_status_t fst = pb_save_format(&g_store, &ops, new_store_id());
  if (fst == PB_SAVE_OK) {
    g_state = SaveState::Ready;
    printf("saves: claimed the region (the player agreed to erase the old data)\n");
    return true;
  }
  g_last_error = (int)fst;
  g_state = SaveState::Unavailable;
  printf("saves: claiming the region failed\n");
  return false;
}

int saves_last_error() { return g_last_error; }

void saves_decline() {
  if (g_state == SaveState::NeedsConfirm) {
    g_state = SaveState::Declined;
    printf("saves: the player chose to play without saving\n");
  }
}

bool saves_begin_game(uint32_t key, uint8_t *ram, uint32_t ram_size, const char *title) {
  g_bank_mask = trigger_banks_for(title);
  memset(ram, 0, ram_size);
  g_in_game = true;
  g_key = key;
  g_ram = ram;
  g_ram_size = ram_size;
  g_blocked = false;
  g_overlay_text = nullptr;
  pb_savepol_init(&g_pol, 0);  // the core starts counting at 0
  g_synced_valid = false;

  if (g_state != SaveState::Ready || ram_size == 0) {
    if (ram_size != 0) show("SAVING IS OFF", true, picosystem::time(), 3000);
    return false;
  }
  uint32_t len = 0;
  const pb_save_status_t st = pb_save_load(&g_store, key, ram, ram_size, &len);
  if (st == PB_SAVE_OK) {
    printf("saves: loaded %u bytes for ROM %08x\n", (unsigned)len, (unsigned)key);
    g_synced_crc = pb_crc32(ram, ram_size);  // what is stored now: later identical RAM needs no write
    g_synced_valid = true;
    show("SAVE LOADED", false, picosystem::time(), 2500);
    return true;
  }
  printf("saves: no save for ROM %08x yet\n", (unsigned)key);
  show("NO SAVE YET", false, picosystem::time(), 2500);
  return false;
}

bool saves_has_save(uint32_t key) {
  if (g_state != SaveState::Ready) return false;
  for (uint32_t i = 0; i < g_store.n_copies; i++)
    if (g_store.copies[i].key == key) return true;
  return false;
}

uint32_t saves_game_count() {
  if (g_state != SaveState::Ready) return 0;
  uint32_t n = 0;
  for (uint32_t i = 0; i < g_store.n_copies; i++) {
    bool seen = false;
    for (uint32_t j = 0; j < i; j++) seen = seen || g_store.copies[j].key == g_store.copies[i].key;
    if (!seen) n++;
  }
  return n;
}

void saves_tick(const pb_core_t *core) {
  if (!g_in_game || g_state != SaveState::Ready || g_ram_size == 0) return;
  const uint32_t now = picosystem::time();
  if (g_blocked && (int32_t)(now - g_blocked_until_ms) < 0) return;
  if (pb_savepol_update(&g_pol, pb_core_cart_ram_generation_banks(core, g_bank_mask), now)) flush(core);
}

void saves_end_game(const pb_core_t *core) {
  if (g_in_game && g_state == SaveState::Ready && g_ram_size != 0 &&
      pb_savepol_dirty(&g_pol, pb_core_cart_ram_generation_banks(core, g_bank_mask))) {
    flush(core);
  }
  g_in_game = false;
}

const char *saves_overlay(uint32_t now_ms, bool *is_error) {
  if (!g_overlay_text || (int32_t)(now_ms - g_overlay_until_ms) >= 0) return nullptr;
  *is_error = g_overlay_error;
  return g_overlay_text;
}
