#include "pb_pacer.h"

void pb_pacer_init(pb_pacer_t *p) {
  p->last_us = 0;
  p->acc_ns = 0;
  p->started = false;
}

uint32_t pb_pacer_frames_due(pb_pacer_t *p, uint32_t now_us) {
  if (!p->started) {
    p->started = true;
    p->last_us = now_us;
    p->acc_ns = PB_FRAME_PERIOD_NS;  // run the first frame immediately
  } else {
    uint32_t elapsed_us = now_us - p->last_us;  // unsigned: correct across wrap-around
    p->last_us = now_us;
    p->acc_ns += (uint64_t)elapsed_us * 1000u;
  }

  uint64_t n = p->acc_ns / PB_FRAME_PERIOD_NS;
  if (n > PB_PACER_MAX_CATCH_UP) {
    p->acc_ns = 0;
    return PB_PACER_MAX_CATCH_UP;
  }
  p->acc_ns -= n * PB_FRAME_PERIOD_NS;
  return (uint32_t)n;
}
