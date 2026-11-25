// launcher/launcher_model: the logic behind the game list (selection, scrolling, labels).
// Pure logic, no drawing and no hardware, so it is host-tested; the app draws it through
// platform/ui.
#ifndef PB_LAUNCHER_MODEL_H
#define PB_LAUNCHER_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rom_library.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t count;     // number of games
  uint32_t selected;  // highlighted game, 0 <= selected < count (0 if count == 0)
  uint32_t top;       // first game shown on screen
} lm_t;

void lm_init(lm_t *m, uint32_t count);

// Moves the highlight by `delta` (negative = up), wrapping at both ends, and scrolls so that
// it stays inside a window of `rows` visible rows.
void lm_move(lm_t *m, int32_t delta, uint32_t rows);

// Why a game cannot be started, as a short text for the screen, or NULL if it can.
// `max_ram` is the save RAM the firmware has room for.
const char *lm_unavailable_reason(const pb_lib_entry_t *e, uint32_t max_ram);

// Formats a byte count as a short label: "512B", "8K", "128K", "1M". `out` needs 8 bytes.
void lm_format_bytes(uint32_t bytes, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
