#include "input.h"

#include "pb_core.h"
#include "picosystem.hpp"

using namespace picosystem;

namespace {
constexpr uint32_t MENU_HOLD_MS = 1000;
uint32_t g_hold_start_ms = 0;
bool g_holding = false;
bool g_fired = false;
}  // namespace

uint8_t input_poll() {
  uint8_t m = 0;
  if (button(A)) m |= PB_BTN_A;
  if (button(B)) m |= PB_BTN_B;
  if (button(X)) m |= PB_BTN_SELECT;
  if (button(Y)) m |= PB_BTN_START;
  if (button(UP)) m |= PB_BTN_UP;
  if (button(DOWN)) m |= PB_BTN_DOWN;
  if (button(LEFT)) m |= PB_BTN_LEFT;
  if (button(RIGHT)) m |= PB_BTN_RIGHT;
  return m;
}

bool input_menu_requested() {
  bool both = button(X) && button(Y);
  if (!both) {
    g_holding = false;
    g_fired = false;
    return false;
  }
  uint32_t now = time();
  if (!g_holding) {
    g_holding = true;
    g_hold_start_ms = now;
    return false;
  }
  if (!g_fired && now - g_hold_start_ms >= MENU_HOLD_MS) {
    g_fired = true;
    return true;
  }
  return false;
}
