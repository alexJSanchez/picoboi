// Host test for core audio. Builds a tiny ROM in memory (our own code, no Nintendo data)
// that turns on sound channel 1 and plays a square wave, then checks the samples that
// pb_core delivers: one call per frame, right count, silent before the tone starts, and the
// right pitch afterwards.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pb_core.h"

static int failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                            \
    }                                                        \
  } while (0)

#define ROM_SIZE 0x8000
#define FRAMES 60
#define TONE_START_FRAME 10  // the ROM waits this many frames before starting the tone

static uint8_t g_rom[ROM_SIZE];

static void build_tone_rom(uint8_t nr13, uint8_t nr14) {
  memset(g_rom, 0, sizeof(g_rom));
  // Entry point: NOP; JP 0x0150
  g_rom[0x100] = 0x00;
  g_rom[0x101] = 0xC3;
  g_rom[0x102] = 0x50;
  g_rom[0x103] = 0x01;
  // Title, cartridge type 0 (ROM only), 32 KiB ROM, no RAM.
  memcpy(&g_rom[0x134], "TONETEST", 8);
  g_rom[0x147] = 0x00;
  g_rom[0x148] = 0x00;
  g_rom[0x149] = 0x00;
  uint8_t chk = 0;
  for (int i = 0x134; i <= 0x14C; i++) chk = (uint8_t)(chk - g_rom[i] - 1);
  g_rom[0x14D] = chk;

  uint8_t *p = &g_rom[0x150];
  // Wait ~TONE_START_FRAME frames using the LY register (0xFF44): count VBlank starts.
  //   LD B, N            ; frames to wait
  // wait_vbl:
  //   LDH A, (0x44)      ; LY
  //   CP 144
  //   JR NZ, wait_vbl    ; wait until line 144
  // wait_leave:
  //   LDH A, (0x44)
  //   CP 144
  //   JR Z, wait_leave   ; wait until we leave line 144 (so each frame counts once)
  //   DEC B
  //   JR NZ, wait_vbl
  *p++ = 0x06; *p++ = TONE_START_FRAME;      // LD B, n
  uint8_t *wait_vbl = p;
  *p++ = 0xF0; *p++ = 0x44;                  // LDH A, (0x44)
  *p++ = 0xFE; *p++ = 144;                   // CP 144
  *p++ = 0x20; *p = (uint8_t)(wait_vbl - (p + 1)); p++;  // JR NZ, wait_vbl
  uint8_t *wait_leave = p;
  *p++ = 0xF0; *p++ = 0x44;                  // LDH A, (0x44)
  *p++ = 0xFE; *p++ = 144;                   // CP 144
  *p++ = 0x28; *p = (uint8_t)(wait_leave - (p + 1)); p++; // JR Z, wait_leave
  *p++ = 0x05;                               // DEC B
  *p++ = 0x20; *p = (uint8_t)(wait_vbl - (p + 1)); p++;  // JR NZ, wait_vbl
  // Sound setup: NR52 on, NR50 max volume, NR51 all channels both sides,
  // NR11 50% duty, NR12 volume 15 no envelope, NR13/NR14 frequency + trigger.
  static const uint8_t regs[][2] = {
      {0x26, 0x80}, {0x24, 0x77}, {0x25, 0xFF}, {0x11, 0x80}, {0x12, 0xF0}};
  for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
    *p++ = 0x3E; *p++ = regs[i][1];          // LD A, value
    *p++ = 0xE0; *p++ = regs[i][0];          // LDH (reg), A
  }
  *p++ = 0x3E; *p++ = nr13; *p++ = 0xE0; *p++ = 0x13;  // NR13
  *p++ = 0x3E; *p++ = nr14; *p++ = 0xE0; *p++ = 0x14;  // NR14 (bit 7 = trigger)
  *p++ = 0x18; *p++ = 0xFE;                  // JR -2 (loop forever)
}

typedef struct {
  int16_t samples[FRAMES][PB_AUDIO_SAMPLES_PER_FRAME];
  uint32_t calls;
  uint32_t bad_count;
} sink_t;

static void on_audio(void *user, const int16_t *mono, uint32_t count) {
  sink_t *s = (sink_t *)user;
  if (count != PB_AUDIO_SAMPLES_PER_FRAME) s->bad_count++;
  if (s->calls < FRAMES && count == PB_AUDIO_SAMPLES_PER_FRAME)
    memcpy(s->samples[s->calls], mono, sizeof(s->samples[0]));
  s->calls++;
}

static void on_line(void *u, const uint8_t px[PB_SCREEN_W], uint8_t line) {
  (void)u; (void)px; (void)line;
}

static sink_t g_sink;
static pb_core_t g_core;

// Rising zero crossings of the signal, relative to its mean, over frames [from, to).
static double estimate_hz(int from, int to, int *peak_out) {
  int64_t sum = 0, n = 0;
  int peak = 0;
  for (int f = from; f < to; f++)
    for (int i = 0; i < PB_AUDIO_SAMPLES_PER_FRAME; i++) {
      sum += g_sink.samples[f][i];
      n++;
    }
  int mean = (int)(sum / n);
  int crossings = 0, prev_below = 1;
  for (int f = from; f < to; f++)
    for (int i = 0; i < PB_AUDIO_SAMPLES_PER_FRAME; i++) {
      int v = g_sink.samples[f][i] - mean;
      if (abs(v) > peak) peak = abs(v);
      int below = v < 0;
      if (prev_below && !below) crossings++;
      prev_below = below;
    }
  *peak_out = peak;
  return (double)crossings * (double)PB_AUDIO_NOMINAL_RATE / (double)n;
}

static void run(uint8_t nr13, uint8_t nr14, double expect_hz) {
  build_tone_rom(nr13, nr14);
  memset(&g_sink, 0, sizeof(g_sink));
  pb_core_config_t cfg = {.rom = g_rom,
                          .rom_size = ROM_SIZE,
                          .draw_line = on_line,
                          .on_audio = on_audio,
                          .user = &g_sink};
  CHECK(pb_core_init(&g_core, &cfg) == PB_OK);
  for (int f = 0; f < FRAMES; f++) pb_core_run_frame(&g_core);

  CHECK(g_sink.calls == FRAMES);   // exactly one delivery per frame
  CHECK(g_sink.bad_count == 0);    // always PB_AUDIO_SAMPLES_PER_FRAME samples

  // Before the program starts the tone (first few frames) the output is silent.
  int quiet_peak = 0;
  for (int f = 0; f < TONE_START_FRAME - 2; f++)
    for (int i = 0; i < PB_AUDIO_SAMPLES_PER_FRAME; i++)
      if (abs(g_sink.samples[f][i]) > quiet_peak) quiet_peak = abs(g_sink.samples[f][i]);
  CHECK(quiet_peak == 0);

  // Well after the trigger: loud, and at the right pitch (within 1.5%).
  int peak = 0;
  double hz = estimate_hz(TONE_START_FRAME + 5, FRAMES, &peak);
  printf("expected %.1f Hz, measured %.1f Hz, peak %d\n", expect_hz, hz, peak);
  CHECK(peak > 500);
  CHECK(hz > expect_hz * 0.985 && hz < expect_hz * 1.015);
}

int main(void) {
  // Channel 1 frequency = 131072 / (2048 - x) Hz.
  // x = 1750 (0x6D6): about 439.8 Hz.  x = 1900 (0x76C): about 885.7 Hz.
  run(0xD6, 0x80 | 0x06, 131072.0 / (2048 - 1750));
  run(0x6C, 0x80 | 0x07, 131072.0 / (2048 - 1900));

  // Without an audio sink nothing is generated and nothing crashes.
  build_tone_rom(0xD6, 0x86);
  pb_core_config_t cfg = {.rom = g_rom, .rom_size = ROM_SIZE, .draw_line = on_line};
  CHECK(pb_core_init(&g_core, &cfg) == PB_OK);
  for (int f = 0; f < 5; f++) pb_core_run_frame(&g_core);

  if (failures == 0) printf("audio tests passed\n");
  return failures ? 1 : 0;
}
