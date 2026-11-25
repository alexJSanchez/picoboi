// Run an embedded free test ROM through core/ and show it on screen.
// The emulator runs inside the SDK's draw(): the SDK loop waits for the previous
// framebuffer transfer to finish before draw() and for vsync before the next flip.
// The panel refreshes at 40 Hz (SDK setting) but the Game Boy runs at 59.73 Hz, so
// emulation is paced by time (pb_pacer): each refresh runs however many frames are due
// (1, 2, 1, 2... at 40 Hz) and only the last of them is rendered.
#include <stdio.h>

#include "pico/stdlib.h"
#include "picosystem.hpp"

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

// Statistics, printed over USB serial every BENCH_DRAWS display refreshes.
constexpr uint32_t BENCH_DRAWS = 600;
struct Stat {
  uint32_t min = 0xFFFFFFFF, max = 0;
  uint64_t sum = 0;
  void add(uint32_t v) {
    if (v < min) min = v;
    if (v > max) max = v;
    sum += v;
  }
  void reset() { *this = Stat(); }
};
Stat g_emu_us;     // time to emulate one Game Boy frame
Stat g_draw_us;    // time spent in draw() emulating, per display refresh
Stat g_period_us;  // time between display refreshes
Stat g_flip_us;    // SDK: time from flip start until the transfer finished
uint32_t g_draws = 0;
uint32_t g_emulated = 0;      // Game Boy frames emulated in this window
uint32_t g_window_start_us = 0;
uint32_t g_last_draw_us = 0;

void on_core_error(void *, int code, uint16_t addr) {
  if (g_error_count++ < 5) printf("core error %d at 0x%04x\n", code, addr);
}

}  // namespace

void init() {
  stdio_init_all();
  printf("PicoBoy Phase 1\n");
  display_init();

#ifdef PB_HAVE_EMBEDDED_ROM
  pb_core_config_t cfg = {};
  cfg.rom = pb_embedded_rom;
  cfg.rom_size = (uint32_t)(pb_embedded_rom_end - pb_embedded_rom);
  cfg.cart_ram = g_cart_ram;
  cfg.cart_ram_size = sizeof(g_cart_ram);
  cfg.draw_line = display_draw_line;
  cfg.on_error = on_core_error;
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
  pb_core_set_buttons(&g_core, input_poll());
  if (input_menu_requested()) printf("menu requested (menu not implemented yet)\n");
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

  const uint32_t draw_start = time_us();
  if (g_last_draw_us) g_period_us.add(draw_start - g_last_draw_us);
  g_last_draw_us = draw_start;
  if (g_draws == 0) g_window_start_us = draw_start;

  const uint32_t due = pb_pacer_frames_due(&g_pacer, draw_start);
  for (uint32_t i = 0; i < due; i++) {
    pb_core_set_rendering(&g_core, i == due - 1);  // show only the newest frame
    const uint32_t t0 = time_us();
    pb_core_run_frame(&g_core);
    g_emu_us.add(time_us() - t0);
  }
  g_emulated += due;
  g_draw_us.add(time_us() - draw_start);
  g_flip_us.add(stats.flip_us);

  if (++g_draws == BENCH_DRAWS) {
    const uint32_t elapsed_us = time_us() - g_window_start_us;
    const uint32_t fps_x100 = (uint32_t)(((uint64_t)g_emulated * 100000000u) / elapsed_us);
    printf("bench draws=%u emulated=%u gb_fps=%u.%02u | emu_us min=%u avg=%u max=%u | "
           "draw_us max=%u | period_us avg=%u max=%u\n",
           (unsigned)g_draws, (unsigned)g_emulated, (unsigned)(fps_x100 / 100),
           (unsigned)(fps_x100 % 100), (unsigned)g_emu_us.min,
           (unsigned)(g_emulated ? g_emu_us.sum / g_emulated : 0), (unsigned)g_emu_us.max,
           (unsigned)g_draw_us.max, (unsigned)(g_flip_us.sum / g_draws), (unsigned)g_flip_us.max,
           (unsigned)(g_period_us.sum / (g_draws - 1)),
           (unsigned)g_period_us.max);
    g_emu_us.reset();
    g_draw_us.reset();
    g_period_us.reset();
    g_flip_us.reset();
    g_draws = 0;
    g_emulated = 0;
  }
}
