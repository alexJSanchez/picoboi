// platform/input: maps PicoSystem buttons to Game Boy buttons (PLAN.md section 7).
//   A -> A, B -> B, D-pad -> D-pad, X -> Select, Y -> Start.
//   Hold X+Y for about 1 second to request the menu (one-shot event).
//   Hold X+Y+B for about 1 second to toggle audio mute (temporary, until the Phase 6 menu).
#ifndef PB_PLATFORM_INPUT_H
#define PB_PLATFORM_INPUT_H

#include <stdbool.h>
#include <stdint.h>

// Returns the current Game Boy button mask (PB_BTN_* bits, 1 = pressed).
uint8_t input_poll();

// True exactly once each time X+Y (without B) has been held for the hold time.
// Call once per frame.
bool input_menu_requested();

// True exactly once each time X+Y+B has been held for the hold time. Call once per frame.
bool input_mute_toggle_requested();

#endif
