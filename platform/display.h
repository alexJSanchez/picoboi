// platform/display: draws the 160x144 Game Boy screen centered on the 240x240 panel.
// Only this layer touches the PicoSystem SDK (see CLAUDE.md layering rules).
#ifndef PB_PLATFORM_DISPLAY_H
#define PB_PLATFORM_DISPLAY_H

#include <stdint.h>

// Builds the DMG palette and clears the whole screen (border) to black.
void display_init();

// pb_core draw_line callback: writes one scanline straight into the framebuffer.
// Must only be called while the SDK is not transferring the framebuffer to the LCD
// (i.e. from inside the SDK's draw()).
void display_draw_line(void *user, const uint8_t px[160], uint8_t line);

#endif
