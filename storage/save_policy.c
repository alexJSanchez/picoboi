#include "save_policy.h"

void pb_savepol_init(pb_savepol_t *p, uint32_t gen) {
  p->last_gen = gen;
  p->last_change_ms = 0;
  p->dirty_since_ms = 0;
  p->dirty = false;
}

bool pb_savepol_update(pb_savepol_t *p, uint32_t gen, uint32_t now_ms) {
  if (gen != p->last_gen) {  // the game changed its save RAM
    if (!p->dirty) {
      p->dirty = true;
      p->dirty_since_ms = now_ms;
    }
    p->last_gen = gen;
    p->last_change_ms = now_ms;
    return false;
  }
  if (!p->dirty) return false;
  if ((uint32_t)(now_ms - p->last_change_ms) >= PB_SAVEPOL_IDLE_MS) return true;
  return (uint32_t)(now_ms - p->dirty_since_ms) >= PB_SAVEPOL_MAX_AGE_MS;
}

bool pb_savepol_dirty(const pb_savepol_t *p, uint32_t gen) { return p->dirty || gen != p->last_gen; }

void pb_savepol_saved(pb_savepol_t *p, uint32_t gen) {
  p->dirty = false;
  p->last_gen = gen;
}
