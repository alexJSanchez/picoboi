# Decisions and measurements

Record every decision, resolved [VERIFY] item and measured number here, with the date and the phase it belongs to.

## Pinned dependency versions

Nothing is pinned yet. Fill in when each dependency is added (adding one needs explicit approval, see CLAUDE.md).

| Dependency | Version / commit | Date pinned | License verified |
|---|---|---|---|
| pico-sdk | tag `2.3.1` (tinyusb submodule at `86ad6e5`) | 2026-11-25 | not yet |
| picosystem | commit `9a26b2a` (upstream pimoroni/picosystem `main`) | 2026-11-25 | not yet |
| Arm GNU Toolchain (arm-none-eabi-gcc) | 14.2.Rel1 (gcc 14.2.1) | 2026-11-25 | n/a (toolchain, not shipped) |
| picotool (host tool) | 2.3.1 | 2026-11-25 | n/a (build tool) |
| pioasm (host tool) | 2.3.1 | 2026-11-25 | n/a (build tool) |
| peanut_gb | tag `v1.3.0` (commit `44d85f6`, deltabeard/Peanut-GB) | 2026-11-25 | MIT (header of `peanut_gb.h`) |
| minigb_apu | _not added_ | | |

## Open decisions (PLAN.md section 12)

| # | Question | Status |
|---|---|---|
| 1 | Default control mapping and menu combo (plan default: X→Select, Y→Start, hold X+Y ≈1 s for menu) | Open |
| 2 | Host-side UF2 packing acceptable for v1? | Open |
| 3 | Audio required for v1, or off by default? | Open |
| 4 | Repo license | Open |

## Unresolved [VERIFY] items (PLAN.md section 2)

1. SDK framebuffer RAM in 240×240 mode
2. How `flip()` pushes pixels (DMA/PIO, core 1?)
3. Minimum Pico SDK version for `flash_safe_execute()`; PicoSystem SDK compatibility
4. Size of `struct gb_s` in the pinned Peanut-GB
5. `lcd_draw_line` pixel format and gamepad bit layout
6. Stable overclock speed and battery impact
7. `stdio_usb` logging alongside the SDK

## Log

### 2026-11-25 — Phase 0, backlog item 1: scaffold builds a UF2

Serves Phase 0 acceptance ("CMake configures and builds to a UF2"). Hardware flashing is not yet tested.

- Pinned `pico-sdk` 2.3.1 and `picosystem` `9a26b2a` as submodules; the pair configures and builds. This partly answers [VERIFY] #3: `picosystem` at `9a26b2a` builds against Pico SDK 2.3.1. `flash_safe_execute()` availability in 2.3.1 is still to be checked when used.
- **Upstream picosystem does not link out of the box** with SDK 2.3.1 + Arm GNU 14.2.Rel1: `__dso_handle` is undefined (libstdc++ pulled in by `utility.cpp`'s `std::string` references it via `__aeabi_atexit`). The author's own fork of picosystem fixes this with `PROVIDE(__dso_handle = 0);` in `memmap_picosystem.ld`. We keep the submodule unmodified and define the symbol in `src/toolchain_shims.cpp`.
- **Host tools:** SDK 2.x builds `picotool` and `pioasm` from source, which needs a host C/C++ compiler. This machine has none, so the build is pointed at prebuilt copies with `-Dpicotool_DIR=<dir>` and `-Dpioasm_DIR=<dir>` (both 2.3.1). CI machines with a host compiler will not need these flags.
- The SDK's `pico_cxx_options` already passes `-fno-exceptions` and `-fno-rtti` for C++ by default (verified in `build/build.ninja`), so CLAUDE.md's "no exceptions or RTTI" rule is met without extra flags.
- **[VERIFY] #1 (partly resolved):** in 240×240 mode the framebuffer `_fb` is 115,200 bytes (0x1C200 = 112.5 KiB), matching the plan. Total `.bss` of the empty scaffold is 118,208 bytes.
- **Correction to PLAN.md section 4:** the default sprite sheet (`_default_sprite_sheet`, 0x8000 bytes) is read-only data in flash (0x100134a8), not RAM; the RAM-side struct is 16 bytes. With this pinned version the "32 kB RAM" saving from `no_spritesheet()` does not apply, so RAM headroom is about 32 KiB larger than the plan estimated. `no_spritesheet()` would only save flash. PLAN.md is left as written until the Phase 2 linker-map pass produces the full table.
- `picosystem` ships its own linker script (`memmap_picosystem.ld`) that `picosystem_executable()` applies. Phase 3's firmware cap at 1 MiB will need a copy/replacement of it (the plan's `ld/picoboy_memmap.ld`); note for then.
- Scaffold build size: text 124,976 bytes; UF2 250,368 bytes (UF2 is not committed).

### 2026-11-25 — Phase 0, backlog item 2: hello-world on hardware

Serves Phase 0 acceptance ("UF2 flashes; all 8 buttons visibly respond"). Tested on the author's PicoSystem.

- **Boots and runs:** the UF2 flashes, the firmware boots and shows the button test screen. All 8 buttons respond.
- **Button layout** (physical): X top, Y left, B bottom, A right. The test screen was corrected twice to match (commits `4209add`, `1446af4`). Final layout confirmed on hardware.
- **Battery:** `picosystem::battery()` returns a percentage; the value shown looked plausible. (Exact scale and accuracy not measured; tidy up when a battery indicator is built in Phase 6.)
- **[VERIFY] #7 resolved: `stdio_usb` works alongside the PicoSystem SDK** (pico-sdk 2.3.1, picosystem `9a26b2a`). The device enumerates as a composite USB device, VID `2E8A` PID `000A`: a CDC serial port (COM7 on this PC, 115200 8N1) plus the SDK's vendor "Reset" interface, which Windows lists under "Universal Serial Bus devices" as "Reset". Button down/up lines were read from the serial port while pressing B, A, Y, X. The one-shot boot banner was not captured (port opened after boot); not a concern.
- Confirmed: runs on battery with no USB host attached (HARDWARE_TESTS.md row 9). Exact splash timing not measured.
- Remaining for Phase 0 acceptance: CI builds the UF2 (backlog item 4).

### 2026-11-25 — Phase 0, backlog item 4: CI workflow

Serves Phase 0 acceptance ("CI builds the UF2"). `.github/workflows/build.yml`: on push to `main` and on pull requests, builds on `ubuntu-24.04` with Arm GNU 14.2.Rel1 downloaded from Arm (same version as local builds), configures and builds with Ninja, fails if `picoboy.bin` exceeds 1 MiB, and uploads `picoboy.uf2` as an artifact (firmware only, no ROM).

- CI does not use the `-Dpicotool_DIR` / `-Dpioasm_DIR` flags; the SDK builds both from source on the runner (it has a host compiler). This is untested until the first run on GitHub.
- The 1 MiB check is on the `.bin` size for now. The real linker-enforced cap arrives in Phase 3.
- Host unit tests are not part of CI yet; there are none until `storage/` exists.

### 2026-11-25 — Phase 1: Peanut-GB pinned, API facts from source

Serves Phase 1 ("Vendor Peanut-GB at a pinned commit under `third_party/`").

- Added `third_party/peanut_gb` as a submodule at tag `v1.3.0` (`44d85f6`). License is MIT (checked in the `peanut_gb.h` header; the repo has no separate LICENSE file). `minigb_apu` is not added yet (Phase 5).
- **[VERIFY] #4 resolved:** `sizeof(struct gb_s)` is 16,916 bytes (0x4214) on Cortex-M0+ (`arm-none-eabi-gcc` 14.2.1, `-O2`) with `ENABLE_SOUND 0`, `ENABLE_LCD 1`, `PEANUT_GB_HIGH_LCD_ACCURACY` at its default of 1. The plan estimated about 19 KiB, so the real figure is about 2 KiB better. Re-measure if the configuration macros change.
- **[VERIFY] #5 resolved from the pinned source:** `lcd_draw_line(gb, pixels, line)` gets 160 bytes; bits 1-0 are the shade (0 to 3), and with `PEANUT_GB_12_COLOUR` (default 1) bits 5-4 select the palette (OBJ0 = 0b00, OBJ1 = 0b01, BG = 0b10). `line` is 0 to 143. Joypad is `gb->direct.joypad`, **active-low** (bit set = released; initial value 0xFF), bits: A=0x01, B=0x02, SELECT=0x04, START=0x08, RIGHT=0x10, LEFT=0x20, UP=0x40, DOWN=0x80. Our `pb_core_set_buttons()` stays active-high and the adapter inverts.
- Peanut-GB's own `test/` folder ships `cpu_instrs.h` and `dmg-acid2.gb.h` (test ROMs as C headers) and a serial-output pass check. They live in the submodule, not in our tree. Their licenses (Blargg's tests are LGPL; dmg-acid2 is by Matt Currie) must be checked before we copy or build against them, per PLAN.md section 9; until then we do not vendor them.
- `gb_init()` returns an error for unsupported cartridge types and bad header checksums (`GB_INIT_CARTRIDGE_UNSUPPORTED`, `GB_INIT_INVALID_CHECKSUM`); `gb_get_save_size()` gives the cart RAM size. The header is single-file: implementation is compiled into whichever translation unit includes it without `PEANUT_GB_HEADER_ONLY`, so only `core/` should include it.
- **Host compiler:** this machine has no host C compiler, so the PC build and host tests (Phase 1 items 6 and 7) cannot be run locally until one is installed.

### 2026-11-25 — Phase 1: core adapter and host tests

Serves Phase 1 acceptance ("Blargg `cpu_instrs` reports pass"; "`dmg-acid2` renders recognisably correct"). Host side only; the on-device display (backlog item 8) is not done yet.

- Added `core/pb_core.{h,c}`: portable adapter, caller-provided storage (`PB_CORE_STORAGE_BYTES` = 20480, enforced by `_Static_assert`; compiles clean for Cortex-M0+ with ~18.5 KB of code and natively with gcc 16.2). ROM and cart RAM accesses are bounds-checked (cost to be measured in Phase 2); dirty flag is set only when a cart RAM byte actually changes.
- Added `tests/` (host build, `core_run` headless runner) and `tools/fetch_test_roms.sh` (downloads two free test ROMs into untracked `tests/roms/`, verified against pinned SHA-256).
- **Result:** `cpu_instrs` passes all 11 sub-tests ("Passed all tests") in 3,175 frames through our adapter (about 1.5 s on the PC). `dmg-acid2` runs 200 frames with no core errors and the dumped frame shows the correct face and "HELLO WORLD!" title; compared from memory, not pixel-diffed against the reference image.
- **Test ROM licensing:** `dmg-acid2` is MIT (Matt Currie). The Blargg `cpu_instrs.gb` comes from the `retrio/gb-test-roms` mirror, which declares no license in its metadata; Blargg's sources are distributed under the LGPL. We never commit these files: they are downloaded on demand into the gitignored `tests/roms/`, as PLAN.md section 9 says. If that is not acceptable, drop the `cpu_instrs` test.
- **Host compiler:** installed WinLibs GCC 16.2.0 (winget `BrechtSanders.WinLibs.POSIX.UCRT`) on the dev machine. The host test command now needs `-G Ninja` (CMake would otherwise look for Visual Studio). SDK builds of picotool/pioasm could now compile from source on this machine, but the prebuilt-tool flags still work and stay documented.
- CI now has a second job, `host-tests`: installs gcc/Ninja, runs `tools/fetch_test_roms.sh`, builds `tests/` and runs `ctest`. Untested on GitHub until the first run; a missing or corrupt ROM download would show up as a failed or skipped test, so check the job output rather than only the green tick.

### 2026-11-25 — Phase 1: emulator on the device (code written, hardware test pending)

Serves Phase 1 acceptance ("`cpu_instrs` reports pass on screen; frame time logged"). Built and linked; not yet run on hardware.

- **Controls decision (PLAN.md section 12, #1): accepted the plan default.** A→A, B→B, D-pad→D-pad, X→Select, Y→Start; holding X+Y for 1 s requests the menu (event only for now; the menu is Phase 6). Implemented in `platform/input.cpp`.
- **Frame pacing:** the SDK main loop is `update(); wait for previous flip; draw(); wait for vsync; flip`, so one `draw()` equals one display refresh. We run one `pb_core_run_frame()` per `draw()`, after the SDK has finished the previous framebuffer transfer, so we never write into the framebuffer while it is being sent. This couples emulation speed to the panel's vsync rate; the real rate is to be read from the `period_us` serial output (about 16,667 us would mean 60 Hz vs the Game Boy's 59.73 Hz). This partly answers [VERIFY] #2 (the flip uses DMA, started from the main loop); whether core 1 is involved is still open.
- **Drawing path:** `platform/display.cpp` writes each scanline straight into the SDK framebuffer (`SCREEN->data`) through a 4-entry palette; palette bits (OBJ/BG) from Peanut-GB are ignored for now.
- **ROM embedding:** `src/embedded_rom.S` `.incbin`s `tests/roms/cpu_instrs.gb` into `.rodata`; confirmed in the ELF at `0x1001bedc` (flash, XIP), 65,536 bytes, so it is read in place. The ROM and any UF2 built with it stay out of git (`.gitignore`). If the ROM file is missing the firmware builds without it and shows "no ROM embedded" (this is what CI builds).
- **RAM now (from `nm`):** framebuffer 115,200 B + core state `g_core` 20,480 B + cart RAM buffer 32,768 B; `.bss` total 173,604 B (about 170 KiB of 264 KiB, so roughly 94 KiB free before stacks and heap). Firmware `text` 231,160 B including the 64 KiB ROM; UF2 462 KB. Full table is a Phase 2 task (`docs/MEMORY_BUDGET.md`).
- **Layering note:** `src/main.cpp` is the SDK entry glue (`init`/`update`/`draw`) and includes SDK headers for that reason; all drawing and input access live in `platform/`.

### 2026-11-25 — Phase 1 result on hardware, and a pacing problem

Serves Phase 1 acceptance. Tested on the author's PicoSystem with the Blargg `cpu_instrs` ROM embedded.

- **Accepted:** `cpu_instrs` runs on the device and the screen shows "Passed all tests" (reported by the author). Frame time is logged over USB serial. (`dmg-acid2` was checked on the PC only, not on the device.)
- **Measured (serial, two consecutive 600-frame windows, SDK default 250 MHz overclock, no audio, `cpu_instrs` ROM):** emulation time per frame min 10,061 us, avg 10,061 us, max 10,074 us. Loop period min 25,149 us, avg 25,161 to 25,164 us, max 25,175 to 25,179 us.
- **Problem: the loop period is about 25.2 ms (about 39.7 Hz), not 16.7 ms.** The SDK main loop waits for the LCD vsync before every flip, and the SDK programs the panel's frame-rate register `FRCTRL2` (0xC6) to `0x1E` in `hardware.cpp`, which on this controller family is 40 Hz (datasheet table value to be double-checked; `0x0F` is listed as 60 Hz). With one emulated frame per display refresh the game would run at about 40/59.73 = 67% speed. `cpu_instrs` is not timing-sensitive, so it still passed, but real games would run slow. [VERIFY #2 further resolved: the pacing source is the panel refresh configured by the SDK.]
- **Headroom:** emulation takes about 10.1 ms of a 16.7 ms frame (about 60%) in this scene, with unoptimised code and the `cpu_instrs` workload; it is not a game scene and does not include audio. Game scenes (PPU-heavy, bank switching) must be benchmarked in Phase 2.
- **Options for Phase 2 (not decided, nothing changed yet):** (a) reprogram `FRCTRL2` to a 60 Hz setting from `platform/display`, then check on hardware that the DMA transfer still fits and the picture is stable; (b) decouple emulation from vsync and run it from a 59.73 Hz timer, accepting that the screen shows only 40 of those frames per second (uneven motion); (c) a mix: 60 Hz panel if (a) works, else (b). Option (a) uses an SDK-internal command function, so it needs care and a hardware test. Must be settled before audio (Phase 5), since audio needs a steady 59.73 Hz frame clock.

### 2026-11-25 — Phase 2, backlog item 5: memory budget from the linker output

Serves Phase 2 acceptance ("linker map reviewed; memory budget table updated with real numbers"). Full table in `docs/MEMORY_BUDGET.md`.

- **Result:** main RAM used 185,648 B (181.3 KiB) of 262,144 B, **74.7 KiB free**, with the framebuffer, a 32 KiB cart RAM buffer and the emulator state all in place. This is better than the plan's 64 KiB estimate. The framebuffer-bypass fallback in PLAN.md section 4 is not needed at this point.
- **[VERIFY] #1 fully resolved:** `_fb` is 115,200 B in main RAM. Core 0's stack is 2 KiB in SCRATCH_Y; SCRATCH_X (4 KiB) is free while core 1 is unused.
- **New finding for Phase 3 (flash map):** `memmap_picosystem.ld` declares `FLASH` = 12 MiB at `0x10000000` and a `FAT` region of 4 MiB ("Reserved for FAT"; its origin line reads `0x10003000`, which looks like a typo upstream because it overlaps `FLASH`). PLAN.md section 5 places saves at `0xE00000` to `0x1000000`, inside that top 4 MiB. Needs a decision before Phase 3: use the full 16 MiB (overwrites anything the stock firmware keeps there), or keep to the first 12 MiB. Not decided; the PicoSystem bootloader behaviour is still a [VERIFY].

### 2026-11-25 — Phase 2: pacing solved by emulating in update() (measured on hardware)

Serves Phase 2 / PLAN.md section 6 ("average >= 59.5 fps"). Measured on the author's PicoSystem, SDK defaults (250 MHz overclock), no audio, `cpu_instrs` ROM only (not a game scene).

Sequence of findings, all from USB serial output:
1. Emulating once per `draw()` (Phase 1): game ran at 39.7 fps (67% speed) because the panel is 40 Hz.
2. Time-based pacer with catch-up cap 3, emulating in `draw()`: game speed correct (59.62) but the screen updated only about 20 times a second. Cause: the screen transfer takes about 12.2 ms of the 25.2 ms refresh and `draw()` waits for it, leaving about 13 ms; a refresh needing 3 frames (about 22 ms) missed vsync, then owed 3 again and stayed locked there. (One printout in this round had mislabelled columns because my format string did not match its arguments; corrected afterwards.)
3. Cap lowered to 2, still emulating in `draw()`: still 20 Hz, game back to 39.7 fps, for the same reason (2 frames, about 16 ms, do not fit in about 13 ms).
4. **Fix in use:** emulate in `update()` (which the SDK calls while the previous transfer is still running in the background) into a private 160x144 off-screen buffer; `draw()` only copies it into the SDK framebuffer. Cap stays at 2 (`PB_PACER_MAX_CATCH_UP`).

**Result (two consecutive 600-refresh windows):** `gb_fps` 59.77 and 59.72 (Game Boy: 59.73); `screen_hz` 39.71 and 39.73 (the panel's 40 Hz); emulation per Game Boy frame min 6,077 us, avg about 8,320 us, max 9,850 us (frames that are not rendered cost about 6.1 ms, rendered ones about 9.5 to 10 ms); `update()` max 15,532 us of a 25,200 us period, so about 9.7 ms headroom in this scene. Period max about 25,208 us, no missed vsync.

- RAM cost: +23,040 B (the off-screen buffer). Free main RAM is now 53,360 B (52.1 KiB). Updated in `docs/MEMORY_BUDGET.md`.
- A stats bug (average computed over 599 instead of 600 samples) was fixed with a sample counter.
- **Not yet shown:** a real game scene. `cpu_instrs` is a CPU test; a PPU-heavy or bank-switching scene could cost more per frame. With two frames per refresh, a scene where one rendered frame costs more than about 17 ms (plus about 8 ms for the other) would miss vsync again. Needs real game scenes (Phase 2 acceptance wants three) before we call performance solved.
- Not done, not needed so far: reprogramming the panel to 60 Hz, using core 1, hot code in RAM. These stay as options if game scenes turn out heavier.

### 2026-11-25 — Audio (PLAN.md Phase 5), started early at the author's request: research and plan

CLAUDE.md says not to start a later phase early; the author asked for audio now (before Phases 3 and 4), so this is a conscious deviation. Nothing is implemented in this entry, only facts from the pinned sources and the plan.

**Library / license.** Peanut-GB v1.3.0 ships a copy of the APU at `third_party/peanut_gb/examples/sdl2/minigb_apu/` with its own `LICENSE`; `minigb_apu.c` states MIT (Koshkouei, Baines; based on MiniGBS, also MIT). We use that bundled copy, so **no new submodule or version pin is needed**. (The standalone `deltabeard/minigb_apu` repo has no LICENSE file at its HEAD `344d981` and GitHub shows no license, which is why we do not use it.) `THIRD_PARTY.md` updated.

**How it plugs in (from the source).** Compile with `ENABLE_SOUND 1` and provide `audio_read(addr)` / `audio_write(addr, val)`, which Peanut-GB calls for the sound registers `0xFF10` to `0xFF3F` with `gb` in scope. `minigb_apu_audio_callback(ctx, stream)` must be called **once per emulated frame** (including frames we do not render) and fills `AUDIO_SAMPLES_TOTAL` interleaved stereo 16-bit samples. At the default `AUDIO_SAMPLE_RATE` 32768 that is 548 samples per channel per frame (truncated from 548.6); at 59.7275 frames/s the real production rate is 32,730.7 samples/s, so the output side must consume at that rate, not 32,768. Integer-only (no floats). The library's README claims about 1000x real time on an RP2040 at 125 MHz; **not measured by us yet**.

**Speaker hardware (from `third_party/picosystem`).** The speaker is a piezo on GPIO 11 driven by PWM. The SDK starts a **1 ms repeating timer** (`_audio_update_timer`, `hardware.cpp`) whose callback `_update_audio()` calls `_play_note()` every millisecond even when silent; that rewrites the PWM clock divider, wrap and level and would overwrite any sample output. The timer object is a non-static global in namespace `picosystem`, so `platform/audio` can stop it with `cancel_repeating_timer()` during `init()` (SDK `play()` stops working afterwards, which we do not use). This reaches into SDK internals, so it must stay isolated in `platform/audio` and be re-checked if the `picosystem` pin changes. The SDK's own comments say the piezo cannot do volume directly and that short pulses (under about 1/10000 s) give partial deflection, so PCM as PWM duty at a carrier of about 32 kHz (period about 30 us) looks workable; sound quality on this piezo is **unknown until heard**.

**Plan (each step is its own commit):**
1. `core/`: enable sound in `pb_core` (bundled APU), add an audio API; host test with a tiny hand-assembled tone ROM generated at test time (our own code, no Nintendo data) checking that samples are non-silent and have the expected pitch, plus a check that `cpu_instrs` still passes with sound on.
2. `platform/audio`: stop the SDK timer, PWM on GPIO 11, ring buffer filled once per emulated frame, drained at 32,730 Hz by a timer/DMA; mono mix of left and right; fill-level handling so underruns and overruns cannot click or drift.
3. Hardware test: first the tone, then music from the Pokemon ROM; measure frame time with audio on (budget so far: 15.5 ms of 25.2 ms in `update()`).
4. Mute/volume control; a menu does not exist yet (Phase 6), so a temporary input combo is needed. To be decided with the author.

### 2026-11-25 — Audio step 1: core produces audio (host-tested; device unchanged)

- `pb_core` is built with `ENABLE_SOUND 1` and the bundled `minigb_apu` (compiled from `third_party/peanut_gb/examples/sdl2/minigb_apu/minigb_apu.c`, 16-bit samples). New config field `on_audio(user, mono, count)`: called once at the end of every emulated frame (rendered or not) with 548 mono samples (left and right averaged, mixed in place in the core's own buffer). If `on_audio` is NULL no audio is generated (no cost). Constants `PB_AUDIO_SAMPLES_PER_FRAME` (548) and `PB_AUDIO_NOMINAL_RATE` (32,768) are checked against the library at compile time.
- **Host tests** (`tests/test_audio.c`, generates a tiny ROM in memory, no Nintendo data): exactly one `on_audio` per frame with 548 samples; silent before the tone starts; pitch within 1.5% (measured 439.8 Hz vs 439.8 expected, and 886.3 Hz vs 885.6 expected, peak about 7,170); no crash without a sink. A deliberate break of the per-frame APU call makes the test fail. `cpu_instrs` still passes with sound compiled in (all 5 host tests pass).
- **Cost:** code about +5 KB flash; RAM unchanged (the APU state and 2,192-byte stereo buffer fit in the existing 20,480-byte `pb_core_t` storage; the `_Static_assert` still holds on Cortex-M0+ and on the PC). The firmware does not use audio yet (`on_audio` is NULL), so on-device behaviour and timing are unchanged.
- Fixed a compiler warning: the APU defines its frame size with a floating-point expression, which is not a C constant, so our buffer uses our own integer constant (kept equal by the static assert).

### 2026-11-25 — Audio step 2: first hardware try failed; cause found (DMA channel collision)

First flash of the speaker driver (test-tone build): the author heard a repeating fast click that did not change with the pitch, and the device printed nothing over serial (USB enumerated, main loop silent). Cause: the PicoSystem SDK drives the screen with **DMA channel 0 without claiming it** (`hardware.cpp`: `dma_channel = 0`), so `dma_claim_unused_channel()` returned channel 0 and our audio setup reconfigured the screen's channel; the screen transfer then never completed and the main loop waited on it forever. Fix: claim channel 0 on the SDK's behalf (`dma_channel_claim(0)` if not claimed) before claiming ours, plus a refusal to start if we ever got 0. Lesson recorded: SDK-internal hardware use is not visible through the Pico SDK's claim tables, so any new DMA, PWM slice or timer use must be checked against `third_party/picosystem/libraries/hardware.cpp` first. The same check applies to PWM slice 5 (speaker) and any future PIO/IRQ use.

### 2026-11-25 — Audio step 2: speaker pipeline verified on hardware (test-tone build)

Serial readout after the DMA channel fix, test-tone build (`cpu_instrs` screen, steps through 220, 440, 880, 1760, 3520, 5000 Hz and a silent step):

- **PWM:** `top` = 7637 as computed (7638 cycles per sample at 250 MHz), divider 1.0, enabled. DMA busy, **measured playback 32,730 to 32,731 Hz** against the expected 32,730.7 Hz, so the PWM+DMA timing is right.
- **Narrow write assumption confirmed:** the compare register reads back `0x162e162e` (the same 16-bit value in both halves), so the 16-bit DMA write to the channel B half replicates to both halves as expected; harmless for GPIO 11 (channel B).
- **Cost of audio:** `update()` avg 12,591 us and max 15,786 us (was 12,508 and 15,532 before audio; the test tone also runs the core with no audio sink, so this is a bound on the driver and the sync logic, not on the APU, which is measured with the game). `gb_fps` 59.76, `screen_hz` 39.71, no missed refresh. Blit unchanged (about 966 us).
- **Sync logic on real clocks:** one underrun and one overrun, both at start-up only (the count did not grow afterwards): the first audio frame arrives later than the 1000-sample prefill lasts. About 11 one-sample nudges (`dups`) in the first 25 s after the reference was learned, consistent with a small clock mismatch. Fill stays between about 1,200 and 1,500 when sampled right after a push.
- RAM: the 4 KB ring buffer is in `.bss` (`g_ring` at 0x20003000).
- **Still open:** how it sounds. Quality of PWM-through-piezo, the mix level (`g_gain`, full scale +-16384 maps to +-50% duty), and whether the carrier whine is audible have to be judged by ear. Then the game's APU audio and its CPU cost.

### 2026-11-25 — Phase 2: speed investigation with audio on, and the budgeted scheduler (measured on hardware)

Serves Phase 2 / PLAN.md section 6. Pokemon Red (author's own ROM, never committed) with game audio on, SDK defaults (250 MHz, 40 Hz panel).

**Problem.** In a busy scene the game and the music ran at about two thirds speed and the screen dropped to about 20 updates per second: `gb_fps` 39.7, `screen_hz` 19.9, `emu_us` avg 13.4 ms / max 17.8 ms, `update_us` avg 26.7 ms over the 25.2 ms refresh, audio underruns growing by about 40 every 3 s. Cause: two frames in one refresh cost more than the refresh; the SDK loop then misses vsync (period doubles to about 50 ms), the pacer owes more frames, and the system stays locked in that state.

**Where the time goes (boot benchmark: first 1,800 frames of the ROM with no input, three passes, microseconds per frame).** Drawing + audio (A) 5.7 to 10.0 ms avg; audio only (B) 1.7 to 5.1; neither (C, pure CPU emulation) 1.5 to 4.1. So the picture costs about 4.1 to 5.7 ms per frame, audio 0.04 to 1.2 ms (`apu_us` about 0.9 to 1.1 ms when music plays), CPU emulation the rest. Worst single frames reach 19 ms with drawing. The slow scene the author reported cost more than any of these 1,800 boot frames (min 9.2 ms without drawing).

**Things tried, with measured effect:**
- Emulator + audio code run from RAM (own copy of the linker script, `ld/picoboy_memmap.ld`): `emu_us` avg 13.37 to 13.00 ms, about 3%. Costs 22 KB of RAM (free RAM now 26.3 KiB). Kept for now; revisit if RAM is needed.
- `PEANUT_GB_HIGH_LCD_ACCURACY` 0 instead of 1: about 0.15 ms per frame (1.5%). Not worth the accuracy loss; left at 1 (CMake option `PB_HIGH_LCD_ACCURACY`).
- Catch-up cap 2 or 3 alone: does not help; the lock-in persists.

**Fix that worked: budgeted scheduler** (`pb_pacer_plan` in `core/pb_pacer.c`, host-tested with simulated light/heavy/extreme scenes). Each refresh it estimates the cost of a frame with and without drawing (running peak with slow decay, measured on the device) and picks: all due frames with the last drawn if that fits 23,000 us; else all due frames without drawing (game speed and audio stay right, the picture repeats); else as many as fit, dropping the rest. Emulation also moved into `update()` so it overlaps the 12 ms screen transfer (earlier finding). The line copy into the off-screen buffer now moves four pixels at a time (with an alignment fallback; the Cortex-M0+ faults on unaligned word access).

**Result on hardware, stable (two consecutive 600-refresh windows, the author's scene):** `gb_fps` 59.77, `screen_hz` 39.71, picture drawn on 600 of 600 refreshes, 0 frames dropped, `update_us` avg 9.4 ms and max 13.0 ms, `emu_us` avg 6.3 ms, `apu_us` avg 0.91 ms, audio underruns constant at 52 (not growing), overruns 24 (not growing). The author reports it works much better and calls it stable.

**Caveats.** (1) In this scene the estimated costs were low (no-draw 4.3 ms, draw 7.5 to 8.3 ms), lighter than the scene measured before the fix (9.2 and about 17 ms), so the "heavy scene, draw about every other refresh" path of the scheduler was validated by simulation but not yet seen on hardware. (2) The boot benchmark is not representative of busy gameplay scenes; a recorded-input replay would make busy scenes repeatable. (3) The mute combo (X+Y+B) is untested on hardware.

### 2026-11-25 — Phase 3 starts: flash stays inside the first 12 MiB (decision) and the layout is defined in code

Serves Phase 3 ("`flash_layout.h` is the single source of truth for addresses"; backlog item 11, first half).

- **Decision (author): use only the first 12 MiB of flash.** The PicoSystem SDK's linker script treats 12 MiB as user flash and reserves the top 4 MiB for its FAT filesystem (see the memory budget entry). Staying out of it keeps whatever the stock firmware stores there intact and avoids over-reaching on the device. PLAN.md section 5 had put the save area at `0xE00000`; that is now `0xA00000`. This answers the "flash map" open item from the memory budget entry.
- **New layout** (`storage/flash_layout.h`, `docs/FLASH_LAYOUT.md`): firmware 1 MiB at `0x000000`; library index 4 KiB at `0x100000`; ROM area `0x101000` to `0xA00000` (0x8FF000 = 9,433,088 bytes, room for eight 1 MiB games); settings 8 KiB at `0xA00000`; save slots `0xA02000` to `0xC00000` (0x1FE000 = 2,088,960 bytes, about 31 games with A/B copies of a 32 KiB save). Everything above `0xC00000` is not ours.
- **Guards:** compile-time asserts that the regions touch but do not overlap, are sector aligned, stay inside the user flash, and that the usable flash end is exactly 12 MiB; plus `pb_flash_erase_allowed()` / `pb_flash_program_allowed()` that only allow sector/page-aligned operations inside settings + save slots, with overflow-safe arithmetic. Host test `flash_layout` (all 7 host tests pass). The header also compiles as C++ for the firmware.
- **Test of the tests:** three deliberate breaks. A ROM area overlapping settings and a removed alignment check were caught immediately. Moving the end of usable flash to 14 MiB was **not** caught at first, because every region is derived from that value; fixed by adding an assert that it equals 12 MiB.
- Not done yet in Phase 3: the 1 MiB linker cap (development builds that embed a 1 MiB ROM exceed it, so it comes with the library), `tools/pack_library.py`, `storage/rom_library`, the launcher, and the on-hardware UF2 test (backlog items 12 and 13).

### 2026-11-25 — Phase 3: library index format v1, PC tool and firmware reader (host-tested)

Serves Phase 3 backlog item 12 (`tools/pack_library.py` + `storage/rom_library` with host unit tests). Nothing runs on the device yet.

- **Format** (documented in `docs/FLASH_LAYOUT.md`): 16-byte header (magic `PBLB`, version 1, count, entry size 48, CRC-32 of the entries) plus up to 85 entries of 48 bytes (name, flash offset, size, ROM CRC-32, save RAM size, banks, flags, mapper, cartridge type). Differences from the plan's sketch: the offset is from the start of flash (not "from the XIP base"), entries carry the cartridge type and a size-mismatch flag, and the header has an entry size field for future growth.
- **Single source of truth kept:** `pack_library.py` reads every flash address from `storage/flash_layout.h` instead of repeating them.
- **PC tool:** reads ROM headers (title, cartridge type, size and RAM codes, header checksum, Game Boy Color flag), flags what the core cannot run, lays the ROMs out back to back in whole 4 KiB sectors, and writes a UF2 (256-byte blocks, RP2040 family id, ascending addresses) covering only the index sector and the used ROM area. It refuses more than 85 ROMs or a full ROM area (error names the sizes).
- **Firmware reader:** `storage/rom_library.c` (784 bytes of code on Cortex-M0+) and `storage/crc32.c` (120 bytes). It validates the index in place (so it can point straight at flash) and decodes entries on demand; reads bytes individually because the Cortex-M0+ faults on unaligned word access.
- **Tests (all 9 host tests pass):** 11 Python tests (CRC check value, layout parsing, golden index, per-ROM flags, UF2 structure and content, capacity boundary, CLI) and the C test that reads the **same golden file** and checks every field of 7 entries, 12 kinds of malformed entry, damaged headers, an erased sector, an empty and a full (85-entry) library. The golden ROMs are synthetic (generated filler plus a valid header); no real game data is committed.
- **Test of the tests:** removing the overlap check and removing the CRC check were each caught. Removing the sector rounding was not, and that is not a gap: with sector-aligned offsets and a sector-aligned end of the ROM area the rounding never changes a decision (equivalent change).
- **Found while committing:** `.gitignore` blocks `*.bin`, so the golden file was silently ignored; added a narrow exception for `tests/golden/*.bin`.
- **Cartridge RAM size code 5 (64 KiB) is flagged as a bad header:** Peanut-GB's `gb_get_save_size()` has no entry for it (it would read past its table), so we do not offer such cartridges. Revisit if a wanted game uses it.
- **Still to do in Phase 3:** the UF2 hardware test with a throwaway file (item 13), the launcher and firmware integration (reading the index from flash, listing the ROMs, running the chosen one; replacing the embedded ROM), and the 1 MiB linker cap.

### 2026-11-25 — Phase 3: flash test firmware prepared (not yet run on hardware)

Serves backlog item 13 (test the UF2 flashing approach on hardware with a throwaway file). Built, not yet flashed.

- New build option `PB_FLASH_TEST` (`src/flash_test.cpp`): no emulator; reports the firmware image CRC (from the linker's `__flash_binary_end`), validates the library index in place, re-reads every ROM from flash and compares its CRC with the index, writes a one-page marker into the settings region if (and only if) that page is blank, and reports the first save-area sector. Procedure and expected results: `docs/HARDWARE_TESTS.md`, "Phase 3: flash and UF2".
- This is the **first flash write by our code**. It goes through `pb_flash_erase_allowed()` / `pb_flash_program_allowed()` and `flash_safe_execute()`. [VERIFY #3 partly resolved: `flash_safe_execute()` exists in pico-sdk 2.3.1.] We never start the second core, so the build defines `PICO_FLASH_ASSUME_CORE1_SAFE=1` (the SDK's documented switch for that case); otherwise `flash_safe_execute` would refuse to run because it cannot know what core 1 is doing. If a later feature starts core 1, this assumption must be revisited.
- The test library (`build-flashtest/flashtest_library.uf2`, 1,680 blocks, offsets `0x100000` to `0x169000`) is made from the synthetic ROMs only.
- Expected stale data: the earlier firmware builds embed a 1 MiB ROM that physically extends past `0x100000` (the library area), so before the library is flashed the index sector will hold old bytes, not erased flash.

### 2026-11-25 — Phase 3: stage A of the flash test found existing data in the settings and save regions

- On the author's device, the settings page at `0xA00000` and the first save-area sector at `0xA02000` are **not blank** (unknown data; first-sector CRC 52cc78b2). The test firmware did what it is built to do and refused to overwrite them.
- This contradicts the assumption in `docs/FLASH_LAYOUT.md` that nothing below `0xC00000` is in use apart from our own data. It is leftover data from earlier use of the device (not from this project).
- **Rule going forward:** firmware must never erase or program a page in the settings or save regions that holds data it did not write itself, without an explicit confirmation from the author. Phase 4 (saves) must therefore include a first-use step that asks before claiming those regions (or a dedicated "prepare saves" action), not silently erase them.
- This does not affect the library: the index and ROM area (`0x100000` and up) are only written by the PC tool's UF2, and earlier test builds that embedded a ROM already wrote over part of it.
- Next in Phase 3: stage B of the hardware test (flash the synthetic library UF2 and verify the firmware CRC and ROM data), then the launcher.

### 2026-11-25 — Phase 3: the UF2 library approach is verified on hardware (backlog item 13 done)

Test firmware `PB_FLASH_TEST` (build of 11:11:19, image 147,880 bytes, CRC-32 a23a045c) and a library UF2 of three synthetic ROMs (32 KiB, 128 KiB, 256 KiB). Procedure in `docs/HARDWARE_TESTS.md`.

- **Stage B (flash `flashtest_library.uf2` over the running test firmware):** the device restarted into the firmware; library sector `OK, 3 ROMs`; all three ROMs read back from flash through XIP with CRCs equal to the index (3 match, 0 mismatch). The firmware's CRC-32 reported by the device (`a23a045c`) is **identical to the CRC-32 computed on the PC from `picoboy.bin`**, so the library UF2 did not change a single byte of the firmware.
- **Stage C (reflash the firmware UF2 over the library):** library still `OK, 3 ROMs`, all ROM CRCs still match, firmware CRC unchanged. So the everyday workflow works: new firmware builds can be flashed without redoing the library, and a new library without redoing the firmware.
- **Result:** the plan's central assumption (a UF2 covering only the index and ROM area leaves the rest alone, and ROMs are read correctly in place) is confirmed on this device. [VERIFY from PLAN.md section 5 resolved.]
- The settings marker was not written: the page at `0xA00000` holds unknown data on this device (see previous entry), and the test firmware refuses to overwrite it. So the first write to flash by our own code (`flash_safe_execute`) has **not** been exercised yet; that waits for Phase 4 and the author's confirmation to claim the save regions.
- Observation: all 3,072 sectors of the first 12 MiB hold data (nothing is erased); the first bytes at `0xA00000` are `fa 08 00 00 f4 08 00 00`, at `0xA02000` `fd 08 00 00`; origin unknown.

### 2026-11-25 — Phase 3 result: the device boots to a game list from the flash library

Serves Phase 3 acceptance ("ROMs flashed via UF2 appear in the list and launch; firmware and save areas survive a library reflash"). Tested on the author's PicoSystem with a library of two real ROMs (Zelda: Link's Awakening, 512 KiB MBC1; Pokemon Red, 1 MiB MBC3) made from the git-ignored `roms/` folder, and the default (no embedded ROM) firmware, `build-lib`.

- **Reported by the author:** the list appears; A starts each game; holding X+Y returns to the list; no problems seen. Zelda's picture quality (mid-frame effects in a line-by-line renderer) was not described beyond "nothing wrong"; to be recorded in `docs/COMPAT.md` in Phase 7.
- Verified earlier (stages B and C): the library UF2 leaves the firmware byte-identical and survives a firmware reflash, and the library is validated and read in place.
- **Design:** `launcher/` (list logic host-tested: wrap-around, scrolling, reasons a game cannot run, labels; screen drawn through `platform/ui`), `src/main.cpp` mode switch (list or game, plus the embedded-ROM development path), `audio_reset()` so the speaker is silent between games, the game-area border is cleared from `draw()` (not `update()`, which may run during the previous picture's transfer), and the cart RAM buffer (32 KiB) is cleared at each game start (no saves yet).
- **Firmware size cap (1 MiB) now enforced by the linker:** `ld/picoboy_memmap.ld.in` takes its FLASH length from `PB_FW_SIZE` in `storage/flash_layout.h` via CMake; a deliberate test with a 100 KiB cap failed with "region FLASH overflowed by 77288 bytes". The launcher firmware is 179,688 bytes (17% of the limit). Development builds with an embedded ROM get the whole user flash instead (they exceed 1 MiB by design).
- **Known gaps:** saves are not kept (RAM only; Phase 4, and it needs the author's confirmation to use the save regions because they currently hold unrelated data); the launcher's greyed-game and no-library screens were not exercised on hardware; list scrolling has only been tested on the PC (only 2 games on the device); no performance numbers yet for games started from the library (same code path as the embedded builds).

### 2026-11-25 — Phase 4 starts: the author agrees to claim the save regions; power-loss-safe save store (host-tested)

- **Decision (author, on being asked): option 1.** The settings and save regions (`0xA00000` to `0xC00000`), which currently hold unrelated old data on the device, may be claimed and erased for saves. The firmware will still ask on the device before erasing a region that is not blank and not ours (rule recorded earlier), so the protection holds for any user.
- **Save store** (`storage/save_store.{h,c}`, format in `docs/FLASH_LAYOUT.md`): claim sector, per-game pair of copies (header sector + payload sectors), header programmed last as the commit, payload and header CRCs, wrap-safe sequence numbers, free-run allocation, flash reached through an ops table. All erase/program calls pass `pb_flash_erase_allowed` / `pb_flash_program_allowed`. On Cortex-M0+: 2,417 bytes of code, 120 bytes of stack at most, 1,568 bytes of RAM for the store.
- **Tests** (`tests/test_save_store.c`, 11 host tests pass in total): a strict simulated flash (counts any erase or program outside the regions, unaligned ranges, or programming bytes that were not erased); round trips for 11 sizes including page and sector boundaries; alternation; reopen from flash; foreign data and a foreign "copy" header from another store are ignored; formatting changes only the claim sector; corrupted payloads and headers fall back to the older copy; sequence wrap-around; a full region fails cleanly and damages nothing. **Power is cut at every flash operation of a save (first write, second write, third write; 5 sizes; 4 partial-progress levels x 4 tear points inside and beyond the header), and after every cut the store reopens and loads either the previous or the new save, never anything else, and recovers with the next write.**
- **Test of the tests:** five deliberate breaks were all caught: overwriting the newest good copy, loading without the payload CRC, ignoring the header CRC, comparing sequence numbers without wrap-around, and programming without erasing first. A first version of the tear test only tore pages halfway, which can never land inside the 32-byte header; tear points 1, 16 and 31 bytes were added.
- Not done yet: the device side (`flash_safe_execute` operations, the on-device claim prompt, loading at game start, saving after the game stops writing for about 2 s and when returning to the list), and an on-hardware power-cut test. The first erase and program by our code on the real device happens in that step.

### 2026-11-25 — Phase 4: first flash write on the device froze it; cause and fix (SDK believed the flash was 2 MiB)

- **Symptom (author):** pressing A on the "SAVE AREA IN USE" question left the screen unchanged, and after a power cycle the question came back (the erase never happened). The device still enumerated over USB, but opening its serial port blocked: the CPU was stuck with interrupts disabled.
- **Cause:** the SDK's `flash_range_erase()` and `flash_range_program()` begin with `hard_assert(flash_offs + count <= PICO_FLASH_SIZE_BYTES)`, which is always active. The default board (`pico`) says 2 MiB; the PicoSystem has 16 MiB and our settings and save regions are at `0xA00000` (10 MiB). The assertion failed, and the SDK's panic handler halts forever, here inside `flash_safe_execute` with interrupts off. The author's earlier game (Tomodachi) saves at 1.5 MiB, inside the 2 MiB, which is why that worked. The earlier flash test firmware never wrote (the marker page held unknown data), so the first real write of this project exposed it.
- **Proof before fixing:** a new `static_assert(PICO_FLASH_SIZE_BYTES >= PB_FLASH_USER_END)` in `platform/flash_ops.cpp` failed to compile on the old configuration. In the binary, the limit compared in `flash_range_erase` was `128 << 14` (2 MiB) before the fix and is `128 << 17` (16 MiB) after.
- **Fix:** the target defines `PICO_FLASH_SIZE_BYTES=16777216` (CMakeLists.txt). The static assert stays, so a wrong value is a build error and never a frozen device. (Using `PICO_BOARD=pimoroni_picosystem`, which exists in the SDK, would also say 16 MiB but would change the boot loader selection too; the minimal change was preferred.)
- **Lesson, same family as the DMA channel 0 collision:** any SDK function with hidden board assumptions must be checked against this hardware before its first use. Items checked so far: DMA channel 0 (screen), SDK audio timer, flash size. Still to watch: nothing known.

### 2026-11-25 — Saving to flash is switched off by default (author's decision)

- **Decision (author):** play without saving to flash for now ("i think i should be fine without it"). In-game saves therefore live only in RAM: they last while the device stays on and the game keeps running, and are lost at power-off and when returning to the game list with X+Y.
- **How:** saving is a CMake option, `PB_SAVES`, default OFF. With it off none of the save code is compiled in (no save store, no save policy, no flash driver, no "SAVE AREA IN USE" question, no "SAVES ..." text or "saved" tags, no "SAVED" overlay): `app/save_manager.h` supplies do-nothing versions, and the firmware contains no `flash_range_erase`, `flash_range_program`, `flash_safe_execute` or save function (checked with `nm`). The tested code stays in the repository: `storage/save_store`, `storage/save_policy`, `platform/flash_ops`, `app/save_manager`, and the host tests (13 pass). It still builds with `-DPB_SAVES=ON`.
- **What was learned and is kept for when saving is wanted again:** (1) the first real flash write froze the device because the SDK believed the board had 2 MiB of flash (fixed with `PICO_FLASH_SIZE_BYTES=16777216`, guarded by a `static_assert`); after that fix the game's save was written ("SAVED" in green) but CONTINUE did not appear after a power cycle, and the cause was not found: the likely candidates are a save that was never really stored (now caught by reading every write back and failing with "SAVE FAILED"), or the question appearing again at boot and re-claiming the region (which forgets earlier saves by design). The last firmware that shows what the device finds after a power-off (`SAVES ON n stored`, `saved` tags, `SAVE LOADED`/`NO SAVE YET`) is the PB_SAVES=ON build. (2) The 12 MiB save region on this device holds unrelated old data that the firmware will not erase without a question.
- **Left on the device:** the one test that was run may have written a small "claim" marker and one save into `0xA00000` to `0xC00000`. Nothing else outside our own regions was touched. This region is not used by the default firmware.
- PLAN.md Phase 4 stays unfinished (acceptance "save, power off, power on, state restored" not met); to resume, build with `-DPB_SAVES=ON`.

### 2026-11-25 — Saving is back on by default (author's clarification)

- The previous entry switched saving off because "remove this feature" was understood as removing saving. The author clarified: **the in-game save must still be there after the PicoSystem is turned off and on; "i want it to stay."** Saving is therefore ON by default again (`PB_SAVES=ON`); `-DPB_SAVES=OFF` still builds a firmware with no flash-writing code.
- State of the problem: with the build that had the 16 MiB flash fix but no read-back check (efd9492), the game's in-game save was written ("SAVED" in green) but CONTINUE was missing after a power cycle. Nothing was logged (no USB cable), so the cause is unknown. The PC tests (13) show the store logic keeps saves across reopen and power cuts at every step, so the suspects are on the device side: the claim question coming back at boot (a second claim forgets earlier saves by design), or writes that the chip accepts but does not keep. The current build reads every claim and every save back from flash and fails visibly ("Saves unavailable (error N)" after the question, red "SAVE FAILED" in the game), and shows what survived a power cycle on screen: `SAVES ON n stored` in the list header, a green `saved` tag per game, `SAVE LOADED` / `NO SAVE YET` when a game starts.
- Not yet verified on hardware: that the 16 MiB fix plus read-back build keeps a save across a power cycle. Procedure: `docs/HARDWARE_TESTS.md`, "Phase 4: saves".

### 2026-11-25 — V1 declared stable by the author

- **Verified by the author on hardware (final firmware, commit 09dcc17):** boots to the game list, starts games with picture and sound, X+Y returns to the list, and the in-game save of the game survives turning the PicoSystem off and on. Phase 4's acceptance ("save in a game, power off, power on, state restored") is met for that case.
- **What fixed the persistence problem is not established.** The build that failed (efd9492) and the one that worked differ by: reading every claim and save back from flash and failing loudly, on-screen status (`SAVES ON n stored`, `saved` tags, `SAVE LOADED` / `NO SAVE YET`), and a skip of writes whose RAM is identical to what is stored. Candidate explanations (the claim question returning at boot and re-claiming, or an incomplete write) were never confirmed because no log was available. If it recurs, the on-screen status and the serial log (`saves: ...` lines) are the starting point.
- **Open items:** the real-flash power-cut test; Zelda's save and picture quality (Phase 7 notes); an in-game menu (Phase 6); the repository licence (open decision 4); the plan's Phase 7 compatibility pass; performance numbers for games started from the list.
- Repository now has a README. The V1 state is tagged `v1.0`.

### 2026-11-25 — Saves should follow the in-game SAVE, not any RAM write (found with the compatibility tool)

- **A claim made earlier in this log was wrong:** "Game Boy games write battery RAM almost only when you save". Measured with the new host tool `tests/play_rom.c` (same emulator core as the firmware, scripted buttons, per-bank RAM tracking) on Pokemon Red from power-on: during the intro and play, **only RAM bank 0 changes** (275 frames with changes, 20,455 byte changes up to the start of the game) and banks 1 to 3 never do. Bank 0 is the game's scratch space for decompressing sprites. A scripted in-game SAVE (START menu, SAVE, YES) changes **bank 1** (121 bytes in 6 frames) and leaves bank 0 alone.
- **Consequence for the earlier rule** ("save 2 s after the game stops changing its RAM"): with Pokemon it would have saved to flash at moments such as the start of a battle, each with a half-second pause, which is what the author asked to avoid.
- **Change:** the core counts RAM changes per 8 KiB bank (`pb_core_cart_ram_generation_banks`, host-tested with a two-bank ROM). The save manager takes the game's title and, for POKEMON RED / BLUE / YELLOW / GREEN, only counts banks 1 and up when deciding that the game saved; a flush still writes the whole RAM. Other games keep "any change" (the table is deliberately small and evidence-based; Yellow, Blue and Green were assigned by analogy with Red and are **not** verified). Saving on leaving a game uses the same rule.
- Not yet checked on the device: that the device now saves only after the in-game SAVE for Pokemon Red (expect no `SAVED` in the bar at battle starts), and what Zelda's RAM does (see `docs/COMPAT.md`).

### 2026-11-25 — Licence and public release
- **Decision:** PicoBoy's own code is MIT-licensed, "Copyright (c) 2026 alexJSanchez". Chosen because the owner wants anyone to be able to use the project, has no company, and does not want to claim exclusive rights. MIT is compatible with every dependency (Peanut-GB/minigb_apu MIT, pico-sdk BSD-3, picosystem MIT, TinyUSB MIT).
- Licence texts of the dependencies are kept in `licenses/` and shipped in the release zip, because the firmware contains their code.
- Releases: pushing a `v*` tag runs `.github/workflows/release.yml`, which builds the default firmware and publishes only `picoboy.uf2`, a checksum and the licence zip. It refuses to publish if `PB_EMBED_ROM` is set or the binary is over 400,000 bytes. **The workflow has not yet run on GitHub** (only its check was run locally).
- Never published: `library.uf2`, or any firmware built with a ROM embedded.

### 2026-11-25 — Delete one game's save from the game list

Serves the Phase 4 save mechanism (author's request after V1; not a numbered backlog item). Chosen by the author: per-game delete, triggered by a button combination in the list. Nothing about the flash layout or the save format changed.

- **Storage:** new `pb_save_delete(store, key)` in `storage/save_store.c` erases every copy of that game's save (header sector first, then the payload), reads the header back to check it is blank, and removes the copy from the table. It never touches the flash outside the settings and save regions (same `pb_flash_erase_allowed` guard as saving).
- **Power-cut rule:** copies are erased oldest first, newest last. If power goes in the middle, the game keeps its newest save (the delete can be repeated) or has none; an older save never comes back. Host tests cut the power at every flash operation with partial erases, for 1, 2 and 3 saves written. Deliberately reversing the order makes those tests fail, so they do test it. A chip that reports success without erasing is caught (the table is rescanned and `PB_SAVE_ERR_FLASH` returned).
- **App:** `saves_delete(key)` refuses to run while a game is running or saving is not ready. The launcher shows the option only for games that have a save, needs X+B held for 2 seconds with no other button down (`lm_hold_*` in `launcher_model`, host-tested incl. clock wrap-around), then asks `DELETE THIS SAVE?` (A deletes, B keeps). A cannot start a game while X or B is held.
- **Not verified:** none of this has run on a device. It is listed in `docs/HARDWARE_TESTS.md` (D1 to D9). The erase takes about 9 sectors per copy for a 32 KiB save; the time per delete is not measured. The firmware compiles; no RAM was added beyond a few bytes (no new buffers), so `docs/MEMORY_BUDGET.md` was not regenerated.

### 2026-11-25 — Picture could freeze for good after one slow drawn frame (pacer fix; hypothesis, not yet confirmed on the device)

- **Report (author, Pokemon Yellow from the game list):** stays on the Pikachu title picture; the music keeps playing and the game goes on, but the screen does not change.
- **Found by reading `src/main.cpp` and `core/pb_pacer.c`, reproduced in a host simulation:** the planner draws a frame only if the estimated cost of a drawn frame (`g_cost_render_us`) fits the 23,000 us budget, and that estimate is updated only from frames that were drawn. One drawn frame over about 23 ms (for example a cold XIP cache in a ROM-heavy scene) raised it above the budget; after that no frame was drawn, nothing was measured and the estimate never came down. Emulation and audio were unaffected. In the simulation (light scene, one 30 ms drawn frame at refresh 50) the picture never updated again over 4,000 refreshes.
- **Fix:** `pb_cost_relax()` in `core/pb_pacer.h`: when frames ran but none was drawn, the draw estimate decays by 1/32 per refresh (not below the undrawn estimate). The planner then tries drawing again and measures the real cost. A scene that really is over budget is drawn only now and then (the previous behaviour for heavy scenes is unchanged: the existing "heavy" simulation still draws on 50% of refreshes). Tests in `tests/test_pacer.c` (recovery within about 40 refreshes; the test fails without the fix).
- **Not confirmed:** that this is what happens on the device. It matches the symptom; it was not seen in a log. PC runs of the same ROM and core show the title screen then the intro normally. Separately found on the PC: Yellow ignores Start/A before about frame 2000 (about 33 s after power-on); presses after that work.
- **To check on hardware:** flash the new firmware, start Pokemon Yellow, wait on the title screen about 35 s, then tap Y. The picture should change to Oak's intro. With a USB cable, the serial log line `est_r=` shows the draw-cost estimate; a value stuck above 23000 would confirm the diagnosis.
