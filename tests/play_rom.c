// play_rom: a developer tool (not a test) that runs a ROM on the PC with the same emulator core as
// the firmware, presses buttons from a script, saves screenshots and reports what happened. Used
// for the compatibility pass (docs/COMPAT.md).
//
//   play_rom <rom.gb> <frames> [--press SPEC]... [--shot FRAME:file.bmp]...
//
//   SPEC   BUTTON@START[+HOLD][%PERIOD][~END]   BUTTON is A B START SELECT UP DOWN LEFT RIGHT.
//          Pressed from frame START for HOLD frames (default 4). With %PERIOD the press repeats every
//          PERIOD frames until frame END. Example: A@600+3%20~3000 taps A every 20 frames.
//   --shot FRAME:file.bmp   write the picture as it is after that frame (24-bit BMP, device palette).
//
// Prints: core errors, how many times the cartridge RAM was changed, how much of the sound was not
// silent, and the serial output (test ROMs print their result there).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pb_core.h"

#define MAX_PRESS 64
#define MAX_SHOT 64

typedef struct {
  uint8_t mask;
  long start, hold, period, end;
} press_t;

typedef struct {
  long frame;
  const char *path;
} shot_t;

static uint8_t g_fb[PB_SCREEN_W * PB_SCREEN_H];
static char g_serial[4096];
static size_t g_serial_len;
static int g_errors;
static int g_first_errors[8][2];
static int g_audio_peak;
static long g_audio_frames_loud;
static long g_audio_frames;

// Cartridge RAM activity, tracked per 8 KiB bank: how many frames changed the bank, how many byte
// changes in total, and the first and last frame in which it changed.
#define MAX_BANKS 16
static long g_bank_frames[MAX_BANKS], g_bank_bytes[MAX_BANKS], g_bank_first[MAX_BANKS], g_bank_last[MAX_BANKS];

static void on_line(void *u, const uint8_t px[PB_SCREEN_W], uint8_t line) {
  (void)u;
  if (line < PB_SCREEN_H)
    for (int x = 0; x < PB_SCREEN_W; x++) g_fb[line * PB_SCREEN_W + x] = px[x] & 3;
}
static void on_error(void *u, int code, uint16_t addr) {
  (void)u;
  if (g_errors < 8) { g_first_errors[g_errors][0] = code; g_first_errors[g_errors][1] = addr; }
  g_errors++;
}
static void on_serial(void *u, uint8_t b) {
  (void)u;
  if (g_serial_len < sizeof(g_serial) - 1) { g_serial[g_serial_len++] = (char)b; g_serial[g_serial_len] = 0; }
}
static void on_audio(void *u, const int16_t *mono, uint32_t n) {
  (void)u;
  int peak = 0;
  for (uint32_t i = 0; i < n; i++) {
    int v = abs(mono[i]);
    if (v > peak) peak = v;
  }
  if (peak > g_audio_peak) g_audio_peak = peak;
  g_audio_frames++;
  if (peak > 200) g_audio_frames_loud++;
}

static int write_bmp(const char *path) {
  // The device's palette: 4 bits per channel (13,15,12) (8,11,7) (3,6,5) (0,1,2) times 17.
  static const uint8_t pal[4][3] = {{221, 255, 204}, {136, 187, 119}, {51, 102, 85}, {0, 17, 34}};
  const int w = PB_SCREEN_W, h = PB_SCREEN_H, row = (w * 3 + 3) & ~3;
  const uint32_t data = (uint32_t)(row * h), total = 54 + data;
  uint8_t hdr[54] = {'B', 'M'};
  memcpy(hdr + 2, &total, 4);
  hdr[10] = 54; hdr[14] = 40;
  memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
  hdr[26] = 1; hdr[28] = 24;
  memcpy(hdr + 34, &data, 4);
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  fwrite(hdr, 1, 54, f);
  uint8_t *line = (uint8_t *)calloc(1, (size_t)row);
  for (int y = h - 1; y >= 0; y--) {
    for (int x = 0; x < w; x++) {
      const uint8_t *p = pal[g_fb[y * w + x] & 3];
      line[x * 3 + 0] = p[2]; line[x * 3 + 1] = p[1]; line[x * 3 + 2] = p[0];
    }
    fwrite(line, 1, (size_t)row, f);
  }
  free(line);
  fclose(f);
  return 0;
}

static uint8_t button_mask(const char *name) {
  static const struct { const char *n; uint8_t m; } t[] = {
      {"A", PB_BTN_A}, {"B", PB_BTN_B}, {"START", PB_BTN_START}, {"SELECT", PB_BTN_SELECT},
      {"UP", PB_BTN_UP}, {"DOWN", PB_BTN_DOWN}, {"LEFT", PB_BTN_LEFT}, {"RIGHT", PB_BTN_RIGHT}};
  for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++)
    if (!strcmp(name, t[i].n)) return t[i].m;
  return 0;
}

static int parse_press(const char *spec, press_t *p) {
  char name[16];
  const char *at = strchr(spec, '@');
  if (!at || at - spec >= (long)sizeof(name)) return -1;
  memcpy(name, spec, (size_t)(at - spec));
  name[at - spec] = 0;
  p->mask = button_mask(name);
  if (!p->mask) return -1;
  p->start = strtol(at + 1, NULL, 10);
  p->hold = 4;
  p->period = 0;
  p->end = 0x7FFFFFFF;
  const char *c;
  if ((c = strchr(at, '+'))) p->hold = strtol(c + 1, NULL, 10);
  if ((c = strchr(at, '%'))) p->period = strtol(c + 1, NULL, 10);
  if ((c = strchr(at, '~'))) p->end = strtol(c + 1, NULL, 10);
  return 0;
}

static uint8_t buttons_at(const press_t *p, int n, long f) {
  uint8_t m = 0;
  for (int i = 0; i < n; i++) {
    if (f < p[i].start || f >= p[i].end) continue;
    const long rel = f - p[i].start;
    if (p[i].period > 0 ? (rel % p[i].period) < p[i].hold : rel < p[i].hold) m |= p[i].mask;
  }
  return m;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <rom.gb> <frames> [--press SPEC]... [--shot FRAME:file.bmp]...\n", argv[0]);
    return 2;
  }
  static press_t presses[MAX_PRESS];
  static shot_t shots[MAX_SHOT];
  int n_press = 0, n_shot = 0;
  for (int i = 3; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--press") && n_press < MAX_PRESS) {
      if (parse_press(argv[i + 1], &presses[n_press++]) != 0) { fprintf(stderr, "bad --press %s\n", argv[i + 1]); return 2; }
    } else if (!strcmp(argv[i], "--shot") && n_shot < MAX_SHOT) {
      char *colon = strchr(argv[i + 1], ':');
      if (!colon) { fprintf(stderr, "bad --shot %s\n", argv[i + 1]); return 2; }
      shots[n_shot].frame = strtol(argv[i + 1], NULL, 10);
      shots[n_shot].path = colon + 1;
      n_shot++;
    } else {
      fprintf(stderr, "unknown option %s\n", argv[i]);
      return 2;
    }
  }
  const long frames = atol(argv[2]);

  FILE *f = fopen(argv[1], "rb");
  if (!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *rom = (uint8_t *)malloc((size_t)size);
  if (fread(rom, 1, (size_t)size, f) != (size_t)size) { fprintf(stderr, "read failed\n"); return 2; }
  fclose(f);

  static pb_core_t core;
  static uint8_t ram[0x20000];
  pb_core_config_t cfg = {.rom = rom, .rom_size = (uint32_t)size, .cart_ram = ram, .cart_ram_size = sizeof(ram),
                          .draw_line = on_line, .on_error = on_error, .on_serial_tx = on_serial, .on_audio = on_audio};
  const pb_status_t st = pb_core_init(&core, &cfg);
  if (st != PB_OK) { printf("init failed: status %d\n", (int)st); return 1; }
  printf("ROM %ld bytes, cart RAM needed %u bytes\n", size, (unsigned)pb_core_cart_ram_needed(&core));

  const uint32_t ram_bytes = pb_core_cart_ram_needed(&core);
  static uint8_t prev_ram[0x20000];
  for (int b = 0; b < MAX_BANKS; b++) g_bank_first[b] = -1;
  for (long fr = 0; fr < frames; fr++) {
    pb_core_set_buttons(&core, buttons_at(presses, n_press, fr));
    pb_core_run_frame(&core);
    for (uint32_t bank = 0; bank * 0x2000 < ram_bytes && bank < MAX_BANKS; bank++) {
      long changed = 0;
      const uint32_t lo = bank * 0x2000, hi = (lo + 0x2000 < ram_bytes) ? lo + 0x2000 : ram_bytes;
      for (uint32_t i = lo; i < hi; i++) changed += ram[i] != prev_ram[i];
      if (changed) {
        g_bank_frames[bank]++;
        g_bank_bytes[bank] += changed;
        if (g_bank_first[bank] < 0) g_bank_first[bank] = fr;
        g_bank_last[bank] = fr;
        memcpy(prev_ram + lo, ram + lo, hi - lo);
      }
    }
    for (int s = 0; s < n_shot; s++)
      if (shots[s].frame == fr && write_bmp(shots[s].path) == 0) printf("shot at frame %ld -> %s\n", fr, shots[s].path);
  }

  printf("frames run: %ld\n", frames);
  printf("core errors: %d", g_errors);
  for (int i = 0; i < g_errors && i < 8; i++) printf("  [code %d at 0x%04x]", g_first_errors[i][0], g_first_errors[i][1]);
  printf("\ncart RAM changes: %u\n", (unsigned)pb_core_cart_ram_generation(&core));
  for (uint32_t bank = 0; bank * 0x2000 < ram_bytes && bank < MAX_BANKS; bank++)
    printf("  RAM bank %u: changed in %ld frames, %ld byte changes, first at frame %ld, last at frame %ld\n",
           (unsigned)bank, g_bank_frames[bank], g_bank_bytes[bank], g_bank_first[bank], g_bank_last[bank]);
  printf("sound: %ld of %ld frames not silent, peak %d\n", g_audio_frames_loud, g_audio_frames, g_audio_peak);
  if (g_serial_len) printf("serial: %s\n", g_serial);
  return g_errors ? 1 : 0;
}
