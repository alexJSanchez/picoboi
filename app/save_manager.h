// app/save_manager: ties the save store, the save policy and the running game together.
//
//   boot        saves_init()            opens the store (a blank region is claimed silently; a
//                                       region holding other data waits for the player's answer)
//   prompt      saves_claim() / saves_decline()
//   game start  saves_begin_game()      loads the game's save into its cart RAM
//   each frame  saves_tick()            saves after the game has been quiet for 2 s
//   game end    saves_end_game()        saves at once if anything is unsaved
//
// While a save is written (about half a second for a 32 KiB save) emulation pauses and the
// speaker is silenced.
#ifndef PB_APP_SAVE_MANAGER_H
#define PB_APP_SAVE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "pb_core.h"

enum class SaveState {
  Unavailable,   // no usable save region (error), or never opened
  NeedsConfirm,  // the region holds other data: ask the player before erasing it
  Declined,      // the player chose to play without saves (until the next boot)
  Ready,         // saves work
};

void saves_init();
SaveState saves_state();

// After the player agreed: erase the claim sector and start using the region.
bool saves_claim();
// Status of the last failed attempt to claim or format (a pb_save_status_t), for on-screen messages.
int saves_last_error();
void saves_decline();

// Fills `ram` (ram_size bytes) with the game's saved data, or zeros if there is none. Call before
// the core is initialised. Returns true if a save was loaded.
bool saves_begin_game(uint32_t key, uint8_t *ram, uint32_t ram_size);

// For the game list: is there a stored save for this ROM, and for how many games in total.
bool saves_has_save(uint32_t key);
uint32_t saves_game_count();

void saves_tick(const pb_core_t *core);
void saves_end_game(const pb_core_t *core);

// A short message about the last save to show for a moment ("SAVED", or a problem), or nullptr.
const char *saves_overlay(uint32_t now_ms, bool *is_error);

#endif
