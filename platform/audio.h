// platform/audio: plays the emulator's mono audio on the PicoSystem's piezo speaker.
//
// The speaker is on GPIO 11 (PWM). One PWM period equals one audio sample (about 32.7 kHz);
// DMA copies the next level from a ring buffer into the PWM compare register at the start
// of every period, so the CPU does no per-sample work. The producer side (audio_push) is
// kept in step with the DMA by core/pb_audio_sync.
#ifndef PB_PLATFORM_AUDIO_H
#define PB_PLATFORM_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

// Stops the SDK's own 1 ms audio timer (it would overwrite our PWM settings), sets up the
// PWM and DMA, and starts playing silence. Call once from the SDK's init().
void audio_init();

// Queues the mono samples of one emulated frame (PB_AUDIO_SAMPLES_PER_FRAME of them).
void audio_push(const int16_t *mono, uint32_t count);

// Mute / unmute. While muted the speaker pin is held low.
void audio_set_enabled(bool on);
bool audio_enabled();

typedef struct {
  int32_t fill;  // samples queued but not yet played
  uint32_t underruns, overruns, drops, dups;
} audio_stats_t;
audio_stats_t audio_get_stats();

// Prints one diagnostic line over USB serial: the PWM registers as read back from the
// hardware, the measured playback rate of the DMA, and the buffer state. Intended to be
// called every few seconds while bringing up the speaker.
void audio_print_diag();

#ifdef PB_AUDIO_TEST_TONE
// Queues one frame of a built-in test tone (cycles through several pitches).
void audio_push_test_tone();
#endif

#endif
