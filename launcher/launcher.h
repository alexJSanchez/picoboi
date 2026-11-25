// launcher: the game list shown at boot. Reads the library index that tools/pack_library.py
// put into flash, lets the player pick a game with the D-pad, and reports the choice.
// Drawing goes through platform/ui; the list logic is in launcher_model (host-tested).
#ifndef PB_LAUNCHER_H
#define PB_LAUNCHER_H

#include <stdbool.h>
#include <stdint.h>

#include "rom_library.h"

// Reads and validates the library index from flash. Call once at boot (and again after a
// game, to reset the selection if you want).
void launcher_init(uint32_t max_cart_ram);

// Handles one frame of input. Returns true (and fills `out`) when the player started a game
// that can run.
bool launcher_update(pb_lib_entry_t *out);

// Draws the list (or a message if there is no usable library).
void launcher_draw();

// Shows a one-line problem under the list (for example when the emulator refused a ROM).
void launcher_set_message(const char *text);

#endif
