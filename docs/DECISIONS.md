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
| peanut_gb | _not added_ | | |
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
- **Button layout** (physical): X top, Y left, B bottom, A right. The test screen was corrected twice to match (commits `4209add`, `1446af4`). Re-check the final layout on the next flash.
- **Battery:** `picosystem::battery()` returns a percentage; the value shown looked plausible. (Exact scale and accuracy not measured; tidy up when a battery indicator is built in Phase 6.)
- **[VERIFY] #7 resolved: `stdio_usb` works alongside the PicoSystem SDK** (pico-sdk 2.3.1, picosystem `9a26b2a`). The device enumerates as a composite USB device, VID `2E8A` PID `000A`: a CDC serial port (COM7 on this PC, 115200 8N1) plus the SDK's vendor "Reset" interface, which Windows lists under "Universal Serial Bus devices" as "Reset". Button down/up lines were read from the serial port while pressing B, A, Y, X. The one-shot boot banner was not captured (port opened after boot); not a concern.
- Not yet checked: running on battery with no USB host (HARDWARE_TESTS.md row 9), and the exact splash timing.
- Remaining for Phase 0 acceptance: CI builds the UF2 (backlog item 4).
