// Host unit tests for core/pb_audio_sync: simulates the producer (bursts of frames) and a
// consumer at a slightly different rate, applying the plans exactly as platform/audio does.
#include <stdint.h>
#include <stdio.h>

#include "pb_audio_sync.h"

#define COUNT 548

static int failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                            \
    }                                                        \
  } while (0)

typedef struct {
  pb_async_t s;
  int64_t produced, consumed;  // absolute sample counts
  int32_t min_fill, max_fill;
} sim_t;

static void sim_init(sim_t *m) {
  pb_async_init(&m->s);
  m->consumed = 0;
  m->produced = PB_ASYNC_TARGET;  // prefill with silence, as platform/audio does
  m->min_fill = 1 << 30;
  m->max_fill = -(1 << 30);
}

// One frame arrives; apply the plan the way the platform layer would.
static void sim_frame(sim_t *m) {
  int32_t fill = (int32_t)(m->produced - m->consumed);
  if (fill < m->min_fill) m->min_fill = fill;
  if (fill > m->max_fill) m->max_fill = fill;
  pb_async_plan_t p = pb_async_plan(&m->s, fill, COUNT);
  if (p.resync) m->produced = m->consumed + PB_ASYNC_TARGET;  // same level as the first prefill
  m->produced += p.write;
}

// Run `refreshes` display refreshes of `period_us`; the frame pacer owes frames by time
// (1,2,1,2 at 25.16 ms); the consumer plays `rate` samples per second.
static void run(sim_t *m, double rate, double period_us, long refreshes) {
  double owed_us = 16742.706;  // first frame immediately
  double cons_frac = 0;
  for (long r = 0; r < refreshes; r++) {
    while (owed_us >= 16742.706) {
      sim_frame(m);
      owed_us -= 16742.706;
    }
    owed_us += period_us;
    cons_frac += rate * period_us / 1e6;
    int64_t whole = (int64_t)cons_frac;
    m->consumed += whole;
    cons_frac -= (double)whole;
  }
}

int main(void) {
  const double nominal = 548.0 / 0.016742706;  // 32,730.7 samples/s

  // 1. Perfectly matched clocks, 40 Hz display: no events at all over 2 hours.
  sim_t m;
  sim_init(&m);
  run(&m, nominal, 25160.0, 7200 * 40);
  CHECK(m.s.underruns == 0 && m.s.overruns == 0);
  CHECK(m.min_fill >= 0 && m.max_fill <= PB_ASYNC_RING - 8);

  // 2. Consumer 0.4 samples/s slower than nominal (the real PWM rounding error): the
  //    nudges absorb it, no underruns/overruns in 2 hours, and nudges are rare.
  sim_init(&m);
  run(&m, nominal - 0.4, 25160.0, 7200 * 40);
  CHECK(m.s.underruns == 0 && m.s.overruns == 0);
  // 0.4 samples/s over 7200 s is about 2,880 samples to absorb, almost all by dropping.
  CHECK(m.s.drops > 2000 && m.s.drops < 3800 && m.s.dups < 100);
  CHECK(m.min_fill >= 0 && m.max_fill <= PB_ASYNC_RING - 8);

  // 3. Consumer 0.4 samples/s faster.
  sim_init(&m);
  run(&m, nominal + 0.4, 25160.0, 7200 * 40);
  CHECK(m.s.underruns == 0 && m.s.overruns == 0);
  CHECK(m.s.dups > 2000 && m.s.dups < 3800 && m.s.drops < 100);
  CHECK(m.min_fill >= 0 && m.max_fill <= PB_ASYNC_RING - 8);

  // 4. 60 Hz display (16,667 us): one frame per refresh.
  sim_init(&m);
  run(&m, nominal, 16667.0, 7200 * 60);
  CHECK(m.s.underruns == 0 && m.s.overruns == 0);

  // 5. A long stall (producer silent for 2 s) is an underrun; it resyncs once and the
  //    fill returns to the target, with no further underruns.
  sim_init(&m);
  run(&m, nominal, 25160.0, 400);
  m.consumed += (int64_t)(2.0 * nominal);  // consumer keeps playing during the stall
  run(&m, nominal, 25160.0, 4000);
  CHECK(m.s.underruns == 1);
  int32_t fill_now = (int32_t)(m.produced - m.consumed);
  CHECK(fill_now > 0 && fill_now < PB_ASYNC_RING - 8);

  // 6. Overrun: producer far ahead of the consumer drops frames instead of overwriting.
  pb_async_t s;
  pb_async_init(&s);
  pb_async_plan_t p = pb_async_plan(&s, PB_ASYNC_RING - 100, COUNT);
  CHECK(p.write == 0 && !p.resync && s.overruns == 1);

  // 7. Underrun plan asks for a resync and still delivers the frame's samples.
  pb_async_init(&s);
  p = pb_async_plan(&s, -50, COUNT);
  CHECK(p.resync && p.write == COUNT && s.underruns == 1);

  if (failures == 0) printf("audio sync tests passed\n");
  return failures ? 1 : 0;
}
