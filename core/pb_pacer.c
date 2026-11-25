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

pb_plan_t pb_pacer_plan(pb_pacer_t *p, uint32_t now_us, uint32_t cost_norender_us,
                        uint32_t cost_render_us, uint32_t budget_us) {
  pb_plan_t plan = {0, false, 0};
  const uint32_t due = pb_pacer_frames_due(p, now_us);
  if (due == 0) return plan;

  const uint64_t with_render = (uint64_t)(due - 1) * cost_norender_us + cost_render_us;
  const uint64_t without_render = (uint64_t)due * cost_norender_us;
  if (with_render <= budget_us) {
    plan.frames = due;
    plan.render = true;
  } else if (without_render <= budget_us) {
    plan.frames = due;
    plan.render = false;
  } else {
    uint32_t fit = cost_norender_us ? budget_us / cost_norender_us : due;
    if (fit < 1) fit = 1;  // always make progress
    if (fit > due) fit = due;
    plan.frames = fit;
    plan.render = false;
    plan.dropped = due - fit;
  }
  return plan;
}
