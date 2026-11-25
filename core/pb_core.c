// pb_core: adapter around Peanut-GB. The only file that includes peanut_gb.h.
#include "pb_core.h"

#include <string.h>

// Compile-time configuration of the core (sound is added in Phase 5).
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#include "peanut_gb.h"

typedef struct {
  struct gb_s gb;
  pb_core_config_t cfg;
  bool dirty;
} impl_t;

_Static_assert(sizeof(impl_t) <= sizeof(pb_core_t),
               "PB_CORE_STORAGE_BYTES too small for the emulator state");
_Static_assert(_Alignof(impl_t) <= 8, "pb_core_t alignment too small");

static inline impl_t *impl(pb_core_t *c) { return (impl_t *)(void *)c->storage; }
static inline const impl_t *impl_c(const pb_core_t *c) {
  return (const impl_t *)(const void *)c->storage;
}
static inline impl_t *from_gb(struct gb_s *gb) { return (impl_t *)gb->direct.priv; }

static void report(impl_t *m, int code, uint16_t addr) {
  if (m->cfg.on_error) m->cfg.on_error(m->cfg.user, code, addr);
}

static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr) {
  impl_t *m = from_gb(gb);
  if (addr >= m->cfg.rom_size) {
    report(m, PB_RT_ROM_OUT_OF_RANGE, (uint16_t)addr);
    return 0xFF;
  }
  return m->cfg.rom[addr];
}

static uint8_t cart_ram_read(struct gb_s *gb, const uint_fast32_t addr) {
  impl_t *m = from_gb(gb);
  if (addr >= m->cfg.cart_ram_size) {
    report(m, PB_RT_CART_RAM_OUT_OF_RANGE, (uint16_t)addr);
    return 0xFF;
  }
  return m->cfg.cart_ram[addr];
}

static void cart_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
  impl_t *m = from_gb(gb);
  if (addr >= m->cfg.cart_ram_size) {
    report(m, PB_RT_CART_RAM_OUT_OF_RANGE, (uint16_t)addr);
    return;
  }
  // Only a real change makes the save dirty (games rewrite identical values often).
  if (m->cfg.cart_ram[addr] != val) {
    m->cfg.cart_ram[addr] = val;
    m->dirty = true;
  }
}

static void gb_error_cb(struct gb_s *gb, const enum gb_error_e err, const uint16_t addr) {
  report(from_gb(gb), (int)err, addr);
}

static void draw_line_cb(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line) {
  impl_t *m = from_gb(gb);
  m->cfg.draw_line(m->cfg.user, pixels, (uint8_t)line);
}

static void serial_tx_cb(struct gb_s *gb, const uint8_t byte) {
  impl_t *m = from_gb(gb);
  if (m->cfg.on_serial_tx) m->cfg.on_serial_tx(m->cfg.user, byte);
}

pb_status_t pb_core_init(pb_core_t *c, const pb_core_config_t *cfg) {
  if (!c || !cfg || !cfg->rom || cfg->rom_size < 0x150 || !cfg->draw_line) return PB_ERR_BAD_ARGS;
  if (cfg->cart_ram_size > 0 && !cfg->cart_ram) return PB_ERR_BAD_ARGS;

  impl_t *m = impl(c);
  memset(m, 0, sizeof(*m));
  m->cfg = *cfg;

  enum gb_init_error_e e =
      gb_init(&m->gb, rom_read, cart_ram_read, cart_ram_write, gb_error_cb, m);
  if (e == GB_INIT_CARTRIDGE_UNSUPPORTED) return PB_ERR_UNSUPPORTED_CART;
  if (e != GB_INIT_NO_ERROR) return PB_ERR_BAD_HEADER;

  if (m->cfg.cart_ram_size < gb_get_save_size(&m->gb)) return PB_ERR_CART_RAM_TOO_SMALL;

  gb_init_lcd(&m->gb, draw_line_cb);
  gb_init_serial(&m->gb, serial_tx_cb, NULL);
  return PB_OK;
}

void pb_core_set_buttons(pb_core_t *c, uint8_t mask) {
  // Peanut-GB: bit set = released. Our API: bit set = pressed.
  impl(c)->gb.direct.joypad = (uint8_t)~mask;
}

void pb_core_run_frame(pb_core_t *c) { gb_run_frame(&impl(c)->gb); }

void pb_core_set_rendering(pb_core_t *c, bool on) {
  // Peanut-GB skips all pixel work when the line callback is NULL.
  impl(c)->gb.display.lcd_draw_line = on ? draw_line_cb : NULL;
}

uint32_t pb_core_cart_ram_needed(const pb_core_t *c) {
  // gb_get_save_size() takes a non-const context but does not modify it.
  return (uint32_t)gb_get_save_size((struct gb_s *)&impl_c(c)->gb);
}

bool pb_core_cart_ram_dirty(const pb_core_t *c) { return impl_c(c)->dirty; }
void pb_core_cart_ram_clear_dirty(pb_core_t *c) { impl(c)->dirty = false; }
