# PicoBoy — Developer Plan (v1: original Game Boy / DMG only)
 
Target: Pimoroni PicoSystem (RP2040). Goal game for compatibility testing: Pokémon Red / Blue / Yellow (all run in DMG mode). Game Boy Color is **out of scope for v1**.
 
Status of this document: planning only. No emulator code exists yet. Items marked **[VERIFY]** are assumptions that must be confirmed on real hardware or against the vendored source before anything depends on them.
 
---
 
## 1. Scope
 
### In scope (v1)
- Run original Game Boy `.gb` ROMs on the PicoSystem screen, buttons and (optionally) piezo speaker.
- ROMs stored in the PicoSystem's onboard 16 MB QSPI flash, read in place (XIP), never copied wholesale to RAM.
- Battery saves (cartridge SRAM) persisted to flash with power-loss safety.
- Simple launcher listing the ROMs on the device.
- Host-side tooling to pack ROMs into a flashable UF2 (drag-and-drop, no custom USB protocol).
### Out of scope (v1)
- Game Boy Color (`.gbc`) and Game Boy Advance.
- Link cable, printer, save states, rewind, cheats.
- On-device ROM installation (v1 uses host-side packing).
- Shipping, bundling or committing any commercial ROM or Nintendo boot ROM.
### Stretch (only after v1 is stable)
- Audio quality improvements, 1.5× screen scaling, palettes, GBC feasibility spike.
---
 
## 2. Facts and assumptions
 
### Confirmed from public sources
| Fact | Source |
|---|---|
| PicoSystem: RP2040, 264 kB SRAM, 16 MB flash, 240×240 LCD, D-pad + A/B/X/Y, piezo speaker | Pimoroni product/repo pages |
| PicoSystem SDK has two screen modes: 240×240 native and 120×120 pixel-doubled (saves ~84 kB RAM). Colour is 16-bit with 4 bits per channel incl. alpha | pimoroni/picosystem README |
| SDK default CPU clock is 250 MHz unless `no_overclock(...)` is used | pimoroni/picosystem README |
| Default SDK sprite sheet costs 32 kB RAM; `no_spritesheet(<target>)` removes it | pimoroni/picosystem README |
| Out-of-tree build: `find_package(PICOSYSTEM REQUIRED)` then `picosystem_executable(...)` after `pico_sdk_import.cmake` | pimoroni/picosystem README |
| Peanut-GB is a single-header C library; the front end supplies ROM read, cart-RAM read/write and error callbacks; LCD and sound are optional and compile-time switchable | Peanut-GB docs / secondary sources |
| Peanut-GB sound needs an external APU (`minigb_apu`, separate repo) | Peanut-GB v1.2.0 release notes |
| Peanut-GB supports an optional boot ROM, so none is required | Peanut-GB v1.2.0 release notes |
| Peanut-GB renders line by line; a few games with mid-frame tricks render wrongly | Peanut-GB docs |
| RP2040-GB (Peanut-GB on RP2040) reported >70 fps with no audio on a small ILI9225 LCD; Pico-GB overclocks to 266 MHz to hold ~60 fps | those repos' READMEs |
 
### Not yet known — verify early **[VERIFY]**
1. Exact RAM the SDK reserves for the framebuffer in 240×240 mode (expected ≈112.5 KiB) — read the linker map.
2. How the SDK's `flip()` pushes pixels to the LCD (DMA/PIO) and whether it uses core 1.
3. Minimum Pico SDK version for `flash_safe_execute()` (`pico/flash.h`) and whether the PicoSystem SDK is compatible with the Pico SDK version you pin.
4. Size of Peanut-GB's `struct gb_s` in the pinned version.
5. Exact `lcd_draw_line` pixel format and the gamepad bit layout in the pinned `peanut_gb.h`.
6. Stable overclock speed and battery impact on your unit.
7. Whether `stdio_usb` logging works alongside the SDK on your build.
---
 
## 3. Architecture
 
```
┌────────────────────────────────────────────────────────┐
│ app/        state machine: boot → launcher → play → menu │
├──────────────┬──────────────┬──────────────┬───────────┤
│ launcher/    │ core/        │ storage/     │ platform/ │
│ list, select │ gb_core API  │ rom_library  │ display   │
│ settings UI  │ wraps        │ save_store   │ input     │
│              │ Peanut-GB    │ flash_layout │ audio     │
│              │              │ crc32        │ power/log │
└──────────────┴──────────────┴──────────────┴───────────┘
third_party/: pico-sdk, picosystem, peanut_gb, minigb_apu (all pinned)
```
 
Rules:
- `core/` knows nothing about the PicoSystem or flash. It receives a ROM pointer, a cart-RAM buffer and callbacks. This lets it build and run on a PC for tests.
- `storage/` knows nothing about the emulator. It manages flash regions and the ROM index.
- `platform/` is the only layer that touches Pimoroni SDK or Pico SDK hardware APIs.
- Anything in `core/` and `storage/` that is pure logic (index parsing, save-slot selection, CRC) gets host unit tests.
### Core adapter API (sketch; adjust after reading pinned `peanut_gb.h`)
```c
typedef struct pb_core pb_core_t;
 
typedef struct {
    const uint8_t *rom;          // XIP-mapped flash pointer, read-only
    uint32_t       rom_size;
    uint8_t       *cart_ram;     // RAM-resident save buffer
    uint32_t       cart_ram_size;
    void (*draw_line)(void *user, const uint8_t px[160], uint8_t line);
    void (*on_error)(void *user, int code, uint16_t addr);
    void  *user;
} pb_core_config_t;
 
int      pb_core_init(pb_core_t *c, const pb_core_config_t *cfg);
void     pb_core_set_buttons(pb_core_t *c, uint8_t mask);   // active-high in our API
void     pb_core_run_frame(pb_core_t *c);
bool     pb_core_cart_ram_dirty(pb_core_t *c);
void     pb_core_cart_ram_clear_dirty(pb_core_t *c);
uint32_t pb_core_audio_pull(pb_core_t *c, int16_t *out, uint32_t n); // phase 5
```
 
---
 
## 4. Memory budget (RAM, 264 KiB)
 
Estimates; replace with numbers from the linker map in Phase 2. **[VERIFY]**
 
| Item | Estimate |
|---|---|
| SDK framebuffer, 240×240 @ 16-bit | 112.5 KiB |
| SDK default sprite sheet (disabled with `no_spritesheet`) | 0 (was 32) |
| Peanut-GB state (WRAM 8 K + VRAM 8 K + OAM/HRAM/IO + counters) | ~19 KiB |
| Cart RAM (Pokémon Red/Blue/Yellow use 32 KiB) | 32 KiB |
| Hot emulator code copied to RAM (`__not_in_flash_func`) | ~24 KiB |
| Audio ring buffer (phase 5) | ~4 KiB |
| Stacks (2 cores) + heap | ~8 KiB |
| **Total** | **≈200 KiB** |
| **Headroom** | **≈64 KiB** |
 
Fallback if headroom disappears: bypass the SDK framebuffer and DMA each scanline to the LCD directly (Peanut-GB already produces one line at a time). Treat that as a Phase 2 option, not the starting point.
 
Note: 120×120 pixel-double mode is too coarse for a 160×144 screen. Use 240×240.
 
---
 
## 5. Flash layout (first 12 MiB of the 16 MiB chip)
 
Reserve regions in a custom linker script; **never** write to flash outside them. The exact numbers live in `storage/flash_layout.h` and `docs/FLASH_LAYOUT.md` (updated 2026-11-25: 12 MiB limit; the table below was changed to match).
 
| Region | Offset | Size | Purpose |
|---|---|---|---|
| Firmware | 0x000000 | 1 MiB | Code + constants. Linker `FLASH` length capped here so overflow is a build error |
| Library index | 0x100000 | 4 KiB | One sector: header + ROM entries |
| ROM area | 0x101000 | ≈9.0 MiB (to 0xA00000) | ROMs stored back to back, each 4 KiB-aligned, **contiguous** |
| Settings | 0xA00000 | 8 KiB | Two sectors (A/B copies), written by the firmware |
| Save slots | 0xA02000 | ≈2.0 MiB (to 0xC00000) | Battery saves, written by the firmware |
| Not ours | 0xC00000 | 4 MiB | Reserved by the PicoSystem SDK for its FAT filesystem; never touched (decision 2026-11-25) |
 
XIP base address is 0x10000000, so ROM N is read at `0x10000000 + entry.offset`. **[VERIFY region sizes against real firmware size and the PicoSystem bootloader.]**
 
### Why not LittleFS for ROMs
The emulator reads ROM bytes directly from XIP. That only works if each ROM is one contiguous flash range, which a general filesystem does not guarantee. Use a flat index for ROMs. A small log-structured store (or LittleFS) is fine for saves/settings only.
 
### Library index format (v1)
```
header:  magic "PBLB" | version u16 | count u16 | crc32 of entries
entry:   name[24] | offset u32 (from XIP base) | size u32 | rom_crc32 u32
         | mapper u8 | rom_banks u16 | ram_size u32 | flags u16
```
Entries are produced on the host by `tools/pack_library.py` from the ROM header (title 0x134, cart type 0x147, ROM size 0x148, RAM size 0x149). Unsupported mappers are flagged so the launcher can grey them out with a clear message.
 
### Getting ROMs onto the device (v1, no custom protocol)
`tools/pack_library.py *.gb -o library.uf2` emits a UF2 covering **only** the index sector and ROM area. Enter bootloader (hold X while powering on, per Pimoroni), drag the UF2 onto the RPI-RP2 drive. The bootrom only erases/writes blocks present in the UF2, so firmware and saves are untouched. **[VERIFY on hardware before trusting it with a real save.]** Alternative: `picotool load <bin> -o <address>`.
 
### Save store
- Slot per ROM, keyed by `rom_crc32`; size = cart RAM size rounded up to 4 KiB.
- Two alternating copies (A/B). Each has `seq u32 | crc32 | payload`. Write the older copy: erase, program payload, program header **last** as the commit marker. On boot, load the valid copy with the highest `seq`.
- Flush policy: cart RAM stays in SRAM while playing. Flush when it is dirty AND idle (no writes for ~2 s), on returning to the menu, on low battery, and before sleep. Never flush every write (flash endurance, and Pokémon writes in bursts).
- Flash erase/program stalls XIP. Use `flash_safe_execute()` and make sure core 1 and any DMA reading flash are parked or run from RAM. **[VERIFY SDK version and multicore lockout setup.]**
---
 
## 6. Performance plan
 
- Frame budget: Game Boy runs ≈59.7 Hz → 16.7 ms/frame. At 250 MHz that is ≈4.2 M cycles/frame.
- The reference RP2040 ports needed ~266 MHz for steady 60 fps on smaller panels with limited audio, so treat 60 fps on PicoSystem with audio as **unproven** until measured.
- Measure first. Add a benchmark build that runs a fixed ROM for 600 frames and prints min/avg/max frame time over USB serial.
- Levers, in order: (1) hot CPU/PPU functions in RAM, (2) `-O2`/`-O3` on core only, (3) compile Peanut-GB with unused features off (`ENABLE_SOUND` off for baseline), (4) core 1 does LCD push, (5) Peanut-GB frame-skip / interlace as a user option, (6) tune overclock vs battery.
- Targets: average ≥ 59.5 fps and no frame over 20 ms in three scenes: overworld walking, battle with animation, menu scrolling.
- Watch the 16 KB XIP cache: reading a ~1 MB ROM at random banks causes cache misses. Measure bank-switch-heavy scenes separately.
---
 
## 7. Display and input
 
- Native Game Boy screen 160×144 on the 240×240 panel.
- **Default: 1:1 centered** (offset x=40, y=48). Pixel-perfect, cheapest.
- **Option later: "fit" ≈1.5×** (240×216). Needs a repeating row/column pattern; evaluate cost after performance is known.
- Palette: DMG 4 shades mapped to a configurable palette (classic green default). Colour format is the SDK's 4-bit-per-channel.
- Buttons: A→A, B→B, D-pad→D-pad. PicoSystem has no Start/Select, so default **X→Select, Y→Start**. Menu opens on a long-press combo (default hold X+Y ≈1 s; configurable). Decide and record in `DECISIONS.md`.
---
 
## 8. Phases and acceptance criteria
 
Each phase ends with something runnable and a written result in `docs/DECISIONS.md`.
 
### Phase 0 — Toolchain and hello world (S)
- Submodules pinned: `pico-sdk`, `picosystem`. CMake configures and builds to a UF2.
- Firmware boots on hardware: splash, reads and displays button states, logs to USB serial (if available).
- **Accept:** UF2 flashes; all 8 buttons visibly respond; CI builds the UF2.
### Phase 1 — Core on screen with an embedded test ROM (M)
- Vendor Peanut-GB at a pinned commit under `third_party/`. Implement `core/` adapter and `platform/display`.
- ROM embedded in flash from a free test ROM (Blargg `cpu_instrs`, `dmg-acid2`, a homebrew game), read through XIP.
- **Accept:** Blargg `cpu_instrs` reports pass on screen; `dmg-acid2` renders recognisably correct; frame time logged.
### Phase 2 — Performance and memory pass (M)
- Linker map reviewed; memory budget table updated with real numbers.
- Hot code in RAM; benchmark harness; decide on core 1 and overclock.
- **Accept:** meets targets in section 6 for a homebrew game without audio, or a documented decision on frame-skip as default.
### Phase 3 — Flash partitioning and ROM library (M)
- Custom linker script caps firmware at 1 MiB. `flash_layout.h` is the single source of truth for addresses.
- `tools/pack_library.py` builds `library.uf2`. `storage/rom_library` parses and validates the index. Launcher lists titles, rejects unsupported mappers.
- **Accept:** two ROMs flashed via UF2 appear in the list and launch; firmware and save areas survive a library reflash.
### Phase 4 — Saves (L)
- Cart RAM in SRAM; A/B slot save store; dirty+idle flush; `flash_safe_execute()`.
- **Accept:** save in a game, power off, power on, state restored. Power loss during a flush (pull power in a loop test build) never leaves both copies invalid. Host unit tests cover slot selection and CRC failure cases.
### Phase 5 — Audio (L, optional gate)
- Integrate `minigb_apu` with integer-only mixing; mono PWM output on the piezo; ring buffer; user toggle.
- **Accept:** audio on keeps ≥ 59 fps in the benchmark scenes, or audio ships as "off by default / experimental" with the measured numbers recorded.
### Phase 6 — UX polish (M)
- In-game menu (resume, scaling, palette, brightness/volume if supported, quit), battery indicator, error screen from the `on_error` callback, button remapping.
- **Accept:** no dead-end screens; every error path shows a readable message.
### Phase 7 — Compatibility pass (M)
- Using ROMs you legally own: Pokémon Red/Blue/Yellow checklist — title screen, new game, overworld, wild battle, trainer battle, menus, save/load, bank-heavy scenes (Pokémon Center, big towns), 30+ minutes of play.
- Record issues per game in `docs/COMPAT.md`. Known Peanut-GB limits (line-by-line rendering) may break some titles; document, do not hide.
- **Accept:** a documented pass/fail table; Red/Blue playable without blocking bugs, or a list of the blockers with causes.
---
 
## 9. Testing strategy
 
- **Host unit tests** (`tests/`, plain CMake + a tiny test runner): index parsing, ROM header validation, save-slot A/B selection, CRC, flash-layout overlap checks (`static_assert` that regions don't overlap).
- **Host core runs**: build `core/` with a desktop frontend (or headless frame dump) to run test ROMs in CI.
- **Hardware smoke checklist** in `docs/HARDWARE_TESTS.md`, run before each release tag.
- **DERP emulator** (Daft-Freak's RP2040/PicoSystem emulator) for no-hardware smoke runs. It estimates display timing, so never use it for performance numbers.
- **Test ROMs** are downloaded by `tools/fetch_test_roms.sh` into an untracked `tests/roms/`. Check each ROM's license before vendoring anything.
---
 
## 10. Repository layout
 
```
picoboy/
├─ CLAUDE.md
├─ README.md
├─ CMakeLists.txt
├─ pico_sdk_import.cmake
├─ .gitignore                 # *.gb *.gbc *.sav *.uf2 build/ tests/roms/
├─ .github/workflows/build.yml
├─ docs/
│  ├─ PLAN.md
│  ├─ DECISIONS.md
│  ├─ FLASH_LAYOUT.md
│  ├─ MEMORY_BUDGET.md
│  ├─ HARDWARE_TESTS.md
│  └─ COMPAT.md
├─ ld/picoboy_memmap.ld       # firmware capped at 1 MiB
├─ third_party/               # git submodules, pinned
│  ├─ pico-sdk/
│  ├─ picosystem/
│  ├─ peanut_gb/
│  └─ minigb_apu/
├─ src/main.cpp
├─ app/
├─ platform/                  # display, input, audio, power, log
├─ core/                      # gb_core adapter (portable C)
├─ storage/                   # flash_layout.h, rom_library, save_store, crc32
├─ launcher/
├─ tools/                     # pack_library.py, fetch_test_roms.sh, bench_parse.py
└─ tests/                     # host-side unit tests
```
 
---
 
## 11. Build and CI
 
- Pin everything: Pico SDK tag, PicoSystem commit, Peanut-GB commit, `minigb_apu` commit. Record versions in `docs/DECISIONS.md`.
- CI: (1) build firmware UF2 and upload as artifact, (2) build and run host tests, (3) fail if firmware exceeds the 1 MiB region.
- Build flags: `-O2` default, `-O3` for `core/` if it measures faster, `-ffunction-sections -fdata-sections`, keep exceptions and RTTI off.
- Do not use `PICO_COPY_TO_RAM` for the whole binary; firmware will be larger than RAM. Place only hot functions in RAM.
---
 
## 12. Risks and open questions
 
| Risk | Impact | How we find out |
|---|---|---|
| 60 fps not reachable at full accuracy with audio | Playable but choppy | Phase 2 benchmark; frame-skip fallback; audio optional |
| SDK framebuffer + cart RAM leave too little headroom | Crashes or no audio | Linker map in Phase 2; bypass-SDK-framebuffer option |
| UF2-to-address flashing behaves differently on the PicoSystem bootloader | ROM loading plan changes | Test on hardware in Phase 3 with a throwaway ROM first |
| Flash write stalls/corrupts during save | Lost saves | A/B slots; `flash_safe_execute`; power-loss test in Phase 4 |
| Peanut-GB inaccuracy breaks a target game | Pokémon unplayable | Phase 7; consider a different core only if a blocker is traced to the core |
| Overclock stability / battery life | Crashes, short play time | Test on your unit; keep a `no_overclock` fallback |
| Piezo is a poor fit for 4-channel Game Boy audio | Weak sound | Treat audio as experimental |
 
### Decisions needed from you
1. Default control mapping and menu combo (section 7).
2. Is host-side UF2 packing acceptable for v1, or do you want on-device installation soon?
3. Is audio required for v1, or can it ship off by default?
4. Repo license (note: Peanut-GB and the Pimoroni SDK are MIT — verify on the pinned versions).
---
 
## 13. Licensing and legal hygiene
 
- Never commit ROMs, saves, UF2s containing ROMs, or any Nintendo boot ROM. `.gitignore` enforces it.
- A UF2 with a commercial ROM embedded or packed in it is a copy of that ROM; do not share it.
- Add `THIRD_PARTY.md` crediting each dependency with its license text.
- Keep the project name and UI text neutral: describe it as an emulator for original Game Boy games; do not use Nintendo or Pokémon artwork or logos.
---
 
## 14. Starter backlog (paste into issues)
 
1. Add submodules (`pico-sdk`, `picosystem`) pinned; make the scaffold configure and build a UF2.
2. Hello-world firmware: splash + live button display; confirm flashing on hardware.
3. Add `.gitignore`, `THIRD_PARTY.md`, `docs/DECISIONS.md` with recorded versions.
4. CI workflow: build UF2 artifact.
5. Read the linker map; fill `docs/MEMORY_BUDGET.md` with real numbers (framebuffer, `.bss`, stack).
6. Vendor Peanut-GB at a pinned commit; write the `core/` adapter and a host build that dumps a frame to PNG/BMP.
7. `fetch_test_roms.sh` + host test that runs `cpu_instrs` headless and checks the result.
8. `platform/display`: draw 160×144 centered from `draw_line`; DMG palette; flip per frame.
9. `platform/input`: button mapping and menu long-press combo.
10. Benchmark harness: 600-frame run, min/avg/max frame time over serial.
11. Custom linker script + `flash_layout.h` with `static_assert` region checks.
12. `tools/pack_library.py` + `storage/rom_library` with host unit tests.
13. Test the UF2 flashing approach on hardware with a throwaway ROM first.
14. Save store (A/B slots, CRC) with host unit tests against a simulated flash.
15. `flash_safe_execute` integration + power-loss test build.
