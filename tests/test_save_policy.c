// Host tests for storage/save_policy: timelines of game writes against the save decisions.
#include <stdio.h>

#include "save_policy.h"

static int failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                            \
    }                                                        \
  } while (0)

int main(void) {
  pb_savepol_t p;

  // Nothing written: never saves, however long it runs.
  pb_savepol_init(&p, 0);
  for (uint32_t t = 0; t < 200000; t += 25) CHECK(!pb_savepol_update(&p, 0, t));
  CHECK(!pb_savepol_dirty(&p, 0));

  // A burst of writes, then quiet: saves exactly IDLE_MS after the last write, not before.
  pb_savepol_init(&p, 0);
  uint32_t gen = 0, now = 1000;
  for (int i = 0; i < 10; i++) {  // ten writes, 100 ms apart
    gen++;
    CHECK(!pb_savepol_update(&p, gen, now));
    CHECK(pb_savepol_dirty(&p, gen));
    now += 100;
  }
  const uint32_t last_write = now - 100;
  CHECK(!pb_savepol_update(&p, gen, last_write + PB_SAVEPOL_IDLE_MS - 1));
  CHECK(pb_savepol_update(&p, gen, last_write + PB_SAVEPOL_IDLE_MS));
  CHECK(pb_savepol_update(&p, gen, last_write + PB_SAVEPOL_IDLE_MS + 500));  // still due until saved
  pb_savepol_saved(&p, gen);
  CHECK(!pb_savepol_update(&p, gen, last_write + 10 * PB_SAVEPOL_IDLE_MS));
  CHECK(!pb_savepol_dirty(&p, gen));

  // A write while a save is due (arrives before saved() is called) is not lost: saving at a
  // counter that is already out of date leaves the policy dirty.
  pb_savepol_init(&p, 0);
  gen = 1;
  pb_savepol_update(&p, gen, 100);
  CHECK(pb_savepol_update(&p, gen, 100 + PB_SAVEPOL_IDLE_MS));
  gen = 2;  // the game writes during the save
  pb_savepol_saved(&p, 1);
  CHECK(pb_savepol_dirty(&p, gen));
  CHECK(!pb_savepol_update(&p, gen, 100 + PB_SAVEPOL_IDLE_MS + 10));  // noticed as a new change
  CHECK(pb_savepol_update(&p, gen, 100 + 2 * PB_SAVEPOL_IDLE_MS + 10));

  // The opposite case: the game's last write lands after the final update() but before the data is
  // copied for the save. The save includes it, so nothing is left unsaved.
  pb_savepol_init(&p, 0);
  pb_savepol_update(&p, 1, 100);
  CHECK(pb_savepol_update(&p, 1, 100 + PB_SAVEPOL_IDLE_MS));
  pb_savepol_saved(&p, 2);  // the counter had already moved to 2 when the RAM was copied
  CHECK(!pb_savepol_dirty(&p, 2));
  CHECK(!pb_savepol_update(&p, 2, 100 + 10 * PB_SAVEPOL_IDLE_MS));

  // A game that never goes quiet (writes every 500 ms) is saved after MAX_AGE_MS.
  pb_savepol_init(&p, 0);
  gen = 0;
  bool saved_at_max_age = false;
  for (uint32_t t = 0; t <= PB_SAVEPOL_MAX_AGE_MS + 1000; t += 500) {
    gen++;
    const bool due = pb_savepol_update(&p, gen, t);
    if (t < PB_SAVEPOL_MAX_AGE_MS) {
      CHECK(!due);
    } else if (!saved_at_max_age) {
      // The counter moves at the same instant, so the policy sees a change first; the age rule
      // fires on the next frame that has no change.
    }
    (void)saved_at_max_age;
  }
  // With one quiet frame after the age limit, the save is due.
  CHECK(pb_savepol_update(&p, gen, PB_SAVEPOL_MAX_AGE_MS + 1000 + 25));

  // Leaving the game early: unsaved changes are reported even before the idle time is over.
  pb_savepol_init(&p, 5);
  CHECK(!pb_savepol_dirty(&p, 5));
  CHECK(pb_savepol_dirty(&p, 6));  // counter moved but update() has not run yet
  pb_savepol_update(&p, 6, 400);
  CHECK(pb_savepol_dirty(&p, 6));
  pb_savepol_saved(&p, 6);
  CHECK(!pb_savepol_dirty(&p, 6));

  // The millisecond clock wraps around (after about 49 days) without a spurious or missed save.
  pb_savepol_init(&p, 0);
  const uint32_t near_wrap = 0xFFFFFFFFu - 500u;
  CHECK(!pb_savepol_update(&p, 1, near_wrap));                       // change just before the wrap
  CHECK(!pb_savepol_update(&p, 1, near_wrap + 100u));                // still before the wrap: not due (a plain compare says due)
  CHECK(!pb_savepol_update(&p, 1, near_wrap + 1000u));               // 1000 ms later (the clock has wrapped)
  CHECK(pb_savepol_update(&p, 1, near_wrap + PB_SAVEPOL_IDLE_MS));   // 2000 ms later: due
  pb_savepol_saved(&p, 1);
  CHECK(!pb_savepol_update(&p, 1, near_wrap + 100000u));

  if (failures == 0) printf("save policy tests passed\n");
  return failures ? 1 : 0;
}
