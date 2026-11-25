#include "display.h"

#include "picosystem.hpp"

using namespace picosystem;

namespace {
// 1:1, centered (PLAN.md section 7): (240-160)/2 = 40, (240-144)/2 = 48.
constexpr int OX = 40;
constexpr int OY = 48;
color_t g_pal[4];
}  // namespace

void display_init() {
  // Classic DMG green, shade 0 (lightest) to 3 (darkest), in 4 bits per channel.
  g_pal[0] = rgb(13, 15, 12);
  g_pal[1] = rgb(8, 11, 7);
  g_pal[2] = rgb(3, 6, 5);
  g_pal[3] = rgb(0, 1, 2);

  target(SCREEN);
  pen(0, 0, 0);
  clear();
}

void display_draw_line(void *, const uint8_t px[160], uint8_t line) {
  if (line >= 144) return;
  color_t *dst = SCREEN->data + (OY + line) * SCREEN_WIDTH + OX;
  for (int x = 0; x < 160; x++) dst[x] = g_pal[px[x] & 3];
}
