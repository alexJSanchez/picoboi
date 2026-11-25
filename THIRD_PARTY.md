# Third-party software

PicoBoy's own code is under the [MIT licence](LICENSE). The firmware (`picoboy.uf2`) also contains the open-source components below, each under its own licence. Their full licence texts are in the [`licenses/`](licenses/) folder, so they can be kept together with any copy of the firmware. They are pinned git submodules under `third_party/`; exact versions are recorded in [docs/DECISIONS.md](docs/DECISIONS.md).

| Component | Used for | Licence | Full text |
|---|---|---|---|
| [Peanut-GB](https://github.com/deltabeard/Peanut-GB) v1.3.0 (Mahyar Koshkouei) | The Game Boy emulator itself. It contains a little code taken from [SameBoy](https://github.com/LIJI32/SameBoy) (Lior Halphon), also MIT | MIT | [licenses/Peanut-GB.txt](licenses/Peanut-GB.txt) |
| minigb_apu, the copy bundled in Peanut-GB (Mahyar Koshkouei, Alex Baines; based on [MiniGBS](https://github.com/baines/MiniGBS)) | Game Boy sound | MIT | [licenses/minigb_apu.txt](licenses/minigb_apu.txt) |
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.3.1 | Everything on the RP2040 chip (clocks, DMA, PWM, flash, USB glue) | BSD 3-Clause | [licenses/pico-sdk.txt](licenses/pico-sdk.txt) |
| [Pimoroni PicoSystem SDK](https://github.com/pimoroni/picosystem) (commit `9a26b2a`) | The screen, buttons and board setup of the PicoSystem | MIT | [licenses/picosystem.txt](licenses/picosystem.txt) |
| [TinyUSB](https://github.com/hathach/tinyusb) (through the Pico SDK) | USB serial log | MIT | [licenses/tinyusb.txt](licenses/tinyusb.txt) |

Also linked into the firmware, from the Arm GNU Toolchain 14.2 that builds it: the C library (newlib) and the compiler's runtime libraries (libgcc, libstdc++). Their licences (BSD-style for newlib; GPL with the GCC Runtime Library Exception for libgcc and libstdc++) allow distributing programs built with them; the toolchain's own licence files ship with the toolchain.

Build and test tools that are *not* part of the firmware: CMake, Ninja, Python, `picotool` and `pioasm` (Raspberry Pi, BSD 3-Clause).

Test ROMs used by the automatic tests are downloaded when you run `tools/fetch_test_roms.sh`, never committed, and not part of the firmware: Blargg's `cpu_instrs` and Matt Currie's [`dmg-acid2`](https://github.com/mattcurrie/dmg-acid2) (MIT). The two screenshots in `docs/images/` are the output of those programs.

This project is an emulator. It contains no game code, no console boot ROM, and no Nintendo artwork. It is not affiliated with or endorsed by Nintendo, Raspberry Pi or Pimoroni.
