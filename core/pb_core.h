// pb_core: portable adapter around Peanut-GB (see docs/PLAN.md section 3).
// No Pico or Pimoroni headers here. The ROM is read in place through a pointer
// (XIP on the device); cart RAM lives in a caller-provided RAM buffer.
#ifndef PB_CORE_H
#define PB_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Buttons, active-high in our API (the adapter converts to Peanut-GB's active-low).
#define PB_BTN_A      0x01u
#define PB_BTN_B      0x02u
#define PB_BTN_SELECT 0x04u
#define PB_BTN_START  0x08u
#define PB_BTN_RIGHT  0x10u
#define PB_BTN_LEFT   0x20u
#define PB_BTN_UP     0x40u
#define PB_BTN_DOWN   0x80u

#define PB_SCREEN_W 160
#define PB_SCREEN_H 144

// Result of pb_core_init().
typedef enum {
  PB_OK = 0,
  PB_ERR_BAD_ARGS,         // null pointer, zero size, missing callback
  PB_ERR_UNSUPPORTED_CART, // mapper not supported by the emulator core
  PB_ERR_BAD_HEADER,       // ROM header checksum is wrong
  PB_ERR_CART_RAM_TOO_SMALL,
} pb_status_t;

// Runtime errors reported through on_error (values mirror Peanut-GB's gb_error_e,
// plus our own out-of-range codes).
typedef enum {
  PB_RT_UNKNOWN = 0,
  PB_RT_INVALID_OPCODE = 1,
  PB_RT_INVALID_READ = 2,
  PB_RT_INVALID_WRITE = 3,
  PB_RT_ROM_OUT_OF_RANGE = 100,      // addr = low 16 bits of the offending ROM offset
  PB_RT_CART_RAM_OUT_OF_RANGE = 101, // addr = low 16 bits of the offending RAM offset
} pb_runtime_error_t;

typedef struct {
  const uint8_t *rom;      // read-only, must stay valid while the core runs
  uint32_t rom_size;       // bytes
  uint8_t *cart_ram;       // RAM-resident save buffer; may be NULL if cart_ram_size == 0
  uint32_t cart_ram_size;  // bytes; must be >= the size the cartridge needs

  // Called once per scanline (line 0..143) with 160 pixels. Per pixel: bits 1-0 =
  // shade (0 lightest .. 3 darkest), bits 5-4 = palette (OBJ0 0b00, OBJ1 0b01, BG 0b10).
  void (*draw_line)(void *user, const uint8_t px[PB_SCREEN_W], uint8_t line);
  // Called on emulation errors. Optional (may be NULL).
  void (*on_error)(void *user, int code, uint16_t addr);
  // Called for each byte the game sends on the serial port. Optional; used by tests
  // (Blargg test ROMs print their result over serial).
  void (*on_serial_tx)(void *user, uint8_t byte);
  void *user;
} pb_core_config_t;

// Opaque to callers; storage is provided by the caller so nothing is heap-allocated.
// PB_CORE_STORAGE_BYTES is checked against the real size at compile time in pb_core.c.
#ifndef PB_CORE_STORAGE_BYTES
#define PB_CORE_STORAGE_BYTES 20480
#endif
#ifdef __cplusplus
#define PB_ALIGN8 alignas(8)
#else
#define PB_ALIGN8 _Alignas(8)
#endif
typedef struct pb_core {
  PB_ALIGN8 uint8_t storage[PB_CORE_STORAGE_BYTES];
} pb_core_t;

pb_status_t pb_core_init(pb_core_t *c, const pb_core_config_t *cfg);
void pb_core_set_buttons(pb_core_t *c, uint8_t mask);  // PB_BTN_* bits, 1 = pressed
void pb_core_run_frame(pb_core_t *c);                  // runs until one frame is complete

// Cart RAM bytes the loaded cartridge needs (valid after a successful pb_core_init).
uint32_t pb_core_cart_ram_needed(const pb_core_t *c);

// Dirty tracking for the save store. Set when a cart RAM write changes a byte.
bool pb_core_cart_ram_dirty(const pb_core_t *c);
void pb_core_cart_ram_clear_dirty(pb_core_t *c);

#ifdef __cplusplus
}
#endif

#endif  // PB_CORE_H
