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

// Regression: ONE very slow drawn frame must not freeze the picture for ever. (Seen as "the game
// goes on and the music plays but the screen stays on the title picture".) Mirrors the loop in
// src/main.cpp: plan, emulate, track the cost of what ran, relax the draw estimate when nothing
// was drawn.
static void test_slow_frame_does_not_freeze_picture(void) {
  pb_pacer_t p;
  pb_pacer_init(&p);
  uint32_t nr = 10000, r = 17000, now = 0, drawn = 0, drawn_after = 0, first_redraw = 0;
  for (uint32_t u = 0; u < 4000; u++) {
    now += 25200;
    const pb_plan_t pl = pb_pacer_plan(&p, now, nr, r, 23000);
    for (uint32_t i = 0; i < pl.frames; i++) {
      const bool render = pl.render && i == pl.frames - 1;
      uint32_t cost = render ? 4500 : 2500;  // a light scene
      if (render && u == 50) cost = 30000;   // one slow drawn frame (a cold cache, say)
      if (render) r = pb_cost_track(r, cost); else nr = pb_cost_track(nr, cost);
    }
    if (pl.frames && !pl.render) r = pb_cost_relax(r, nr);
    if (pl.frames && pl.render) {
      drawn++;
      if (u > 50) {
        drawn_after++;
        if (!first_redraw) first_redraw = u;
      }
    }
  }
  CHECK(drawn_after > 3000);                      // drawing resumes and stays on
  CHECK(first_redraw > 50 && first_redraw < 90);  // within a second or so (40 refreshes)
  CHECK(r < 10000);                               // and the estimate came back down

  // A scene that really is that slow keeps being drawn only now and then, never never.
  pb_pacer_init(&p);
  nr = 10000; r = 17000; now = 0; drawn = 0;
  for (uint32_t u = 0; u < 2000; u++) {
    now += 25200;
    const pb_plan_t pl = pb_pacer_plan(&p, now, nr, r, 23000);
    for (uint32_t i = 0; i < pl.frames; i++) {
      const bool render = pl.render && i == pl.frames - 1;
      const uint32_t cost = render ? 24000 : 6000;
      if (render) r = pb_cost_track(r, cost); else nr = pb_cost_track(nr, cost);
    }
    if (pl.frames && !pl.render) r = pb_cost_relax(r, nr);
    if (pl.frames && pl.render) drawn++;
  }
  CHECK(drawn > 20);       // probes: the picture still updates now and then
  CHECK(drawn < 1000);     // but a genuinely over-budget scene is not drawn every time
}

// pb_cost_relax: decays, never below the floor, never raises a value already below it.
static void test_cost_relax(void) {
  CHECK(pb_cost_relax(32000, 5000) == 32000 - 1000);
  CHECK(pb_cost_relax(5100, 5000) == 5000);
  CHECK(pb_cost_relax(5000, 5000) == 5000);
  CHECK(pb_cost_relax(4000, 5000) == 4000);
  uint32_t e = 30000;
  for (int i = 0; i < 500; i++) e = pb_cost_relax(e, 4000);
  CHECK(e == 4000);
}

int main(void) {
  uint32_t mx;
  test_slow_frame_does_not_freeze_picture();
  test_cost_relax();

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

  // ---- pb_pacer_plan: budgeted scheduling at 40 Hz (25,160 us per refresh), budget 23,000 us.
  {
    struct {
      uint32_t nr, r;
      const char *name;
    } scen[] = {{4000, 9000, "light"}, {9250, 16900, "heavy"}, {14000, 22000, "extreme"}};
    for (int sc = 0; sc < 3; sc++) {
      pb_pacer_t pp;
      pb_pacer_init(&pp);
      uint64_t frames = 0, rendered = 0, dropped = 0, refreshes = 7200 * 40;
      uint32_t worst_cost = 0, now = 5000;
      for (uint64_t i = 0; i < refreshes; i++) {
        pb_plan_t pl = pb_pacer_plan(&pp, now, scen[sc].nr, scen[sc].r, 23000);
        uint32_t cost = pl.frames ? (pl.render ? (pl.frames - 1) * scen[sc].nr + scen[sc].r
                                               : pl.frames * scen[sc].nr)
                                  : 0;
        if (cost > worst_cost) worst_cost = cost;
        frames += pl.frames;
        rendered += pl.render ? 1 : 0;
        dropped += pl.dropped;
        if (pl.frames == 0) CHECK(!pl.render && pl.dropped == 0);
        now += 25160;
      }
      double ideal = (double)refreshes * 25160.0 / 16742.706;
      printf("plan %-7s: game speed %.1f%%, drawn on %.0f%% of refreshes, dropped %llu, worst %u us\n",
             scen[sc].name, 100.0 * (double)frames / ideal,
             100.0 * (double)rendered / (double)refreshes, (unsigned long long)dropped, worst_cost);
      if (sc == 0) {  // light scene: full speed, drawn on every refresh that has a frame
        CHECK(dropped == 0);
        CHECK((double)frames > ideal * 0.999);
        CHECK(rendered > refreshes * 99 / 100);
      } else if (sc == 1) {  // heavy: full game speed, never over budget, drawn about half the time
        CHECK(dropped == 0);
        CHECK((double)frames > ideal * 0.999);
        CHECK(worst_cost <= 23000);
        CHECK(rendered > refreshes * 45 / 100 && rendered < refreshes * 55 / 100);
      } else {  // extreme: cannot keep up; still progresses and reports the dropped frames
        CHECK(frames > 0);
        CHECK(dropped > 0);
      }
    }
  }

  if (failures == 0) printf("pacer tests passed\n");
  return failures ? 1 : 0;
}
