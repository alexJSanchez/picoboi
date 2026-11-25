# Handoff: where PicoBoy stands (2026-11-25)

Paste or upload this file at the start of a new Claude conversation, together with `CLAUDE.md` and `docs/PLAN.md`. (In Claude Code, opened in this folder, `CLAUDE.md` is read automatically.)

## What it is
PicoBoy: an original Game Boy (DMG only: not Game Boy Color, not GBA) emulator for the Pimoroni PicoSystem (RP2040), built on Peanut-GB. Repo: github.com/alexJSanchez/picoboi (possibly renamed `picoboy`). MIT licence. Written by the owner with Claude Code.

## Done and working on the device (V1)
- Toolchain, CI, hello-world (Phase 0); emulator on screen, Blargg `cpu_instrs` passes (Phase 1); full-speed pacing on a 40 Hz screen (Phase 2); sound on the piezo speaker; flash game library + game list at boot (Phase 3); saves that survive power-off, written only at the game's own SAVE (Phase 4).
- Tested on hardware with Pokémon Red and Zelda: Link's Awakening. 13 host tests pass.
- Public-release work: README, `docs/USER_GUIDE.md`, `docs/BUILDING.md`, `docs/COMPAT.md`, MIT `LICENSE`, `licenses/`, `THIRD_PARTY.md`, `CONTRIBUTING.md`, issue templates, release workflow (`.github/workflows/release.yml`), screenshots in `docs/images/`.

## Not verified yet (do these first)
1. **The release workflow has never run on GitHub.** After pushing tag `v1.0`, check the Actions tab; fix if red.
2. Device check of the Pokémon save rule (banks 0 ignored; green SAVED only after the in-game SAVE). Blue/Yellow/Green rule unverified.
3. Zelda gameplay past the name entry, and its in-game save.
4. A real power-cut test on actual flash (the PC test simulates it only).
5. A real photo of the device for the README.

## Next features (from `docs/PLAN.md`)
- Phase 6: in-game menu (volume, scaling, palettes) — X+Y hold shortcuts are the stand-in.
- Phase 7: wider game compatibility (mid-line picture effects, more mappers/games).
- Not planned: Game Boy Color (needs an explicit decision recorded in `docs/DECISIONS.md` first), link cable, save states.

## Rules to keep (from `CLAUDE.md`)
One phase/item at a time; small commits; measure before optimising; never commit ROMs, saves, game UF2s, or boot ROMs; flash addresses only in `storage/flash_layout.h`; only `platform/` touches the SDK; record decisions and numbers in `docs/DECISIONS.md`; hardware-only checks go in `docs/HARDWARE_TESTS.md`.

## Facts that cost time to find (details in `docs/DECISIONS.md`)
Screen refreshes at 40 Hz; SDK uses DMA channel 0 for the screen without claiming it; SDK 1 ms timer rewrites speaker PWM; SDK thinks flash is 2 MiB (needs `PICO_FLASH_SIZE_BYTES=16777216`); the user's PicoSystem button layout: A right, B bottom.

## Build quick reference
```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=third_party/pico-sdk
cmake --build build -j          # build/picoboy.uf2
python tools/pack_library.py roms -o library.uf2   # games, from the git-ignored roms/ folder
```
On the owner's Windows machine the prebuilt tools were needed: add `-Dpicotool_DIR=... -Dpioasm_DIR=...` (see `docs/BUILDING.md`).
