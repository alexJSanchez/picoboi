#include "save_manager.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "picosystem.hpp"

#include "audio.h"
#include "flash_ops.h"
#include "save_policy.h"
#include "save_store.h"

namespace {

pb_save_store_t g_store;
SaveState g_state = SaveState::Unavailable;
pb_savepol_t g_pol;

bool g_in_game = false;
uint32_t g_key = 0;
uint8_t *g_ram = nullptr;
uint32_t g_ram_size = 0;

uint32_t g_blocked_until_ms = 0;  // after a failed save, do not retry for a while
bool g_blocked = false;
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
  const uint32_t gen = pb_core_cart_ram_generation(core);
  audio_reset();  // flash writes stall the CPU: do not let the speaker loop stale sound
  const uint32_t t0 = time_us_32();
  const pb_save_status_t st = pb_save_write(&g_store, g_key, g_ram, g_ram_size);
  const uint32_t ms = (time_us_32() - t0) / 1000;
  if (st == PB_SAVE_OK) {
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
  if (pb_save_format(&g_store, &ops, new_store_id()) == PB_SAVE_OK) {
    g_state = SaveState::Ready;
    printf("saves: claimed the region (the player agreed to erase the old data)\n");
    return true;
  }
  g_state = SaveState::Unavailable;
  printf("saves: claiming the region failed\n");
  return false;
}

void saves_decline() {
  if (g_state == SaveState::NeedsConfirm) {
    g_state = SaveState::Declined;
    printf("saves: the player chose to play without saving\n");
  }
}

bool saves_begin_game(uint32_t key, uint8_t *ram, uint32_t ram_size) {
  memset(ram, 0, ram_size);
  g_in_game = true;
  g_key = key;
  g_ram = ram;
  g_ram_size = ram_size;
  g_blocked = false;
  g_overlay_text = nullptr;
  pb_savepol_init(&g_pol, 0);  // the core starts counting at 0

  if (g_state != SaveState::Ready || ram_size == 0) return false;
  uint32_t len = 0;
  const pb_save_status_t st = pb_save_load(&g_store, key, ram, ram_size, &len);
  if (st == PB_SAVE_OK) {
    printf("saves: loaded %u bytes for ROM %08x\n", (unsigned)len, (unsigned)key);
    return true;
  }
  printf("saves: no save for ROM %08x yet\n", (unsigned)key);
  return false;
}

void saves_tick(const pb_core_t *core) {
  if (!g_in_game || g_state != SaveState::Ready || g_ram_size == 0) return;
  const uint32_t now = picosystem::time();
  if (g_blocked && (int32_t)(now - g_blocked_until_ms) < 0) return;
  if (pb_savepol_update(&g_pol, pb_core_cart_ram_generation(core), now)) flush(core);
}

void saves_end_game(const pb_core_t *core) {
  if (g_in_game && g_state == SaveState::Ready && g_ram_size != 0 &&
      pb_savepol_dirty(&g_pol, pb_core_cart_ram_generation(core))) {
    flush(core);
  }
  g_in_game = false;
}

const char *saves_overlay(uint32_t now_ms, bool *is_error) {
  if (!g_overlay_text || (int32_t)(now_ms - g_overlay_until_ms) >= 0) return nullptr;
  *is_error = g_overlay_error;
  return g_overlay_text;
}
