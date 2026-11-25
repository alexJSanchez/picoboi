#include "ui.h"

#include "picosystem.hpp"

using namespace picosystem;

void ui_clear(ui_color c) {
  target(SCREEN);
  pen(c.r, c.g, c.b);
  clear();
}

void ui_rect(int x, int y, int w, int h, ui_color c) {
  pen(c.r, c.g, c.b);
  frect(x, y, w, h);
}

void ui_text(int x, int y, const char *s, ui_color c) {
  pen(c.r, c.g, c.b);
  text(s, x, y);
}

int ui_text_width(const char *s) {
  int32_t w = 0, h = 0;
  measure(s, w, h);
  return (int)w;
}

uint8_t ui_buttons_down() {
  uint8_t m = 0;
  if (button(UP)) m |= UI_UP;
  if (button(DOWN)) m |= UI_DOWN;
  if (button(A)) m |= UI_A;
  if (button(B)) m |= UI_B;
  if (button(X)) m |= UI_X;
  return m;
}

uint32_t ui_millis() { return time(); }

uint8_t ui_buttons_pressed() {
  uint8_t m = 0;
  if (pressed(UP)) m |= UI_UP;
  if (pressed(DOWN)) m |= UI_DOWN;
  if (pressed(A)) m |= UI_A;
  if (pressed(B)) m |= UI_B;
  return m;
}
