# CLAUDE.md — PicoBoy
 
Game Boy (DMG-only for v1) emulator for the Pimoroni PicoSystem (RP2040), built on Peanut-GB. Read `docs/PLAN.md` first; it is the source of truth for scope, phases, flash layout and acceptance criteria.
 
## How to work in this repo
- Work one phase / backlog item at a time. Do not start a later phase early.
- Before changing behaviour, state which phase and acceptance criterion the change serves.
- Measure before optimizing. Performance claims need frame-time numbers from the benchmark harness.
- Anything marked **[VERIFY]** in `docs/PLAN.md` is an assumption. Confirm it (read the vendored source, the linker map, or test on hardware) and record the result in `docs/DECISIONS.md` rather than assuming.
- If hardware access is needed to confirm something, say so and stop at that point; do not guess.
- Keep changes small and reviewable. One logical change per commit.
## Layering rules
- `core/`: portable C, no Pico or Pimoroni headers. Wraps Peanut-GB behind the `pb_core_*` API.
- `storage/`: flash layout, ROM index, save store. No emulator knowledge. Pure logic must have host unit tests.
- `platform/`: the only place that touches Pico SDK / PicoSystem SDK hardware APIs.
- `app/`, `launcher/`: UI and state machine; talk to the layers above only through their headers.
- `flash_layout.h` is the single source of truth for flash addresses. No hard-coded addresses elsewhere.
## Hard rules
- NEVER commit ROMs (`.gb`, `.gbc`), saves (`.sav`), UF2 files, or any Nintendo boot ROM. Do not add one to tests, docs or examples.
- NEVER write to flash outside the regions in `flash_layout.h`. All flash erase/program goes through the save/library code using `flash_safe_execute()`.
- Do not copy a whole ROM into RAM. ROMs are read in place through XIP.
- Do not add or upgrade dependencies (submodules, vendored files) without asking; versions are pinned and recorded in `docs/DECISIONS.md`.
- No C++ exceptions or RTTI. No dynamic allocation in the per-frame path.
- Do not change the DMG-only scope. Game Boy Color work needs an explicit decision recorded first.
## Build
```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=third_party/pico-sdk
cmake --build build -j
# arm-none-eabi-gcc must be on PATH. If the machine has no host C compiler, the SDK cannot
# build picotool/pioasm itself; add -Dpicotool_DIR=<prebuilt dir> -Dpioasm_DIR=<prebuilt dir>
# (see docs/DECISIONS.md, 2026-11-25).
# host tests
sh tools/fetch_test_roms.sh   # downloads free test ROMs into untracked tests/roms/
cmake -S tests -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host
# needs a host C compiler (gcc) on PATH; tests are skipped (not failed) if the ROMs are absent
```
Flashing: hold X, power on (RPI-RP2 drive appears), copy the `.uf2` over. [VERIFY these commands match the actual CMake setup once Phase 0 is done, and update this file.]
 
## Definition of done (every change)
- Builds clean for firmware and host tests; host tests pass.
- If it touches memory: `docs/MEMORY_BUDGET.md` updated from the linker map.
- If it touches flash layout or formats: `docs/FLASH_LAYOUT.md` and `flash_layout.h` updated together, with a `static_assert` guarding overlaps.
- Hardware-only behaviour is listed in `docs/HARDWARE_TESTS.md` as a manual check.
- Decisions and measured numbers recorded in `docs/DECISIONS.md`.
