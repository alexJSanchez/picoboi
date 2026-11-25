# Memory budget

Numbers come from the linker output of a real build, not estimates. Replace them whenever the build changes (CLAUDE.md "definition of done").

Build measured: commit `810b122` (emulator core with audio, speaker driver, embedded ROM, off-screen frame buffer, emulator code running from RAM, SDK defaults, 240x240, 250 MHz, Release `-O3`), pico-sdk 2.3.1, picosystem `9a26b2a`, Arm GNU 14.2.Rel1.

How to reproduce:
```sh
arm-none-eabi-size -A build/picoboy.elf          # section sizes
arm-none-eabi-nm -S -C --size-sort -r build/picoboy.elf   # largest symbols
```

## RAM (264 KiB total)

The picosystem linker script (`third_party/picosystem/libraries/memmap_picosystem.ld`) defines: main RAM 256 KiB at `0x20000000`, SCRATCH_X 4 KiB at `0x20040000`, SCRATCH_Y 4 KiB at `0x20041000`. Core 0's stack sits in SCRATCH_Y (2 KiB reserved); SCRATCH_X is free because core 1 is not used.

| Item | Bytes | KiB |
|---|---:|---:|
| SDK framebuffer `picosystem::_fb` (240x240, 16-bit) | 115,200 | 112.5 |
| Cart RAM buffer `g_cart_ram` (sized for Pokemon, 32 KiB) | 32,768 | 32.0 |
| Emulator state `g_core` (`pb_core_t`, holds `struct gb_s` = 16,916 B) | 20,480 | 20.0 |
| Off-screen Game Boy frame `g_shade` (160x144, 1 byte per pixel) | 23,040 | 22.5 |
| Audio ring buffer `g_ring` (2,048 x 16-bit PWM levels) | 4,096 | 4.0 |
| Other `.bss` (USB, SDK tables, etc.) | 5,096 | 5.0 |
| **`.bss` total** | **200,920** | **196.2** |
| `.data` (initialised data copied from flash at boot, **includes the emulator and audio code, 22 KB, running from RAM**) | 32,028 | 31.3 |
| `.ram_vector_table` | 192 | 0.2 |
| `.heap` (reserved by the linker script) | 2,048 | 2.0 |
| **Used in main RAM** | **235,188** | **229.7** |
| Main RAM capacity | 262,144 | 256.0 |
| **Free in main RAM** | **26,956** | **26.3** |
| Core 0 stack (in SCRATCH_Y, separate from main RAM) | 2,048 | 2.0 |
| SCRATCH_X free / SCRATCH_Y remaining | 4,096 / 2,048 | 4.0 / 2.0 |

Compared with `docs/PLAN.md` section 4: the plan estimated about 200 KiB used and 64 KiB headroom; measured use is now 204 KiB with 52.1 KiB free in main RAM (it was 181 KiB / 74.7 KiB before the 22.5 KiB off-screen frame buffer was added for pacing, see `docs/DECISIONS.md`). Differences: the sprite sheet is in flash, not RAM (see `docs/DECISIONS.md`), `gb_s` is smaller than estimated, and hot code is not yet copied to RAM.

Not yet included (planned): hot emulator code in RAM (about 24 KiB estimated, Phase 2), audio ring buffer (about 4 KiB, Phase 5), launcher and save-store buffers (Phases 3 and 4). These fit inside the 52.1 KiB free, with room to spare for about 24 KiB of hot code plus audio and save buffers, but not much more.

The ROM is not in RAM: it is read-only data in flash and read in place through XIP.

## Flash (as built)

| Item | Bytes |
|---|---:|
| `.boot2` | 256 |
| `.text` (code) | 106,816 |
| `.rodata` (includes the 65,536-byte embedded test ROM and the 32,768-byte default sprite sheet) | 113,332 |
| Other (exception tables, binary info) | 952 |
| **Binary end** (`__flash_binary_end`, `0x100386f8`) | **231,160 (about 226 KiB)** |

The 1 MiB firmware region in `docs/PLAN.md` is not yet enforced by the linker (Phase 3).

### Open issue for Phase 3: PicoSystem's own flash map

`memmap_picosystem.ld` declares `FLASH` as **12 MiB** (`0x10000000`, "User Flash") and a `FAT` region of 4 MiB marked "Reserved for FAT". The plan (`docs/PLAN.md` section 5) places the save area at `0xE00000` to `0x1000000`, which falls inside the top 4 MiB (`0xC00000` and up). Before Phase 3 we must decide whether to use all 16 MiB (and overwrite whatever the stock PicoSystem firmware keeps there) or to stay within the first 12 MiB. See `docs/DECISIONS.md`.
