# PicoBoy

An emulator for original Game Boy (DMG) games on the [Pimoroni PicoSystem](https://shop.pimoroni.com/products/picosystem) (RP2040), built on [Peanut-GB](https://github.com/deltabeard/Peanut-GB). It boots to a list of the games on the device, plays them with sound, and keeps their battery saves in flash.

**Status: V1.** Tested on one PicoSystem with Pokemon Red and Zelda: Link's Awakening, with the games' own saves surviving power-off. Game Boy Color games are not supported.

This project contains **no games**. You bring your own ROM files, from cartridges you own. Nothing in this repository, and no UF2 you build with your games in it, should ever be shared.

## Using it

### Controls

| PicoSystem | Game Boy |
|---|---|
| D-pad | D-pad |
| A / B | A / B |
| X | Select |
| Y | Start |
| Hold X+Y for 1 s | back to the game list |
| Hold X+Y+B for 1 s | mute / unmute the speaker |

In the game list: UP / DOWN choose, A starts.

### Putting games on the device

1. Put your `.gb` files in a folder named `roms/` (it is ignored by git).
2. `python tools/pack_library.py roms -o library.uf2`. It lists each game and warns about any the emulator cannot run.
3. Hold **X**, power on the PicoSystem (the `RPI-RP2` drive appears), copy `library.uf2` onto it.

The firmware and the game library are separate files: flashing one leaves the other alone. Games and saves are stored in the first 12 MiB of the 16 MiB flash; the rest is left to the PicoSystem SDK.

### Saving

Games save the usual way (their own SAVE menu). About two seconds after a game stops writing to its battery RAM, the firmware copies it to flash and shows a green `SAVED` above the picture; the picture and sound pause for about half a second while it writes. The list shows a green `saved` next to games that have a save. Each game keeps two copies and a power cut during a save leaves the previous one intact. The first time, if the save area holds other data, the device asks before erasing it.

## Building

Needs the Arm GNU Toolchain 14.2 (`arm-none-eabi-gcc`), CMake, Ninja and Python 3. See [CLAUDE.md](CLAUDE.md) for the exact commands and for the notes about prebuilt `picotool` / `pioasm` on a machine without a host C compiler.

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=third_party/pico-sdk
cmake --build build -j
# build/picoboy.uf2 is the firmware
```

Host tests (need a host C compiler and the free test ROMs, which a script downloads):

```sh
sh tools/fetch_test_roms.sh
cmake -S tests -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host
```

Useful CMake options: `-DPB_SAVES=OFF` (no code that can write flash), `-DPB_EMBED_ROM=<file>` (development: run one ROM directly, skip the list), `-DPB_BOOT_BENCH=ON`, `-DPB_AUDIO_TEST_TONE=ON`, `-DPB_FLASH_TEST=ON` (hardware test builds, see [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md)).

## What works and what does not

- **Speed:** full game speed (59.7 fps). The PicoSystem's screen refreshes at 40 Hz, so in busy scenes the picture may update less often than the game runs; sound and timing stay right.
- **Sound:** all four channels through the PicoSystem's piezo speaker, mono. A piezo is a small speaker: expect it to sound thin.
- **Picture:** 1.5x scaling to fill the screen width. The emulator draws line by line, so a game that changes the picture in the middle of a frame may look wrong.
- **Not done:** Game Boy Color, an in-game menu (volume, scaling, palettes), link cable, save states, installing games from the device itself. See [docs/PLAN.md](docs/PLAN.md) for the plan and [docs/DECISIONS.md](docs/DECISIONS.md) for what was measured and decided along the way.

## Layout

`core/` portable emulator adapter, pacer and audio sync (no hardware headers) · `storage/` flash layout, ROM library index, save store (pure logic, host-tested) · `platform/` the only code that touches the Pico / PicoSystem SDK · `launcher/` and `app/` the game list and the save manager · `src/` application glue · `tools/` PC tools · `tests/` host tests · `docs/` plan, decisions, flash layout, memory budget, hardware tests.

## Licence and credits

Third-party code and its licences: [THIRD_PARTY.md](THIRD_PARTY.md). Peanut-GB (MIT), the Pico SDK (BSD-3-Clause), the Pimoroni PicoSystem SDK (MIT). This project's own licence has not been chosen yet.
