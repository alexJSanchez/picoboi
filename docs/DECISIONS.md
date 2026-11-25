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
