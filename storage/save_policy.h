// storage/save_policy: decides WHEN the cart RAM is written to flash (PLAN.md section 5, flush
// policy). Pure logic; the app feeds it the core's change counter and a millisecond clock.
//
//   - A game that has changed its save RAM is saved once it has been quiet for PB_SAVEPOL_IDLE_MS
//     (games write in bursts, and flash wears out, so never on every write).
//   - A game that never goes quiet is still saved at least every PB_SAVEPOL_MAX_AGE_MS.
//   - On leaving a game the app saves at once if anything is unsaved (pb_savepol_dirty).
//
// Clocks are 32-bit milliseconds that may wrap (after about 49 days); all comparisons use
// unsigned differences.
#ifndef PB_SAVE_POLICY_H
#define PB_SAVE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PB_SAVEPOL_IDLE_MS 2000u
#define PB_SAVEPOL_MAX_AGE_MS 60000u

typedef struct {
  uint32_t last_gen;        // the change counter when last looked at
  uint32_t last_change_ms;  // when the counter last moved
  uint32_t dirty_since_ms;  // when the first unsaved change happened
  bool dirty;               // something changed since the last save
} pb_savepol_t;

// Start of a game. `gen` is the core's current change counter.
void pb_savepol_init(pb_savepol_t *p, uint32_t gen);

// Call once per frame. Returns true when the RAM should be written now; after writing call
// pb_savepol_saved().
bool pb_savepol_update(pb_savepol_t *p, uint32_t gen, uint32_t now_ms);

// True if there are unsaved changes (for saving when the player leaves the game).
bool pb_savepol_dirty(const pb_savepol_t *p, uint32_t gen);

// The RAM was written (successfully): forget the unsaved state. `gen` is the counter at that moment.
void pb_savepol_saved(pb_savepol_t *p, uint32_t gen);

#ifdef __cplusplus
}
#endif

#endif
