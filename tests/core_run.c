// Headless host runner for core/: loads a ROM file, runs frames, collects serial
// output and optionally dumps the last frame as a BMP.
//
//   core_run <rom.gb> <max_frames> [--expect TEXT] [--dump out.bmp]
//
// Exit codes: 0 ok (and TEXT seen, if --expect given), 1 test failed or timed out,
// 2 usage / init error, 77 ROM file missing (ctest reports "skipped").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pb_core.h"

#define SERIAL_CAP 4096

typedef struct {
  uint8_t fb[PB_SCREEN_W * PB_SCREEN_H];  // shade 0..3 per pixel
  char serial[SERIAL_CAP];
  size_t serial_len;
  int errors;
} ctx_t;

static void on_line(void *user, const uint8_t px[PB_SCREEN_W], uint8_t line) {
  ctx_t *c = (ctx_t *)user;
  if (line >= PB_SCREEN_H) return;
  for (int x = 0; x < PB_SCREEN_W; x++) c->fb[line * PB_SCREEN_W + x] = px[x] & 3;
}

static void on_error(void *user, int code, uint16_t addr) {
  ctx_t *c = (ctx_t *)user;
  if (c->errors++ < 5) fprintf(stderr, "core error %d at 0x%04x\n", code, addr);
}

static void on_serial(void *user, uint8_t byte) {
  ctx_t *c = (ctx_t *)user;
  if (c->serial_len < SERIAL_CAP - 1) {
    c->serial[c->serial_len++] = (char)byte;
    c->serial[c->serial_len] = 0;
  }
}

static uint8_t *read_file(const char *path, uint32_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *buf = (uint8_t *)malloc((size_t)n);
  if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  *size = (uint32_t)n;
  return buf;
}

// 24-bit BMP, classic DMG green shades.
static int write_bmp(const char *path, const uint8_t *fb) {
  static const uint8_t pal[4][3] = {{224, 248, 208}, {136, 192, 112}, {52, 104, 86}, {8, 24, 32}};
  const int w = PB_SCREEN_W, h = PB_SCREEN_H;
  const int row = (w * 3 + 3) & ~3;
  const uint32_t data = (uint32_t)(row * h), total = 54 + data;
  uint8_t hdr[54] = {'B', 'M'};
  memcpy(hdr + 2, &total, 4);
  hdr[10] = 54;
  hdr[14] = 40;
  memcpy(hdr + 18, &w, 4);
  memcpy(hdr + 22, &h, 4);
  hdr[26] = 1;
  hdr[28] = 24;
  memcpy(hdr + 34, &data, 4);
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  fwrite(hdr, 1, 54, f);
  uint8_t *line = (uint8_t *)calloc(1, (size_t)row);
  for (int y = h - 1; y >= 0; y--) {
    for (int x = 0; x < w; x++) {
      const uint8_t *p = pal[fb[y * w + x] & 3];
      line[x * 3 + 0] = p[2];
      line[x * 3 + 1] = p[1];
      line[x * 3 + 2] = p[0];
    }
    fwrite(line, 1, (size_t)row, f);
  }
  free(line);
  fclose(f);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <rom.gb> <max_frames> [--expect TEXT] [--dump out.bmp]\n", argv[0]);
    return 2;
  }
  const char *expect = NULL, *dump = NULL;
  for (int i = 3; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--expect")) expect = argv[i + 1];
    else if (!strcmp(argv[i], "--dump")) dump = argv[i + 1];
    else {
      fprintf(stderr, "unknown option %s\n", argv[i]);
      return 2;
    }
  }
  const long max_frames = atol(argv[2]);

  uint32_t rom_size = 0;
  uint8_t *rom = read_file(argv[1], &rom_size);
  if (!rom) {
    fprintf(stderr, "SKIP: cannot read %s (run tools/fetch_test_roms.sh)\n", argv[1]);
    return 77;
  }

  static ctx_t ctx;
  static pb_core_t core;
  static uint8_t cart_ram[0x20000];
  pb_core_config_t cfg = {.rom = rom,
                          .rom_size = rom_size,
                          .cart_ram = cart_ram,
                          .cart_ram_size = sizeof(cart_ram),
                          .draw_line = on_line,
                          .on_error = on_error,
                          .on_serial_tx = on_serial,
                          .user = &ctx};
  pb_status_t st = pb_core_init(&core, &cfg);
  if (st != PB_OK) {
    fprintf(stderr, "pb_core_init failed: %d\n", (int)st);
    return 2;
  }

  long frame = 0;
  int found = 0, failed = 0;
  for (; frame < max_frames; frame++) {
    pb_core_run_frame(&core);
    if (expect && strstr(ctx.serial, expect)) { found = 1; break; }
    if (strstr(ctx.serial, "Failed")) { failed = 1; break; }
  }

  printf("frames run: %ld\n", frame + 1);
  if (ctx.serial_len) printf("serial output:\n%s\n", ctx.serial);
  if (ctx.errors) printf("core errors: %d\n", ctx.errors);
  if (dump && write_bmp(dump, ctx.fb) == 0) printf("wrote %s\n", dump);

  free(rom);
  if (failed) return 1;
  if (expect) return found ? 0 : 1;
  return ctx.errors ? 1 : 0;
}
