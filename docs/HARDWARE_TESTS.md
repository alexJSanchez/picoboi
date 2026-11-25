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
| 5 | Press A, B, X, Y one at a time | Same; the on-screen layout matches the physical layout (Y top, A bottom, X left, B right) | |
| 6 | Hold several buttons at once | All held squares green together | |
| 7 | Text is readable, no tearing or artefacts | | |
| 8 | USB serial ([VERIFY] #7): connect USB, open the serial port (115200 8N1 is fine) | Banner "PicoBoy hello-world (Phase 0)" and `NAME down` / `NAME up` lines on each press and release | |
| 9 | Firmware still runs with no USB host attached (battery only) | Same behaviour as 3 to 7, no stall | |
| 10 | Battery reading | A number is shown; note the value and what it appears to mean (percent or raw) in DECISIONS.md | |

Note for item 8: the banner is printed once at boot, so open the serial port first or reset the device to see it.
