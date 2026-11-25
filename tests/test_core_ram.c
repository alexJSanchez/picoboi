// Host test for the cart RAM change tracking in core/pb_core: a tiny ROM of our own (no Nintendo
// data) enables the RAM of an MBC1 cartridge and writes to it; the dirty flag and the generation
// counter must follow only the writes that change a byte.
#include <stdio.h>
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

static uint8_t g_rom[0x8000];
static uint8_t g_ram[8192];
static pb_core_t g_core;

static void on_line(void *u, const uint8_t px[PB_SCREEN_W], uint8_t line) { (void)u; (void)px; (void)line; }

int main(void) {
  memset(g_rom, 0, sizeof(g_rom));
  g_rom[0x100] = 0x00; g_rom[0x101] = 0xC3; g_rom[0x102] = 0x50; g_rom[0x103] = 0x01;  // NOP; JP 0150
  memcpy(&g_rom[0x134], "RAMTEST", 7);
  g_rom[0x147] = 0x03;  // MBC1 + RAM + battery
  g_rom[0x148] = 0x00;  // 32 KiB ROM
  g_rom[0x149] = 0x02;  // 8 KiB RAM
  uint8_t chk = 0;
  for (int i = 0x134; i <= 0x14C; i++) chk = (uint8_t)(chk - g_rom[i] - 1);
  g_rom[0x14D] = chk;
  static const uint8_t prog[] = {
      0x3E, 0x0A, 0xEA, 0x00, 0x00,  // LD A,0x0A ; LD (0x0000),A   enable cart RAM
      0x3E, 0x55, 0xEA, 0x00, 0xA0,  // LD A,0x55 ; LD (0xA000),A   change byte 0 (0x00 -> 0x55)
      0xEA, 0x00, 0xA0,              // LD (0xA000),A               same value again: no change
      0xEA, 0x01, 0xA0,              // LD (0xA001),A               change byte 1 (0x00 -> 0x55)
      0x3E, 0x56, 0xEA, 0x00, 0xA0,  // LD A,0x56 ; LD (0xA000),A   change byte 0 again
      0x18, 0xFE};                   // JR -2 (loop forever)
  memcpy(&g_rom[0x150], prog, sizeof(prog));

  pb_core_config_t cfg = {.rom = g_rom, .rom_size = sizeof(g_rom), .cart_ram = g_ram,
                          .cart_ram_size = sizeof(g_ram), .draw_line = on_line};
  CHECK(pb_core_init(&g_core, &cfg) == PB_OK);
  CHECK(!pb_core_cart_ram_dirty(&g_core));
  CHECK(pb_core_cart_ram_generation(&g_core) == 0);

  pb_core_run_frame(&g_core);
  CHECK(g_ram[0] == 0x56 && g_ram[1] == 0x55);
  CHECK(pb_core_cart_ram_dirty(&g_core));
  CHECK(pb_core_cart_ram_generation(&g_core) == 3);  // three writes changed a byte; the repeated one did not

  pb_core_cart_ram_clear_dirty(&g_core);
  CHECK(!pb_core_cart_ram_dirty(&g_core));
  CHECK(pb_core_cart_ram_generation(&g_core) == 3);  // clearing the flag does not touch the counter
  for (int i = 0; i < 5; i++) pb_core_run_frame(&g_core);
  CHECK(pb_core_cart_ram_generation(&g_core) == 3);  // the program loops without writing
  CHECK(!pb_core_cart_ram_dirty(&g_core));

  if (failures == 0) printf("core cart RAM tests passed\n");
  return failures ? 1 : 0;
}
