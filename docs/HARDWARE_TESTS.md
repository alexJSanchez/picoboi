# Hardware tests (manual)

Run before each release tag. Record failures and surprises in `docs/DECISIONS.md`.

## Flashing

1. Hold X, power on the PicoSystem. An `RPI-RP2` drive appears.
2. Copy `build/picoboy.uf2` onto the drive. The device reboots into the firmware.

## Phase 0: hello-world (backlog item 2)

| # | Check | Expected | Result |
|---|---|---|---|
| 1 | Flash the UF2 | Drive disappears, firmware boots, no hang | |
| 2 | Splash | "PicoBoy / Game Boy emulator / hello world" for about 2.5 s | |
| 3 | Button screen appears after the splash | Dark blue background, 8 labelled squares, battery number at the bottom | |
| 4 | Press UP, DOWN, LEFT, RIGHT one at a time | Matching square turns green while held, back to dim on release | |
| 5 | Press A, B, X, Y one at a time | Same; the on-screen layout matches the physical layout (X top, Y left, B bottom, A right) | |
| 6 | Hold several buttons at once | All held squares green together | |
| 7 | Text is readable, no tearing or artefacts | | |
| 8 | USB serial ([VERIFY] #7): connect USB, open the serial port (115200 8N1 is fine) | Banner "PicoBoy hello-world (Phase 0)" and `NAME down` / `NAME up` lines on each press and release | |
| 9 | Firmware still runs with no USB host attached (battery only) | Same behaviour as 3 to 7, no stall | |
| 10 | Battery reading | A number is shown; note the value and what it appears to mean (percent or raw) in DECISIONS.md | |

Note for item 8: the banner is printed once at boot, so open the serial port first or reset the device to see it.

## Phase 1: emulator core on screen (backlog items 8, 9, 10)

Firmware is built with the Blargg `cpu_instrs` ROM embedded (run `sh tools/fetch_test_roms.sh` first, then rebuild). Flash `build/picoboy.uf2`. Do not share that UF2: it contains the ROM.

| # | Check | Expected | Result |
|---|---|---|---|
| 1 | Boot | Black border with a light-green 160x144 area centered on the screen (`cpu_instrs` title text appears) | |
| 2 | `cpu_instrs` runs | Test names listed one after another; after about 55 s the screen shows `Passed all tests` (all 11 ROMs show "ok") | |
| 3 | Picture quality | Text is crisp and readable, no tearing, no flicker, no shifted lines | |
| 4 | Frame timing (USB serial, 115200) | A `bench frames=600 emu_us ... period_us ...` line about every 10 s. Note min/avg/max. `period_us` avg near 16667 means the screen refreshes at about 60 Hz | |
| 5 | Core errors | No `core error` lines on serial | |
| 6 | Menu combo | Holding X+Y for about 1 s prints `menu requested` once on serial (menu itself comes in Phase 6) | |
| 7 | Buttons reach the core | `cpu_instrs` does not use buttons; confirm with a later ROM (Phase 7) or a homebrew game. For now only check that pressing buttons does not crash or change timing | |

## Phase 3: flash and UF2 (backlog item 13)

Verifies, on the device, the assumption the ROM library depends on: a UF2 that covers only the library index and ROM area leaves the firmware and the settings/save area alone, and the ROMs read back correctly from flash (read in place, XIP). Uses synthetic ROMs only (no game data). Build and files: `build-flashtest/picoboy.uf2` (firmware, `cmake -DPB_FLASH_TEST=ON`, no emulator) and `build-flashtest/flashtest_library.uf2` (library with 3 synthetic ROMs: 32 KiB, 128 KiB, 256 KiB). The firmware prints a report on the screen and every 3 s over USB serial.

| Stage | Action | Expected |
|---|---|---|
| A | Flash `picoboy.uf2` (hold X, power on, copy). Open serial. | Report appears. `LIB:` is not OK (old data from earlier builds sits there: "NO LIBRARY (bad magic)" or EMPTY). `MARKER was blank`, then written with result 0 and now valid. Note the `FW CRC` (call it F). First save-area sector blank. |
| B | Flash `flashtest_library.uf2` (hold X, power on, copy). The device restarts into the firmware. | `FW CRC` is **F** again (firmware untouched). `LIB: OK`, 3 ROMs, data ok 3 bad 0 (CRCs of the ROMs read from flash match the index). `MARKER was valid` (settings untouched, nothing rewritten). Save area still blank. |
| C | Flash `picoboy.uf2` again. | `LIB: OK`, 3 ROMs, data ok 3 (the library survives a firmware reflash). `FW CRC` is F. `MARKER was valid`. |
| D (optional) | Power-cycle the device. | Same report as C (nothing lost without power). |

Result 2026-11-25: stages A, B and C passed (see `docs/DECISIONS.md`). Record the numbers in `docs/DECISIONS.md`. Any mismatch in the firmware CRC, the marker or the save area is a failure of the approach: stop and investigate before building the launcher.

## Phase 3: game list (launcher)

Firmware `build-lib/picoboy.uf2` (the default build, no ROM inside) and a library made with `python tools/pack_library.py roms -o library.uf2`. Flash both (hold X, power on, copy each).

| # | Check | Expected | Result 2026-11-25 |
|---|---|---|---|
| 1 | Boot | A list of the games in the library, names and sizes, a highlighted row, mapper and save size of the selected game underneath | pass |
| 2 | UP / DOWN | Highlight moves, wraps at both ends; the list scrolls if longer than 8 | list shown; scrolling not exercised with only 2 games |
| 3 | A on a runnable game | The game starts with picture and sound | pass |
| 4 | Hold X+Y for about 1 s during a game | Back to the list, no sound left over | pass |
| 5 | Start the other game | Starts normally | pass |
| 6 | A greyed game (Game Boy Color only, unsupported cartridge, bad header) | Greyed row, reason in red, A does nothing | not yet tried (no such ROM in the library) |
| 7 | No library flashed (erased or stale flash) | A message explaining how to make and flash `library.uf2` | not yet tried |
| 8 | Zelda: Link's Awakening picture | Compatibility note in `docs/COMPAT.md` (Phase 7) | author reported nothing wrong |

## Phase 4: saves (backlog items 14 and 15)

Firmware: `build-lib/picoboy.uf2` (the default build). The library stays as it is (flashing firmware does not touch it). The first erase and program of flash by our own code happens here; it is confined to the settings and save regions (`0xA00000` to `0xC00000`).

| # | Action | Expected |
|---|---|---|
| 1 | Flash the firmware and boot. | If the save region holds other data (it does on the author's device): a screen "SAVE AREA IN USE" asking A (erase it, enable saves) or B (play without saving). If the region is blank: no question, saves are enabled. |
| 2 | Press A on the question. | The game list appears. Serial: `saves: claimed the region`. |
| 3 | Start a game that has battery saves (Pokemon Red), play, use the game's own SAVE command. | About 2 s after the game stops writing, the black bar above the picture shows `SAVED` in green for 1.5 s; the picture and sound freeze for roughly half a second while it writes. Serial: `save: wrote 32768 bytes ... in NNN ms`. |
| 4 | Power the device off and on, start the game, choose CONTINUE. | The saved game is there. Serial: `saves: loaded 32768 bytes`. |
| 5 | Save again, change something, hold X+Y to return to the list within 2 s of the change. | `SAVED` is written on leaving (the app saves at once if anything is unsaved); the change is there after a restart. |
| 6 | Play Zelda (8 KiB save) the same way. | Works the same; its save does not disturb Pokemon's. |
| 7 | Press B on the question (after erasing or on a device that asks again). | Games run, nothing is written to flash, no `SAVED`; the question comes back on the next boot. |
| 8 | Power-cut test (needs the author): in a game save, then cut power (battery out or unplug while on battery) at random moments for a while, including during the `SAVED` pause. | After every restart the game offers either the previous or the new save; never a corrupted or empty one. (The PC test cuts the power at every operation; this checks the real flash.) |

Record the save time per game in `docs/DECISIONS.md`.
