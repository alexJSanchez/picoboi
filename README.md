# PicoBoy

**A Game Boy emulator for the [Pimoroni PicoSystem](https://shop.pimoroni.com/products/picosystem)** — the little RP2040 handheld with a 240×240 screen, a D-pad, four buttons and a piezo speaker.

Turn it on, pick a game from a list, play it with sound, and your in-game saves are still there after you switch it off. Games are loaded onto the device by dragging one file onto it; no cables, no special software on the device.

> **Status: V1.** Works end to end on the author's PicoSystem with *Pokémon Red* and *Zelda: Link's Awakening*. Original Game Boy (DMG) games only — no Game Boy Color. See [What works and what doesn't](#what-works-and-what-doesnt).

This repository contains **no games and no Nintendo software**. You bring ROM files from cartridges you own.

---

## What it does

- **Game list at boot** — shows every game you've put on the device, with size and a "saved" tag; the ones it can't run are greyed out with the reason.
- **Runs games at full speed** — 59.7 frames a second of game time, even though the PicoSystem's screen only refreshes 40 times a second.
- **Sound** — all four Game Boy sound channels through the PicoSystem's piezo speaker (mono; a piezo is a small speaker, so expect it to sound thin).
- **Fills the screen** — 1.5× scaling, 240×216 pixels.
- **Battery saves that survive power-off** — the game's own SAVE menu works as it did on a real cartridge. Each game keeps two copies and a power cut in the middle of a save can't destroy the previous one.
- **Easy game loading** — one PC command builds a `library.uf2` file; copy it onto the PicoSystem like a firmware update. Flashing the library never touches the firmware, and flashing new firmware never touches your games.

## Using it

### 1. Controls

| PicoSystem | Game Boy |
|---|---|
| D-pad | D-pad |
| A / B | A / B |
| X | Select |
| Y | Start |
| Hold **X + Y** for 1 second | back to the game list |
| Hold **X + Y + B** for 1 second | mute / unmute the speaker |

In the game list: **UP / DOWN** to choose, **A** to start.

### 2. Put the firmware on the PicoSystem

Build it (see [Building](#building-from-source)) or take `picoboy.uf2` from a build. Hold **X** while switching the PicoSystem on; a drive called `RPI-RP2` appears. Copy `picoboy.uf2` onto it. It restarts into the game list.

### 3. Put your games on

1. Put your `.gb` files in a folder called `roms/` (git ignores it).
2. Run:
   ```sh
   python tools/pack_library.py roms -o library.uf2
   ```
   It prints a table of your games and warns about any the emulator can't run. Up to 85 games, about 9 MB of ROM in total.
3. Hold **X**, switch on, copy `library.uf2` onto the `RPI-RP2` drive.

Do not share a `library.uf2` that contains games you don't have the right to distribute.

### 4. Saving

Use the game's own SAVE command. About two seconds after the game stops writing to its battery RAM, PicoBoy copies it to flash and shows a green **SAVED** above the picture. The picture and sound pause for about half a second while it writes. Next time you open the list, that game carries a green **saved** tag and starts from its save.

The first time, if the save area of the flash holds other data, the device asks before erasing it.

---

## How it works

```
 ┌───────────────────────────────────────────────────────────────┐
 │  src/main.cpp   mode switch: game list ⇄ game, statistics       │
 ├──────────────┬──────────────┬───────────────┬───────────────────┤
 │ launcher/    │ core/        │ storage/      │ platform/         │
 │ game list    │ Peanut-GB    │ flash layout  │ screen, buttons,  │
 │ (logic is    │ adapter,     │ ROM library   │ speaker, flash,   │
 │  host-tested)│ pacer, audio │ save store    │ on-screen text    │
 │              │ sync         │ (pure logic)  │ (the only code    │
 │ app/         │ (pure C)     │               │  that touches the │
 │ save manager │              │               │  Pico SDK)        │
 └──────────────┴──────────────┴───────────────┴───────────────────┘
        third_party/: Pico SDK · PicoSystem SDK · Peanut-GB (+ its audio)
```

The rule that shapes the code: **`core/` and `storage/` are plain C with no hardware headers**, so everything that can corrupt data or miscount time is tested on a PC.

### The emulator

[Peanut-GB](https://github.com/deltabeard/Peanut-GB) (a small single-header Game Boy emulator, MIT) does the CPU and picture; its bundled `minigb_apu` does the sound. `core/pb_core.c` is a thin adapter around it with a small API: give it a pointer to the ROM, a buffer for the cartridge's battery RAM and a few callbacks.

**ROMs are never copied into RAM.** The RP2040 maps its flash into the address space (XIP), so a ROM is read in place, straight from where `library.uf2` put it. That's how a 1 MB game fits on a chip with 264 KB of RAM.

### Running at the right speed

A Game Boy runs at 59.73 Hz, but the PicoSystem's screen refreshes at 40 Hz (the SDK configures the panel that way). PicoBoy therefore keeps time itself:

- Each screen refresh, it works out how many game frames are due (1, 2, 1, 2… at 40 Hz) and runs them.
- Sending a picture to the screen takes about 12 ms of the 25 ms refresh, in the background. The emulator runs *during* that transfer into a private buffer, and the final step copies the finished picture into the screen buffer — so emulation gets almost the whole refresh.
- A budget scheduler estimates how long a frame takes with and without drawing. If there isn't time to do both, it still runs every frame (so game speed and music stay correct) and skips drawing for that refresh. The picture then updates a bit less often in the busiest scenes instead of the game slowing down.

Measured in the author's scenes: game speed 59.77 fps, the screen at its 40 Hz, no dropped frames. A frame costs roughly 1.5–9 ms to emulate and 4–8 ms more to draw, depending on the scene; audio adds about 1 ms.

### Sound

The emulator produces 548 mono samples per game frame. They go through a small ring buffer to the speaker's PWM output, fed by DMA at 32.73 kHz, so the CPU does no per-sample work. The two clocks (the game's and the speaker's) can't match exactly, so a small piece of logic (`core/pb_audio_sync`) nudges the stream by one sample when it drifts and recovers cleanly from underruns instead of replaying stale sound.

### The game library

`tools/pack_library.py` reads each ROM's header and writes a UF2 that contains **only** two things: a 4 KB *index* (names, sizes, checksums, what mapper and how much save RAM each game needs, and flags for games that can't run) and the ROMs themselves, back to back, each starting on a flash-sector boundary. The firmware validates the index (magic, version, CRC, bounds, no overlaps) before trusting it and reads it in place.

Because the UF2 holds only those blocks, copying it onto the device leaves the rest of the flash alone. That was verified on the hardware: after flashing a library, the firmware's checksum on the device was identical to the firmware file's checksum on the PC.

### Saving

Flash can only be erased in 4 KB sectors and wears out, and a power cut mid-write is a certainty over enough time. So:

- **Two copies per game**, written alternately. A save erases and fills the *older* copy and writes its header **last**. Until that header is complete the copy doesn't count, so the previous save is still there.
- Every copy has a CRC; a damaged one is skipped and the other is used. Sequence numbers wrap safely.
- After writing, the firmware **reads the data back** and only then says SAVED.
- It saves 2 seconds after the game goes quiet (not on every write), when you leave the game, and never if nothing changed.
- It only ever touches its own 2 MB region (`0xA00000`–`0xC00000`), guarded in code and by compile-time checks.

### Flash map

The PicoSystem has 16 MB of flash. PicoBoy uses only the first 12 MB; the SDK reserves the top 4 MB for its own filesystem.

| Region | Offset | Size | Written by |
|---|---|---|---|
| Firmware | `0x000000` | 1 MiB (the linker enforces it; the firmware is ~190 KB) | flashing the firmware |
| Library index | `0x100000` | 4 KiB | `pack_library.py` |
| ROM area | `0x101000` | ~9 MB | `pack_library.py` |
| Settings / claim | `0xA00000` | 8 KiB | the firmware |
| Save slots | `0xA02000` | ~2 MB (about 28 games with two copies each) | the firmware |
| *not ours* | `0xC00000` | 4 MiB | the PicoSystem SDK |

Details and the on-flash formats: [docs/FLASH_LAYOUT.md](docs/FLASH_LAYOUT.md). RAM budget (about 24 KiB free): [docs/MEMORY_BUDGET.md](docs/MEMORY_BUDGET.md).

---

## Building from source

You need the Arm GNU Toolchain 14.2 (`arm-none-eabi-gcc`), CMake, Ninja and Python 3. Exact commands, and notes for a machine without a host C compiler (prebuilt `picotool`/`pioasm`), are in [CLAUDE.md](CLAUDE.md).

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=third_party/pico-sdk
cmake --build build -j
# firmware: build/picoboy.uf2
```

Options: `-DPB_SAVES=OFF` (no code that can write to flash at all), `-DPB_EMBED_ROM=<file>` (development: run one ROM directly), plus test builds `PB_BOOT_BENCH`, `PB_AUDIO_TEST_TONE` and `PB_FLASH_TEST` (see [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md)). If a firmware ever grows past 1 MiB the link fails, so it can't overwrite your games.

## Testing

```sh
sh tools/fetch_test_roms.sh        # downloads two free test ROMs into tests/roms/ (not committed)
cmake -S tests -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host
```

Thirteen test programs run on the PC (and in CI on every push): Blargg's `cpu_instrs` (all 11 sub-tests pass through the adapter), `dmg-acid2`, tone and pitch checks for the sound, the audio drift logic, the frame pacer and scheduler, the flash layout guards, the ROM library reader against a golden file made by the Python tool, the Python packer, the launcher logic, and the save store. The save store test uses a strict simulated flash and **cuts the power at every flash operation of a save, with several amounts of partial damage, and checks the game always gets back either its old or its new save, never garbage**. Tests were also checked the other way: the code was deliberately broken in several ways to make sure each test notices.

What can only be checked on a real device (flash behaviour, speaker, timing) is listed as manual steps in [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md).

## Things worth knowing if you write code for the PicoSystem

Learned the hard way, and written down in [docs/DECISIONS.md](docs/DECISIONS.md):

- **The screen refreshes at 40 Hz**, not 60 (the SDK sets the panel's frame-rate register to 40 Hz).
- **The SDK uses DMA channel 0 for the screen without claiming it.** A plain "claim any free channel" hands you the screen's channel and freezes the device.
- **The SDK runs a 1 ms timer that rewrites the speaker's PWM settings** even when silent; stop it before driving the speaker yourself.
- **The SDK thinks the board is a 2 MiB Pico.** Its flash erase/program functions halt the program (with interrupts off) for any offset past that, so writing beyond 2 MiB needs `PICO_FLASH_SIZE_BYTES=16777216`.
- Moving the emulator's code from flash into RAM bought only ~3%; the cost is the picture drawing and the CPU emulation, not instruction fetches.

## What works and what doesn't

**Works:** DMG games with the common mappers (none, MBC1, MBC2, MBC3, MBC5); sound; battery saves; a game list from flash.

**Doesn't:**
- Game Boy Color games (they're listed and greyed out).
- An in-game menu (volume, scaling, palettes) — the X+Y shortcuts are the temporary stand-in.
- Link cable, save states, installing games from the device itself.
- The emulator draws one line at a time, so a game that changes the picture in the middle of a frame can look wrong. Compatibility has only been checked on a few games.
- Saving was tested on one device. A cut-the-power-at-random-moments test on real flash hasn't been done.

The plan and the order things were built in are in [docs/PLAN.md](docs/PLAN.md).

## Repository layout

| Folder | What's in it |
|---|---|
| `core/` | Peanut-GB adapter, frame pacer and scheduler, audio sync (plain C) |
| `storage/` | flash layout, ROM library reader, save store and save policy, CRC-32 (plain C) |
| `platform/` | screen, buttons, speaker, flash driver, on-screen text — the only code that uses the Pico/PicoSystem SDK |
| `launcher/`, `app/` | the game list and the save manager |
| `src/` | application glue (`main.cpp`) and the test builds |
| `tools/` | `pack_library.py`, `fetch_test_roms.sh` |
| `ld/` | the linker script (firmware size cap, emulator code in RAM) |
| `tests/` | the host tests |
| `docs/` | plan, decisions and measurements, flash layout, memory budget, hardware tests |
| `third_party/` | git submodules: Pico SDK, PicoSystem SDK, Peanut-GB |

## Legal

PicoBoy is an emulator. It contains no game code, no console boot ROM, and no Nintendo artwork. "Game Boy" and the names of games are trademarks of their owners; this project isn't affiliated with or endorsed by them. Only use ROMs you have the legal right to use.

**Licence:** not chosen yet. Until a licence file is added, the code is under default copyright (all rights reserved) and others have no permission to reuse it. Third-party components and their licences are listed in [THIRD_PARTY.md](THIRD_PARTY.md).

## Credits

[Peanut-GB](https://github.com/deltabeard/Peanut-GB) and `minigb_apu` by Mahyar Koshkouei (MIT, `minigb_apu` based on MiniGBS by Alex Baines), the [Pico SDK](https://github.com/raspberrypi/pico-sdk) by Raspberry Pi, and the [PicoSystem SDK](https://github.com/pimoroni/picosystem) by Pimoroni. Test ROMs: Blargg's `cpu_instrs` and Matt Currie's `dmg-acid2`, downloaded by script and never committed.
