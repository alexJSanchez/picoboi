#include "launcher.h"

#include <stdio.h>
#include <string.h>

#include "launcher_model.h"
#include "save_manager.h"
#include "ui.h"

namespace {

constexpr int ROWS = 8;
constexpr int ROW_H = 20;
constexpr int LIST_Y = 34;

constexpr ui_color BG = {1, 1, 4};
constexpr ui_color TITLE = {15, 15, 15};
constexpr ui_color NORMAL = {12, 12, 14};
constexpr ui_color DIM = {7, 7, 8};
constexpr ui_color HILITE_BG = {3, 6, 13};
constexpr ui_color HILITE_TX = {15, 15, 15};
constexpr ui_color WARN = {15, 7, 6};
constexpr ui_color HINT = {9, 9, 12};

pb_lib_t g_lib;
pb_lib_status_t g_status = PB_LIB_EMPTY;
uint32_t g_bad_entry = 0;
lm_t g_model;
uint32_t g_max_ram = 0;
char g_message[48] = "";

const char *status_text(pb_lib_status_t s) {
  switch (s) {
    case PB_LIB_EMPTY: return "No games on this device yet.";
    case PB_LIB_ERR_MAGIC: return "No game library found.";
    case PB_LIB_ERR_VERSION: return "Library made by a newer tool.";
    case PB_LIB_ERR_CRC: return "Library is damaged (checksum).";
    case PB_LIB_ERR_ENTRY: return "Library has an invalid entry.";
    default: return "Library cannot be read.";
  }
}

void draw_centered(int y, const char *s, ui_color c) {
  ui_text((240 - ui_text_width(s)) / 2, y, s, c);
}

void draw_no_library() {
  draw_centered(70, status_text(g_status), WARN);
  draw_centered(100, "On your PC, run:", NORMAL);
  draw_centered(116, "tools/pack_library.py", HILITE_TX);
  draw_centered(132, "roms -o library.uf2", HILITE_TX);
  draw_centered(156, "Hold X, power on, copy", NORMAL);
  draw_centered(172, "library.uf2 to the drive.", NORMAL);
}

}  // namespace

void launcher_init(uint32_t max_cart_ram) {
  g_max_ram = max_cart_ram;
  g_message[0] = 0;
  g_status = pb_lib_open(&g_lib, (const uint8_t *)(PB_FLASH_XIP_BASE + PB_LIB_INDEX_OFFSET),
                         PB_LIB_INDEX_SIZE, &g_bad_entry);
  lm_init(&g_model, g_status == PB_LIB_OK ? g_lib.count : 0);
  printf("launcher: library %s, %u games\n", g_status == PB_LIB_OK ? "ok" : status_text(g_status),
         (unsigned)g_model.count);
}

void launcher_set_message(const char *text) {
  snprintf(g_message, sizeof(g_message), "%s", text);
}

bool launcher_update(pb_lib_entry_t *out) {
  const uint8_t b = ui_buttons_pressed();
  if (g_model.count == 0) return false;

  if (b & UI_UP) lm_move(&g_model, -1, ROWS);
  if (b & UI_DOWN) lm_move(&g_model, 1, ROWS);
  if (b & (UI_UP | UI_DOWN)) g_message[0] = 0;

  if (b & UI_A) {
    pb_lib_entry_t e;
    pb_lib_get(&g_lib, g_model.selected, &e);
    if (lm_unavailable_reason(&e, g_max_ram) == nullptr) {
      *out = e;
      return true;
    }
  }
  return false;
}

void launcher_draw_save_prompt() {
  ui_clear(BG);
  ui_text(10, 8, "PICOBOY", TITLE);
  ui_rect(10, 22, 220, 1, HINT);
  draw_centered(44, "SAVE AREA IN USE", WARN);
  draw_centered(68, "This PicoSystem already has", NORMAL);
  draw_centered(82, "other data in the place", NORMAL);
  draw_centered(96, "where game saves are kept.", NORMAL);
  draw_centered(124, "A: erase it, enable saves", HILITE_TX);
  draw_centered(144, "B: play without saving", HILITE_TX);
  draw_centered(176, "(B asks again next time.)", HINT);
}

void launcher_draw() {
  ui_clear(BG);
  ui_text(10, 8, "PICOBOY", TITLE);
  {
    char st[28];
    const SaveState ss = saves_state();
    if (ss == SaveState::Ready) {
      snprintf(st, sizeof(st), "SAVES ON  %u stored", (unsigned)saves_game_count());
    } else {
      snprintf(st, sizeof(st), "SAVES OFF");
    }
    ui_text(230 - ui_text_width(st), 8, st, ss == SaveState::Ready ? ui_color{6, 15, 6} : WARN);
  }
  ui_rect(10, 22, 220, 1, HINT);

  if (g_model.count == 0) {
    draw_no_library();
    return;
  }

  for (int row = 0; row < ROWS; row++) {
    const uint32_t i = g_model.top + row;
    if (i >= g_model.count) break;
    pb_lib_entry_t e;
    pb_lib_get(&g_lib, i, &e);
    const bool selected = i == g_model.selected;
    const bool ok = lm_unavailable_reason(&e, g_max_ram) == nullptr;
    const int y = LIST_Y + row * ROW_H;

    if (selected) ui_rect(6, y - 3, 228, ROW_H - 2, HILITE_BG);
    const ui_color tx = selected ? HILITE_TX : (ok ? NORMAL : DIM);
    char name[22];
    snprintf(name, sizeof(name), "%s", e.name);  // at most 21 characters on screen
    ui_text(12, y + 2, name, tx);

    char size[8];
    lm_format_bytes(e.size, size, sizeof(size));
    const int size_x = 228 - ui_text_width(size);
    ui_text(size_x, y + 2, size, selected ? HILITE_TX : DIM);
    if (saves_has_save(e.rom_crc32)) {  // a stored save was found for this game
      ui_text(size_x - 8 - ui_text_width("saved"), y + 2, "saved", ui_color{6, 15, 6});
    }
  }

  // Scroll hints when the list is longer than the screen.
  if (g_model.top > 0) ui_text(114, 26, "^", HINT);
  if (g_model.top + ROWS < g_model.count) ui_text(114, LIST_Y + ROWS * ROW_H - 4, "v", HINT);

  // Details of the highlighted game.
  pb_lib_entry_t cur;
  pb_lib_get(&g_lib, g_model.selected, &cur);
  const char *why = lm_unavailable_reason(&cur, g_max_ram);
  char detail[48];
  if (why) {
    snprintf(detail, sizeof(detail), "Cannot run: %s", why);
    ui_text(10, 206, detail, WARN);
  } else {
    char ram[8];
    lm_format_bytes(cur.ram_size, ram, sizeof(ram));
    const char *mapper = cur.mapper == 0 ? "no mapper" : (cur.mapper == 1 ? "MBC1" : cur.mapper == 2 ? "MBC2"
                                                         : cur.mapper == 3 ? "MBC3" : "MBC5");
    snprintf(detail, sizeof(detail), "%s, save RAM %s", mapper, ram);
    ui_text(10, 206, detail, HINT);
  }
  if (g_message[0]) ui_text(10, 192, g_message, WARN);
  ui_text(10, 224, "UP/DOWN choose    A start", HINT);
}
