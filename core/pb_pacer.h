// pb_pacer: decides how many Game Boy frames are due for the time that has passed,
// independent of the display refresh rate. Pure logic, no hardware (host-tested).
//
// The Game Boy runs 70224 cycles per frame at 4194304 Hz = 16742.706 us per frame
// (59.7275 Hz). If the screen refreshes at 40 Hz we owe about 1.5 frames per refresh
// (1, 2, 1, 2...); at 60 Hz about 1 per refresh.
#ifndef PB_PACER_H
#define PB_PACER_H

#include <stdbool.h>
#include <stdint.h>

#define PB_FRAME_PERIOD_NS 16742706u
// Never owe more than this many frames at once; if we are further behind than that
// (long stall, slow scene) we drop the backlog instead of racing to catch up.
// Only a cap: pb_pacer_plan() additionally limits the work to a time budget, which is what
// stops a slow scene from missing the display refresh (measured: that locked the display at
// 20 Hz when frames were simply run until done).
#define PB_PACER_MAX_CATCH_UP 3u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t last_us;
  uint64_t acc_ns;
  bool started;
} pb_pacer_t;

void pb_pacer_init(pb_pacer_t *p);

// now_us: a free-running 32-bit microsecond clock (wraps every ~71 minutes; handled).
// Returns the number of frames to run now (0 to PB_PACER_MAX_CATCH_UP).
uint32_t pb_pacer_frames_due(pb_pacer_t *p, uint32_t now_us);

// What to do in one display refresh.
typedef struct {
  uint32_t frames;   // Game Boy frames to emulate now (0 = none due yet)
  bool render;       // draw the last of them (false = emulate only, picture stays as is)
  uint32_t dropped;  // frames that were due but did not fit the budget and are skipped
} pb_plan_t;

// Time-aware version of pb_pacer_frames_due(). Chooses the best plan whose estimated time
// fits `budget_us`: all due frames with the last one drawn; else all due frames without
// drawing (game speed and audio stay right, the picture just repeats); else as many as fit
// (at least one) and the rest are dropped (the game slows down). Estimates are in
// microseconds per frame: `cost_norender_us` for a frame emulated without drawing,
// `cost_render_us` for one with drawing.
pb_plan_t pb_pacer_plan(pb_pacer_t *p, uint32_t now_us, uint32_t cost_norender_us,
                        uint32_t cost_render_us, uint32_t budget_us);

// Running cost estimates (microseconds per frame). An estimate jumps to a slower sample at once and
// decays by 1/32 per update, so a heavier scene is respected before it is measured again.
static inline uint32_t pb_cost_track(uint32_t est, uint32_t sample) {
  return sample > est ? sample : est - est / 32;
}
// The cost of a DRAWN frame is only measured when a frame is drawn. If one slow sample pushed the
// estimate above the budget, no frame would be drawn again and it would never be re-measured (the
// game runs on, the picture freezes). So whenever frames were emulated but none drawn, the estimate
// relaxes by the same 1/32, never below `floor_us` (a drawn frame costs at least an undrawn one);
// the planner then tries drawing again and measures the real cost.
static inline uint32_t pb_cost_relax(uint32_t est, uint32_t floor_us) {
  const uint32_t lower = est - est / 32;
  return lower > floor_us ? lower : (est > floor_us ? floor_us : est);
}

#ifdef __cplusplus
}
#endif

#endif
