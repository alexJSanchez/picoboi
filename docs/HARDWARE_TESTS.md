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
