// platform/input: maps PicoSystem buttons to Game Boy buttons (PLAN.md section 7).
//   A -> A, B -> B, D-pad -> D-pad, X -> Select, Y -> Start.
//   Hold X+Y for about 1 second to request the menu (one-shot event).
#ifndef PB_PLATFORM_INPUT_H
#define PB_PLATFORM_INPUT_H

#include <stdbool.h>
#include <stdint.h>

// Returns the current Game Boy button mask (PB_BTN_* bits, 1 = pressed).
uint8_t input_poll();

// True exactly once each time X+Y has been held for the menu hold time.
// Call once per frame, after input_poll().
bool input_menu_requested();

#endif
