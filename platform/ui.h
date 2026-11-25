// platform/ui: the few drawing calls the launcher needs, so that only platform/ touches the
// PicoSystem SDK. Colours are 4 bits per channel (0..15), like the SDK.
#ifndef PB_PLATFORM_UI_H
#define PB_PLATFORM_UI_H

#include <stdint.h>

struct ui_color {
  uint8_t r, g, b;
};

void ui_clear(ui_color c);
void ui_rect(int x, int y, int w, int h, ui_color c);
void ui_text(int x, int y, const char *s, ui_color c);
// Width in pixels of `s` in the default font (for right-aligning and centring).
int ui_text_width(const char *s);

// Buttons pressed since the last frame (edges), for menus.
enum : uint8_t {
  UI_UP = 1,
  UI_DOWN = 2,
  UI_A = 4,
  UI_B = 8,
};
uint8_t ui_buttons_pressed();

#endif
