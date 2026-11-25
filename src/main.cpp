// Run an embedded free test ROM through core/ and show it on screen.
//
// SDK loop per refresh: update(); wait for the previous framebuffer transfer; draw();
// wait for vsync; start the next transfer. The transfer takes about 12 ms of the 25 ms
// refresh (40 Hz panel, SDK setting) and runs in the background, so the emulator runs in
// update(), overlapping the transfer, and draws into a private off-screen buffer.
// draw() only copies that buffer into the SDK framebuffer.
// The Game Boy runs at 59.73 Hz, so emulation is paced by time (pb_pacer): each refresh
// runs however many frames are due (1, 2, 1, 2... at 40 Hz); only the last is rendered.
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "picosystem.hpp"

#include "audio.h"
#include "display.h"
#include "input.h"
#include "pb_core.h"
#include "pb_pacer.h"

using namespace picosystem;

#ifdef PB_HAVE_EMBEDDED_ROM
extern "C" const uint8_t pb_embedded_rom[];
extern "C" const uint8_t pb_embedded_rom_end[];
#endif

namespace {

pb_core_t g_core;
pb_pacer_t g_pacer;
uint8_t g_cart_ram[32 * 1024];  // sized for Pokemon Red/Blue/Yellow (PLAN.md section 4)

bool g_running = false;
const char *g_error = nullptr;
uint32_t g_error_count = 0;

// Statistics, printed over USB serial every BENCH_UPDATES display refreshes.
constexpr uint32_t BENCH_UPDATES = 600;
struct Stat {
  uint32_t min = 0xFFFFFFFF, max = 0;
  uint64_t sum = 0;
  uint32_t n = 0;
  void add(uint32_t v) {
    if (v < min) min = v;
    if (v > max) max = v;
    sum += v;
    n++;
  }
  uint32_t avg() const { return n ? (uint32_t)(sum / n) : 0; }
  void reset() { *this = Stat(); }
};
Stat g_emu_us;     // time to emulate one Game Boy frame
Stat g_update_us;  // time spent in update(), per display refresh
Stat g_period_us;  // time between display refreshes
Stat g_flip_us;    // SDK: time from flip start until the transfer finished
Stat g_blit_us;    // draw(): copying/scaling the off-screen frame into the framebuffer
uint32_t g_updates = 0;
uint32_t g_emulated = 0;      // Game Boy frames emulated in this window
uint32_t g_drawn_updates = 0;  // refreshes in this window in which a new picture was drawn
uint32_t g_dropped = 0;        // frames skipped because they did not fit the time budget

// Time budget for emulation in one display refresh: refresh period (about 25.2 ms at the
// panel's 40 Hz) minus the blit (about 1 ms) minus a safety margin. See docs/DECISIONS.md.
constexpr uint32_t UPDATE_BUDGET_US = 23000;
// Running estimates of the cost of one frame without / with drawing. They hold the recent
// peak and decay slowly, so a sudden heavier scene is respected before it is measured again.
uint32_t g_cost_norender_us = 10000;
uint32_t g_cost_render_us = 17000;
inline void track_cost(uint32_t &est, uint32_t sample) {
  est = sample > est ? sample : est - est / 32;
}
uint32_t g_window_start_us = 0;
uint32_t g_last_start_us = 0;

void on_core_audio(void *, const int16_t *mono, uint32_t count) { audio_push(mono, count); }

void on_core_error(void *, int code, uint16_t addr) {
  if (g_error_count++ < 5) printf("core error %d at 0x%04x\n", code, addr);
}

}  // namespace

namespace {
#ifdef PB_HAVE_EMBEDDED_ROM
pb_core_config_t make_core_config() {
  pb_core_config_t cfg = {};
  cfg.rom = pb_embedded_rom;
  cfg.rom_size = (uint32_t)(pb_embedded_rom_end - pb_embedded_rom);
  cfg.cart_ram = g_cart_ram;
  cfg.cart_ram_size = sizeof(g_cart_ram);
  cfg.draw_line = display_draw_line;
  cfg.on_error = on_core_error;
  cfg.clock_us = time_us_32;
#ifndef PB_AUDIO_TEST_TONE
  cfg.on_audio = on_core_audio;  // game audio; the test-tone build plays tones instead
#endif
  return cfg;
}
#endif

#ifdef PB_BOOT_BENCH
void bench_discard_audio(void *, const int16_t *, uint32_t) {}

// Runs the first frames of the ROM with no input (deterministic, so variants of the firmware
// can be compared) in three configurations and prints the average and worst frame time per
// segment of BENCH_SEG frames:
//   A: drawing + audio (the real cost)   B: no drawing, audio   C: no drawing, no audio
// A-B is the picture cost, B-C the audio cost, C the CPU emulation.
void run_boot_bench() {
  constexpr int SEGMENTS = 6, BENCH_SEG = 300;
  // Wait for a serial reader so the numbers are not lost (the screen stays black meanwhile).
  const uint32_t wait_start = time_us_32();
  while (!stdio_usb_connected() && time_us_32() - wait_start < 120000000u) sleep_ms(100);
  sleep_ms(500);
  printf("bootbench: A=draw+audio  B=audio only  C=neither; %d frames per segment, microseconds per frame\n",
         BENCH_SEG);
  for (int pass = 0; pass < 3; pass++) {
    const bool render = pass == 0;
    pb_core_config_t cfg = make_core_config();
    cfg.on_audio = pass < 2 ? bench_discard_audio : nullptr;
    if (pb_core_init(&g_core, &cfg) != PB_OK) {
      printf("bootbench: init failed\n");
      return;
    }
    pb_core_set_rendering(&g_core, render);
    uint32_t apu_avg, apu_max, apu_n;
    for (int seg = 0; seg < SEGMENTS; seg++) {
      uint64_t sum = 0;
      uint32_t mx = 0, mn = 0xFFFFFFFF;
      for (int f = 0; f < BENCH_SEG; f++) {
        const uint32_t t0 = time_us_32();
        pb_core_run_frame(&g_core);
        const uint32_t dt = time_us_32() - t0;
        sum += dt;
        if (dt > mx) mx = dt;
        if (dt < mn) mn = dt;
      }
      pb_core_take_apu_stats(&g_core, &apu_avg, &apu_max, &apu_n);
      printf("bootbench %c frames %4d-%4d: avg=%5u min=%5u max=%5u apu_avg=%u\n", 'A' + pass,
             seg * BENCH_SEG, (seg + 1) * BENCH_SEG - 1, (unsigned)(sum / BENCH_SEG),
             (unsigned)mn, (unsigned)mx, (unsigned)apu_avg);
    }
  }
  printf("bootbench: done\n");
}
#endif
}  // namespace

void init() {
  stdio_init_all();
  printf("PicoBoy Phase 1\n");
  display_init();
  audio_init();

#ifdef PB_HAVE_EMBEDDED_ROM
#ifdef PB_BOOT_BENCH
  run_boot_bench();
#endif
  pb_core_config_t cfg = make_core_config();
  pb_status_t st = pb_core_init(&g_core, &cfg);
  if (st == PB_OK) {
    g_running = true;
    pb_pacer_init(&g_pacer);
    printf("ROM: %u bytes, cart RAM needed: %u\n", (unsigned)cfg.rom_size,
           (unsigned)pb_core_cart_ram_needed(&g_core));
  } else {
    g_error = "core init failed";
    printf("pb_core_init failed: %d\n", (int)st);
  }
#else
  g_error = "no ROM embedded (see tools/fetch_test_roms.sh)";
  printf("%s\n", g_error);
#endif
}

void update(uint32_t) {
  if (!g_running) return;
  const uint32_t start = time_us();
  if (g_last_start_us) g_period_us.add(start - g_last_start_us);
  g_last_start_us = start;
  if (g_updates == 0) g_window_start_us = start;

  pb_core_set_buttons(&g_core, input_poll());
  if (input_menu_requested()) printf("menu requested (menu not implemented yet)\n");
  if (input_mute_toggle_requested()) {
    audio_set_enabled(!audio_enabled());
    printf("audio %s\n", audio_enabled() ? "on" : "muted");
  }

  // Run the frames that are due, as many as fit the time budget; draw only the last one, and
  // only if there is time for that too (see pb_pacer_plan).
  const pb_plan_t plan = pb_pacer_plan(&g_pacer, start, g_cost_norender_us, g_cost_render_us,
                                       UPDATE_BUDGET_US);
  for (uint32_t i = 0; i < plan.frames; i++) {
    const bool render = plan.render && i == plan.frames - 1;
    pb_core_set_rendering(&g_core, render);
    const uint32_t t0 = time_us();
    pb_core_run_frame(&g_core);
#ifdef PB_AUDIO_TEST_TONE
    audio_push_test_tone();
#endif
    const uint32_t dt = time_us() - t0;
    g_emu_us.add(dt);
    track_cost(render ? g_cost_render_us : g_cost_norender_us, dt);
  }
  g_emulated += plan.frames;
  g_dropped += plan.dropped;
  if (plan.frames && plan.render) g_drawn_updates++;
  g_update_us.add(time_us() - start);
  g_flip_us.add(stats.flip_us);
  if (g_updates % 80 == 40) audio_print_diag();  // about every 3 s

  if (++g_updates == BENCH_UPDATES) {
    const uint32_t elapsed_us = time_us() - g_window_start_us;
    const uint32_t fps_x100 = (uint32_t)(((uint64_t)g_emulated * 100000000u) / elapsed_us);
    const uint32_t refresh_x100 = (uint32_t)(((uint64_t)g_updates * 100000000u) / elapsed_us);
    printf("bench updates=%u emulated=%u gb_fps=%u.%02u screen_hz=%u.%02u drawn=%u dropped=%u"
           " est_nr=%u est_r=%u\n",
           (unsigned)g_updates, (unsigned)g_emulated, (unsigned)(fps_x100 / 100),
           (unsigned)(fps_x100 % 100), (unsigned)(refresh_x100 / 100),
           (unsigned)(refresh_x100 % 100), (unsigned)g_drawn_updates, (unsigned)g_dropped,
           (unsigned)g_cost_norender_us, (unsigned)g_cost_render_us);
    printf("  emu_us    min=%u avg=%u max=%u\n", (unsigned)g_emu_us.min,
           (unsigned)g_emu_us.avg(), (unsigned)g_emu_us.max);
    printf("  update_us avg=%u max=%u | flip_us avg=%u max=%u | period_us avg=%u max=%u\n",
           (unsigned)g_update_us.avg(), (unsigned)g_update_us.max,
           (unsigned)g_flip_us.avg(), (unsigned)g_flip_us.max,
           (unsigned)g_period_us.avg(), (unsigned)g_period_us.max);
    printf("  blit_us   avg=%u max=%u\n", (unsigned)g_blit_us.avg(), (unsigned)g_blit_us.max);
    uint32_t apu_avg = 0, apu_max = 0, apu_n = 0;
    pb_core_take_apu_stats(&g_core, &apu_avg, &apu_max, &apu_n);
    printf("  apu_us    avg=%u max=%u (frames=%u; part of emu_us)\n", (unsigned)apu_avg,
           (unsigned)apu_max, (unsigned)apu_n);
    const audio_stats_t au = audio_get_stats();
    printf("  audio %s fill=%d underruns=%u overruns=%u drops=%u dups=%u\n",
           audio_enabled() ? "on" : "muted", (int)au.fill, (unsigned)au.underruns,
           (unsigned)au.overruns, (unsigned)au.drops, (unsigned)au.dups);
    g_emu_us.reset();
    g_update_us.reset();
    g_period_us.reset();
    g_flip_us.reset();
    g_blit_us.reset();
    g_updates = 0;
    g_emulated = 0;
    g_drawn_updates = 0;
    g_dropped = 0;
  }
}

void draw(uint32_t) {
  if (!g_running) {
    pen(0, 0, 0);
    clear();
    pen(15, 15, 15);
    text("PicoBoy", 84, 100);
    pen(15, 6, 6);
    text(g_error ? g_error : "not running", 10, 120, 220);
    return;
  }
  const uint32_t t0 = time_us();
  display_present();
  g_blit_us.add(time_us() - t0);
}
