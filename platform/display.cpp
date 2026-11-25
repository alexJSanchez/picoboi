#include "display.h"

#include <string.h>

#include "picosystem.hpp"

using namespace picosystem;

namespace {

constexpr int GB_W = 160;
constexpr int GB_H = 144;

color_t g_pal[4];
uint32_t g_pal2[4][4];  // g_pal2[a][b] = two pixels packed for a 32-bit write (a first)
uint8_t g_shade[GB_H * GB_W];  // off-screen Game Boy frame, one shade (0..3) per byte
display_scale_t g_scale = DISPLAY_SCALE_FIT;

void clear_screen() {
  target(SCREEN);
  pen(0, 0, 0);
  clear();
}

// 1:1, centered (PLAN.md section 7): (240-160)/2 = 40, (240-144)/2 = 48.
void present_1x() {
  constexpr int OX = 40, OY = 48;
  for (int y = 0; y < GB_H; y++) {
    const uint8_t *src = g_shade + y * GB_W;
    color_t *dst = SCREEN->data + (OY + y) * SCREEN_WIDTH + OX;
    for (int x = 0; x < GB_W; x++) dst[x] = g_pal[src[x]];
  }
}

// 1.5x nearest-neighbour: 160x144 -> 240x216, centered vertically (12 px bars).
// Horizontally every 2 source pixels (s0, s1) become 3 (s0, s0, s1); vertically every
// 2 source rows (a, b) become 3 (a, a, b), the second copy of a is a memcpy.
void present_fit() {
  constexpr int OY = (240 - 216) / 2;
  for (int y = 0; y < GB_H; y += 2) {
    color_t *d0 = SCREEN->data + (OY + (y / 2) * 3) * SCREEN_WIDTH;
    const uint8_t *a = g_shade + y * GB_W;
    const uint8_t *b = a + GB_W;

    uint32_t *w = (uint32_t *)d0;  // rows start 4-byte aligned (240 px * 2 B)
    for (int x = 0; x < GB_W; x += 4) {
      w[0] = g_pal2[a[x]][a[x]];
      w[1] = g_pal2[a[x + 1]][a[x + 2]];
      w[2] = g_pal2[a[x + 2]][a[x + 3]];
      w += 3;
    }
    memcpy(d0 + SCREEN_WIDTH, d0, SCREEN_WIDTH * sizeof(color_t));

    w = (uint32_t *)(d0 + 2 * SCREEN_WIDTH);
    for (int x = 0; x < GB_W; x += 4) {
      w[0] = g_pal2[b[x]][b[x]];
      w[1] = g_pal2[b[x + 1]][b[x + 2]];
      w[2] = g_pal2[b[x + 2]][b[x + 3]];
      w += 3;
    }
  }
}

}  // namespace

void display_init() {
  // Classic DMG green, shade 0 (lightest) to 3 (darkest), in 4 bits per channel.
  g_pal[0] = rgb(13, 15, 12);
  g_pal[1] = rgb(8, 11, 7);
  g_pal[2] = rgb(3, 6, 5);
  g_pal[3] = rgb(0, 1, 2);
  for (int a = 0; a < 4; a++)
    for (int b = 0; b < 4; b++) g_pal2[a][b] = (uint32_t)g_pal[a] | ((uint32_t)g_pal[b] << 16);

  clear_screen();
}

void display_clear() { clear_screen(); }

void display_set_scale(display_scale_t scale) {
  if (scale == g_scale) return;
  g_scale = scale;
  clear_screen();  // remove the old picture's edges from the borders
}

void display_draw_line(void *, const uint8_t px[160], uint8_t line) {
  if (line >= GB_H) return;
  // Keep only the shade bits (0..3) of each pixel. The destination row start is 4-byte
  // aligned (160 is a multiple of 4), so when the emulator's array is aligned too we do four
  // pixels per 32-bit operation; the Cortex-M0+ faults on unaligned word access, so
  // otherwise fall back to bytes.
  if ((((uintptr_t)px) & 3u) == 0) {
    uint32_t *dst = (uint32_t *)(g_shade + line * GB_W);
    const uint32_t *src = (const uint32_t *)px;
    for (int i = 0; i < GB_W / 4; i++) dst[i] = src[i] & 0x03030303u;
  } else {
    uint8_t *dst = g_shade + line * GB_W;
    for (int x = 0; x < GB_W; x++) dst[x] = px[x] & 3;
  }
}

void display_present() {
  if (g_scale == DISPLAY_SCALE_FIT)
    present_fit();
  else
    present_1x();
}
