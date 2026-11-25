#include "audio.h"

#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/time.h"
#include "picosystem.hpp"

#include "pb_audio_sync.h"
#include "pb_core.h"

// The SDK keeps this timer in hardware.cpp (non-static, namespace picosystem). It calls
// _update_audio() every millisecond, which rewrites the PWM divider, wrap and level even
// when nothing is playing. Not declared in the SDK headers; see docs/DECISIONS.md.
namespace picosystem {
extern struct repeating_timer _audio_update_timer;
}

namespace {

constexpr uint AUDIO_PIN = 11;
constexpr uint32_t RING = PB_ASYNC_RING;
constexpr uint32_t RING_BYTES_LOG2 = 12;  // 2048 samples * 2 bytes = 4096 bytes
static_assert(RING * sizeof(uint16_t) == (1u << RING_BYTES_LOG2), "ring size mismatch");

// PWM compare values, one per audio sample. Aligned so the DMA read pointer can wrap.
uint16_t g_ring[RING] __attribute__((aligned(RING * sizeof(uint16_t))));

int g_dma = -1;
uint g_slice = 0;
uint16_t g_wrap = 0;       // PWM TOP: one period per audio sample
uint16_t g_mid = 0;        // level for a zero sample (50% duty)
int32_t g_gain = 0;        // sample -> level scale, 10 fractional bits
uint32_t g_produced = 0;   // absolute number of samples written into the ring
pb_async_t g_sync;
bool g_ready = false;
bool g_enabled = true;

// The DMA transfer count is programmed to its maximum (about 36 hours of audio at 32.7 kHz);
// the number of samples played so far is the count programmed minus what remains.
constexpr uint32_t DMA_COUNT = 0xFFFFFFFFu;
inline uint32_t consumed() { return DMA_COUNT - dma_hw->ch[g_dma].transfer_count; }

uint16_t level_for(int16_t s) {
  if (!g_enabled) return 0;
  int32_t v = (int32_t)g_mid + (((int32_t)s * g_gain) >> 10);
  if (v < 0) v = 0;
  if (v > (int32_t)g_wrap) v = g_wrap;
  return (uint16_t)v;
}

void clear_ring() {
  const uint16_t silence = level_for(0);
  for (uint32_t i = 0; i < RING; i++) g_ring[i] = silence;
}

}  // namespace

void audio_init() {
  // 1. Stop the SDK's audio timer so nothing else touches the speaker's PWM.
  cancel_repeating_timer(&picosystem::_audio_update_timer);

  // 2. PWM: one period per audio sample. Cycles per sample = sys_clock / real sample rate,
  //    where real rate = 548 samples per 70224 Game Boy cycles at 4194304 Hz.
  gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
  g_slice = pwm_gpio_to_slice_num(AUDIO_PIN);
  const uint32_t sys_hz = clock_get_hz(clk_sys);
  const uint64_t num = (uint64_t)sys_hz * 70224u;
  const uint64_t den = (uint64_t)4194304u * PB_AUDIO_SAMPLES_PER_FRAME;
  const uint32_t period = (uint32_t)((num + den / 2) / den);  // 7638 at 250 MHz
  g_wrap = (uint16_t)(period - 1);
  g_mid = (uint16_t)(period / 2);
  // Full scale (+-16384, a loud 4-channel mix) maps to +-mid.
  g_gain = ((int32_t)g_mid * 1024) / 16384;

  pwm_config cfg = pwm_get_default_config();
  pwm_config_set_clkdiv_int(&cfg, 1);
  pwm_config_set_wrap(&cfg, g_wrap);
  pwm_init(g_slice, &cfg, false);  // configured but not running yet
  pwm_set_gpio_level(AUDIO_PIN, 0);

  // 3. Ring buffer prefilled with silence; DMA moves one entry into the PWM compare
  //    register each time the counter wraps (DREQ), forever.
  clear_ring();
  // The SDK drives the screen with DMA channel 0 (hardware.cpp: `dma_channel = 0`) without
  // ever claiming it, so a plain "claim any free channel" would hand it to us and break the
  // screen. Claim channel 0 on its behalf first so ours is a different one.
  if (!dma_channel_is_claimed(0)) dma_channel_claim(0);
  g_dma = dma_claim_unused_channel(true);
  if (g_dma == 0) {  // cannot happen after the claim above; never share the screen's channel
    printf("audio: refusing to use DMA channel 0 (screen)\n");
    return;
  }
  dma_channel_config c = dma_channel_get_default_config(g_dma);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_ring(&c, false, RING_BYTES_LOG2);  // wrap the read address
  channel_config_set_dreq(&c, DREQ_PWM_WRAP0 + g_slice);
  // The compare register holds channel A in its low half and B in its high half.
  const uint32_t chan_offset = (pwm_gpio_to_channel(AUDIO_PIN) == PWM_CHAN_B) ? 2 : 0;
  dma_channel_configure(g_dma, &c, (volatile uint8_t *)&pwm_hw->slice[g_slice].cc + chan_offset,
                        g_ring, DMA_COUNT, true);

  // 4. Start the PWM; the first wrap triggers the first DMA transfer.
  pwm_set_enabled(g_slice, true);

  pb_async_init(&g_sync);
  g_produced = consumed() + PB_ASYNC_TARGET;  // same prefill level the sync logic assumes
  g_ready = true;
  printf("audio: PWM period %u cycles (%u Hz), DMA ch %d\n", (unsigned)period,
         (unsigned)(sys_hz / period), g_dma);
}

void audio_push(const int16_t *mono, uint32_t count) {
  if (!g_ready || count == 0) return;
  const int32_t fill = (int32_t)(g_produced - consumed());
  const pb_async_plan_t plan = pb_async_plan(&g_sync, fill, count);
  if (plan.resync) {  // consumer overtook us: drop stale audio, restart at the target fill
    clear_ring();
    g_produced = consumed() + PB_ASYNC_TARGET;
  }
  for (uint32_t i = 0; i < plan.write; i++) {
    const int16_t s = mono[i < count ? i : count - 1];  // repeat the last sample if asked for one extra
    g_ring[g_produced & (RING - 1)] = level_for(s);
    g_produced++;
  }
}

void audio_reset() {
  if (!g_ready) return;
  clear_ring();
  pb_async_init(&g_sync);
  g_produced = consumed() + PB_ASYNC_TARGET;
}

void audio_set_enabled(bool on) { g_enabled = on; }
bool audio_enabled() { return g_enabled; }

audio_stats_t audio_get_stats() {
  audio_stats_t st = {};
  if (!g_ready) return st;
  st.fill = (int32_t)(g_produced - consumed());
  st.underruns = g_sync.underruns;
  st.overruns = g_sync.overruns;
  st.drops = g_sync.drops;
  st.dups = g_sync.dups;
  return st;
}

void audio_print_diag() {
  if (!g_ready) return;
  static uint32_t last_us = 0, last_consumed = 0;
  const uint32_t now_us = time_us_32();
  const uint32_t now_consumed = consumed();
  uint32_t rate = 0;
  if (last_us) rate = (uint32_t)(((uint64_t)(now_consumed - last_consumed) * 1000000u) / (now_us - last_us));
  last_us = now_us;
  last_consumed = now_consumed;

  const audio_stats_t st = audio_get_stats();
  const uint32_t div = pwm_hw->slice[g_slice].div;
  printf("audio diag: pwm top=%u (expected %u) div=%u.%u en=%d | dma playback=%u Hz (expected 32730) "
         "busy=%d | cc=0x%08x | fill=%d under=%u over=%u drops=%u dups=%u | %s\n",
         (unsigned)pwm_hw->slice[g_slice].top, (unsigned)g_wrap, (unsigned)(div >> 4),
         (unsigned)(div & 0xF), (int)((pwm_hw->slice[g_slice].csr & 1u) != 0), (unsigned)rate,
         (int)dma_channel_is_busy(g_dma), (unsigned)pwm_hw->slice[g_slice].cc, (int)st.fill,
         (unsigned)st.underruns, (unsigned)st.overruns, (unsigned)st.drops, (unsigned)st.dups,
         g_enabled ? "on" : "muted");
}

#ifdef PB_AUDIO_TEST_TONE
// Square wave, changes pitch every 90 frames (1.5 s): 220, 440, 880, 1760, 3520, 5000 Hz,
// then silence for one step. The pitch is printed when it changes.
void audio_push_test_tone() {
  static const uint32_t freqs[] = {220, 440, 880, 1760, 3520, 5000, 0};
  static uint32_t frame = 0, phase = 0;
  const uint32_t step = (frame / 90) % (sizeof(freqs) / sizeof(freqs[0]));
  const uint32_t hz = freqs[step];
  if (frame % 90 == 0) printf("test tone: %u Hz\n", (unsigned)hz);
  frame++;

  int16_t buf[PB_AUDIO_SAMPLES_PER_FRAME];
  for (uint32_t i = 0; i < PB_AUDIO_SAMPLES_PER_FRAME; i++) {
    if (hz == 0) {
      buf[i] = 0;
    } else {
      phase += hz;  // phase wraps every PB_AUDIO_NOMINAL_RATE
      if (phase >= PB_AUDIO_NOMINAL_RATE) phase -= PB_AUDIO_NOMINAL_RATE;
      buf[i] = (phase < PB_AUDIO_NOMINAL_RATE / 2) ? 8000 : -8000;
    }
  }
  audio_push(buf, PB_AUDIO_SAMPLES_PER_FRAME);
}
#endif
