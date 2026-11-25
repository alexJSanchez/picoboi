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

#ifdef __cplusplus
}
#endif

#endif
