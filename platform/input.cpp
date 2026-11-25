#include "input.h"

#include "pb_core.h"
#include "picosystem.hpp"

using namespace picosystem;

namespace {

constexpr uint32_t HOLD_MS = 1000;

struct Hold {
  bool holding = false;
  bool fired = false;
  uint32_t start_ms = 0;
};

// Returns true once when `down` has been true for HOLD_MS; re-arms when released.
bool update_hold(Hold &h, bool down) {
  if (!down) {
    h.holding = false;
    h.fired = false;
    return false;
  }
  const uint32_t now = time();
  if (!h.holding) {
    h.holding = true;
    h.start_ms = now;
    return false;
  }
  if (!h.fired && now - h.start_ms >= HOLD_MS) {
    h.fired = true;
    return true;
  }
  return false;
}

Hold g_menu, g_mute;

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

bool input_menu_requested() { return update_hold(g_menu, button(X) && button(Y) && !button(B)); }

bool input_mute_toggle_requested() {
  return update_hold(g_mute, button(X) && button(Y) && button(B));
}
