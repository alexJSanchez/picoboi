#include "pb_audio_sync.h"

void pb_async_init(pb_async_t *s) {
  s->avg_x64 = PB_ASYNC_TARGET * 64;
  s->ref_x64 = PB_ASYNC_TARGET * 64;
  s->sum = 0;
  s->frames = 0;
  s->cooldown = 0;
  s->started = false;
  s->locked = false;
  s->underruns = s->overruns = s->drops = s->dups = 0;
}

pb_async_plan_t pb_async_plan(pb_async_t *s, int32_t fill, uint32_t count) {
  pb_async_plan_t plan = {false, count};

  if (fill < 0) {  // consumer overtook the producer: resync instead of replaying stale data
    s->underruns++;
    pb_async_t keep = *s;
    pb_async_init(s);  // forget the old average; measure the equilibrium again
    s->underruns = keep.underruns;
    s->overruns = keep.overruns;
    s->drops = keep.drops;
    s->dups = keep.dups;
    plan.resync = true;
    return plan;
  }

  if (!s->started) {
    s->avg_x64 = fill * 64;
    s->started = true;
  }
  // Exponential moving average of the fill with a time constant of 64 frames.
  s->avg_x64 += (fill * 64 - s->avg_x64) / 64;
  s->frames++;

  if (!s->locked) {
    // The fill seen when a frame arrives is not constant: it depends on the 1,2,1,2
    // cadence. Learn its plain average first, then only correct drift away from that.
    s->sum += fill;
    if (s->frames >= PB_ASYNC_LOCK_FRAMES) {
      s->ref_x64 = (s->sum / (int32_t)s->frames) * 64;
      s->locked = true;
    }
  } else if (s->cooldown > 0) {
    s->cooldown--;
  } else {
    int32_t err = s->avg_x64 - s->ref_x64;
    if (err > PB_ASYNC_HYST * 64) {          // too much waiting: skip one sample
      plan.write = count - 1;
      s->drops++;
      s->cooldown = PB_ASYNC_COOLDOWN;
    } else if (err < -PB_ASYNC_HYST * 64) {  // too little: repeat one sample
      plan.write = count + 1;
      s->dups++;
      s->cooldown = PB_ASYNC_COOLDOWN;
    }
  }

  if (fill + (int32_t)plan.write > PB_ASYNC_RING - 8) {  // would overwrite unplayed audio
    s->overruns++;
    plan.write = 0;
  }
  return plan;
}
