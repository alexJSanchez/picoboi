// platform/display: draws the 160x144 Game Boy screen centered on the 240x240 panel.
// Only this layer touches the PicoSystem SDK (see CLAUDE.md layering rules).
#ifndef PB_PLATFORM_DISPLAY_H
#define PB_PLATFORM_DISPLAY_H

#include <stdint.h>

// Builds the DMG palette and clears the whole screen (border) to black.
void display_init();

// pb_core draw_line callback: stores one scanline in a private 160x144 off-screen buffer
// (one byte per pixel, shade 0..3). It never touches the SDK framebuffer, so it is safe
// to call at any time, including while the SDK is sending the previous frame to the LCD.
void display_draw_line(void *user, const uint8_t px[160], uint8_t line);

// Copies the off-screen buffer into the SDK framebuffer through the palette. Call from
// the SDK's draw(), which only runs once the previous framebuffer transfer is complete.
void display_present();

#endif
