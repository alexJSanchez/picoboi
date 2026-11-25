// app/save_manager: ties the save store, the save policy and the running game together.
//
// Saving is ON by default. With -DPB_SAVES=OFF this header provides do-nothing versions of every
// function below, nothing about saving is compiled into the firmware, and the firmware has no
// code that can write to flash. What the functions do when saving is on:
//
//   boot        saves_init()            opens the store (a blank region is claimed silently; a
//                                       region holding other data waits for the player's answer)
//   prompt      saves_claim() / saves_decline()
//   game start  saves_begin_game()      loads the game's save into its cart RAM
//   each frame  saves_tick()            saves after the game has been quiet for 2 s
//   game end    saves_end_game()        saves at once if anything is unsaved
//   game list   saves_delete()          deletes one game's save (the player confirmed)
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

#ifdef PB_SAVES

void saves_init();
SaveState saves_state();

// After the player agreed: erase the claim sector and start using the region.
bool saves_claim();
// Status of the last failed attempt to claim or format (a pb_save_status_t), for on-screen messages.
int saves_last_error();
void saves_decline();

// Fills `ram` (ram_size bytes) with the game's saved data, or zeros if there is none. Call before
// the core is initialised. Returns true if a save was loaded.
// `title` is the game's name from its ROM header: it picks which RAM banks count as "the game saved".
bool saves_begin_game(uint32_t key, uint8_t *ram, uint32_t ram_size, const char *title);

// For the game list: is there a stored save for this ROM, and for how many games in total.
bool saves_has_save(uint32_t key);
uint32_t saves_game_count();

// Deletes the stored save for this ROM (both copies). Only from the game list, never while a game
// is running. Returns true if a save was deleted; false if there was none, saving is not ready, or
// the flash refused (then saves_last_error() holds the status). Takes a moment: flash erase.
bool saves_delete(uint32_t key);

void saves_tick(const pb_core_t *core);
void saves_end_game(const pb_core_t *core);

// A short message about the last save to show for a moment ("SAVED", or a problem), or nullptr.
const char *saves_overlay(uint32_t now_ms, bool *is_error);

#else  // saving compiled out: nothing here can touch flash

inline void saves_init() {}
inline SaveState saves_state() { return SaveState::Unavailable; }
inline bool saves_claim() { return false; }
inline int saves_last_error() { return 0; }
inline void saves_decline() {}
inline bool saves_begin_game(uint32_t, uint8_t *, uint32_t, const char *) { return false; }
inline bool saves_has_save(uint32_t) { return false; }
inline uint32_t saves_game_count() { return 0; }
inline bool saves_delete(uint32_t) { return false; }
inline void saves_tick(const pb_core_t *) {}
inline void saves_end_game(const pb_core_t *) {}
inline const char *saves_overlay(uint32_t, bool *) { return nullptr; }

#endif  // PB_SAVES

#endif
