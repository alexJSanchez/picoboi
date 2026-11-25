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

// A button combination that must be held for a while (used to delete a save, so that it cannot
// happen by accident). Clocks are 32-bit milliseconds and may wrap.
typedef struct {
  bool holding;
  bool fired;
  uint32_t start_ms;
} lm_hold_t;

void lm_hold_init(lm_hold_t *h);

// Call once per frame with whether the combination is down. Returns true exactly once, when it has
// been held for `hold_ms`; it must be released before it can fire again.
bool lm_hold_update(lm_hold_t *h, bool down, uint32_t now_ms, uint32_t hold_ms);

// Milliseconds the combination has been held so far (0 if not held), for a progress display.
uint32_t lm_hold_elapsed(const lm_hold_t *h, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
