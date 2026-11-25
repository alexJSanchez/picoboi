// pb_audio_sync: keeps the audio producer (one burst of samples per emulated frame) and
// the consumer (DMA feeding the speaker at a fixed rate) in step. Pure logic, no hardware.
//
// The two clocks cannot match exactly, and frames arrive in bursts of one or two per
// display refresh, so the amount of audio waiting in the ring buffer ("fill") wanders.
// Every frame this module decides how many samples to write:
//   - normally exactly `count`;
//   - one sample fewer/more when the smoothed fill has drifted away from the level it had
//     when playback settled (an inaudible nudge; about one per 2.5 s for a 0.4 sample/s
//     clock mismatch, at most one per PB_ASYNC_COOLDOWN frames);
//   - none if the ring is nearly full (overrun: drop this frame's audio);
//   - after an underrun (consumer got ahead of the producer) a resync: the caller clears
//     the ring to silence and sets the fill to PB_ASYNC_TARGET (before writing this
//     frame, exactly like the initial prefill), so stale audio is never replayed as a buzz.
#ifndef PB_AUDIO_SYNC_H
#define PB_AUDIO_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PB_ASYNC_RING 2048    // ring capacity in samples (power of two)
#define PB_ASYNC_TARGET 1000  // fill (samples) the ring is prefilled/resynced to
#define PB_ASYNC_HYST 64      // allowed deviation of the smoothed fill before nudging
#define PB_ASYNC_LOCK_FRAMES 512  // frames averaged (about 8.5 s) to learn the settled fill level
#define PB_ASYNC_COOLDOWN 64     // minimum frames between two nudges

typedef struct {
  int32_t avg_x64;  // smoothed fill, times 64
  int32_t ref_x64;  // settled fill level learned after PB_ASYNC_LOCK_FRAMES, times 64
  int32_t sum;      // running sum of the fill while learning
  uint32_t frames;
  uint32_t cooldown;
  bool started;
  bool locked;
  uint32_t underruns, overruns, drops, dups;
} pb_async_t;

typedef struct {
  bool resync;     // true: clear the ring to silence and set fill to TARGET, then write
  uint32_t write;  // how many samples to write: count - 1, count or count + 1; 0 = drop frame
} pb_async_plan_t;

void pb_async_init(pb_async_t *s);

// fill: samples produced but not yet consumed (negative = consumer is ahead of us).
// count: samples delivered by this frame (PB_AUDIO_SAMPLES_PER_FRAME).
pb_async_plan_t pb_async_plan(pb_async_t *s, int32_t fill, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
