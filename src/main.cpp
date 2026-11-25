// Phase 0, backlog item 2: hello-world firmware.
// Splash, then a live display of all 8 buttons. Button changes are also logged
// over USB serial (stdio_usb) if a host is connected.
#include <stdio.h>

#include "pico/stdlib.h"
#include "picosystem.hpp"

using namespace picosystem;

namespace {

constexpr uint32_t SPLASH_TICKS = 100;  // update() runs at 40 Hz => ~2.5 s

struct Btn {
  uint32_t pin;
  const char *name;
  int16_t x, y;  // top-left of the on-screen indicator
};

// D-pad cross on the left, A/B/X/Y diamond on the right.
constexpr int S = 32;  // indicator size
const Btn BTNS[] = {
    {UP, "UP", 48, 80},     {DOWN, "DOWN", 48, 144},
    {LEFT, "LEFT", 16, 112}, {RIGHT, "RIGHT", 80, 112},
    {X, "X", 160, 80},      {A, "A", 160, 144},
    {Y, "Y", 128, 112},     {B, "B", 192, 112},
};
constexpr size_t NBTN = sizeof(BTNS) / sizeof(BTNS[0]);

bool g_down[NBTN] = {};
uint32_t g_frame = 0;

}  // namespace

void init() {
  stdio_init_all();  // USB serial; does not block waiting for a host
  printf("PicoBoy hello-world (Phase 0)\n");
}

void update(uint32_t tick) {
  g_frame = tick;
  for (size_t i = 0; i < NBTN; i++) {
    bool d = button(BTNS[i].pin);
    if (d != g_down[i]) {
      g_down[i] = d;
      printf("%s %s\n", BTNS[i].name, d ? "down" : "up");
    }
  }
}

void draw(uint32_t tick) {
  (void)tick;
  pen(1, 1, 3);
  clear();

  if (g_frame < SPLASH_TICKS) {
    pen(15, 15, 15);
    text("PicoBoy", 84, 100);
    pen(8, 8, 10);
    text("Game Boy emulator", 52, 116);
    text("hello world", 76, 132);
    return;
  }

  pen(15, 15, 15);
  text("PicoBoy: button test", 36, 16);

  for (size_t i = 0; i < NBTN; i++) {
    if (g_down[i]) {
      pen(4, 15, 6);  // held: green
    } else {
      pen(4, 4, 6);  // idle: dim
    }
    frect(BTNS[i].x, BTNS[i].y, S, S);
    pen(15, 15, 15);
    text(BTNS[i].name, BTNS[i].x + 2, BTNS[i].y + S + 2);
  }

  pen(8, 8, 10);
  text("battery:", 16, 216);
  text(str((uint32_t)battery()), 80, 216);
}
