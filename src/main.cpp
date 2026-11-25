// Phase 0, backlog item 1: build-only scaffold. Hello-world firmware is item 2.
#include "picosystem.hpp"

using namespace picosystem;

void init() {}

void update(uint32_t tick) { (void)tick; }

void draw(uint32_t tick) {
  (void)tick;
  pen(0, 0, 0);
  clear();
}
