// Phase 1: run an embedded free test ROM through core/ and show it on screen.
// The emulator runs inside the SDK's draw(): the SDK loop waits for the previous
// framebuffer transfer to finish before draw() and for vsync before the next flip, so
// one draw() is one display refresh (this is also what paces emulation).
#include <stdio.h>

#include "pico/stdlib.h"
#include "picosystem.hpp"

#include "display.h"
#include "input.h"
#include "pb_core.h"

using namespace picosystem;

#ifdef PB_HAVE_EMBEDDED_ROM
extern "C" const uint8_t pb_embedded_rom[];
extern "C" const uint8_t pb_embedded_rom_end[];
#endif

namespace {

pb_core_t g_core;
uint8_t g_cart_ram[32 * 1024];  // sized for Pokemon Red/Blue/Yellow (PLAN.md section 4)

bool g_running = false;
const char *g_error = nullptr;
uint32_t g_error_count = 0;

// Frame-time statistics, printed over USB serial every BENCH_FRAMES frames.
constexpr uint32_t BENCH_FRAMES = 600;
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
Stat g_emu_us, g_period_us;
uint32_t g_bench_n = 0;
uint32_t g_last_start_us = 0;

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

  uint32_t t0 = time_us();
  pb_core_run_frame(&g_core);
  uint32_t t1 = time_us();

  if (g_last_start_us) g_period_us.add(t0 - g_last_start_us);
  g_last_start_us = t0;
  g_emu_us.add(t1 - t0);

  if (++g_bench_n == BENCH_FRAMES) {
    printf("bench frames=%u emu_us min=%u avg=%u max=%u | period_us min=%u avg=%u max=%u\n",
           (unsigned)BENCH_FRAMES, (unsigned)g_emu_us.min,
           (unsigned)(g_emu_us.sum / BENCH_FRAMES), (unsigned)g_emu_us.max,
           (unsigned)g_period_us.min, (unsigned)(g_period_us.sum / (BENCH_FRAMES - 1)),
           (unsigned)g_period_us.max);
    g_emu_us.reset();
    g_period_us.reset();
    g_bench_n = 0;
    g_last_start_us = 0;
  }
}
