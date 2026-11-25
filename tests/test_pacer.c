// Host unit tests for core/pb_pacer.
#include <stdint.h>
#include <stdio.h>

#include "pb_pacer.h"

static int failures = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      failures++;                                                \
    }                                                            \
  } while (0)

// Simulate `refreshes` display refreshes of period_us each; returns total frames due.
static uint32_t simulate(uint32_t period_us, uint32_t refreshes, uint32_t start_us, uint32_t *max_per_call) {
  pb_pacer_t p;
  pb_pacer_init(&p);
  uint32_t total = 0, now = start_us;
  *max_per_call = 0;
  for (uint32_t i = 0; i < refreshes; i++) {
    uint32_t n = pb_pacer_frames_due(&p, now);
    total += n;
    if (n > *max_per_call) *max_per_call = n;
    now += period_us;
  }
  return total;
}

int main(void) {
  uint32_t mx;

  // 40 Hz panel (measured 25,160 us): about 1.503 frames per refresh -> 1,2,1,2...
  // Expected: first frame + elapsed time / frame period.
  uint32_t total = simulate(25160, 4000, 1000, &mx);
  double expect = 1.0 + (3999.0 * 25160.0) / 16742.706;
  CHECK((double)total >= expect - 1.5 && (double)total <= expect + 1.5);
  CHECK(mx == 2);

  // 60 Hz panel (16,667 us): one frame per refresh, never more than 1 in steady state.
  total = simulate(16667, 6000, 0, &mx);
  expect = 1.0 + (5999.0 * 16667.0) / 16742.706;
  CHECK((double)total >= expect - 1.5 && (double)total <= expect + 1.5);
  CHECK(mx == 1);

  // Loop faster than the game (8,000 us): some refreshes owe 0 frames, never more than 1.
  total = simulate(8000, 3000, 0, &mx);
  CHECK(mx == 1);
  CHECK(total > 1400 && total < 1500);  // about 3000*8000/16742.7 = 1433

  // Time wraps around the 32-bit clock without a burst of extra frames.
  total = simulate(25160, 2000, 0xFFFFFFFFu - 500000u, &mx);
  expect = 1.0 + (1999.0 * 25160.0) / 16742.706;
  CHECK((double)total >= expect - 1.5 && (double)total <= expect + 1.5);
  CHECK(mx == 2);

  // A one-second stall is capped (no catch-up spiral) and the backlog is dropped.
  pb_pacer_t p;
  pb_pacer_init(&p);
  (void)pb_pacer_frames_due(&p, 0);
  CHECK(pb_pacer_frames_due(&p, 1000000u) == PB_PACER_MAX_CATCH_UP);
  CHECK(pb_pacer_frames_due(&p, 1000000u + 16743u) == 1);  // back to normal right away

  // Re-init gives a fresh start (first call always owes exactly one frame).
  pb_pacer_init(&p);
  CHECK(pb_pacer_frames_due(&p, 12345u) == 1);
  CHECK(pb_pacer_frames_due(&p, 12345u) == 0);

  if (failures == 0) printf("pacer tests passed\n");
  return failures ? 1 : 0;
}
