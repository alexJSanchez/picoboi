# Third-party software

Each dependency is a pinned git submodule under `third_party/`. Exact versions are recorded in `docs/DECISIONS.md`. Full license texts are in each submodule (paths below).

| Dependency | Purpose | License | License text | Status |
|---|---|---|---|---|
| Raspberry Pi Pico SDK 2.3.1 | RP2040 SDK | BSD 3-Clause, (c) 2020 Raspberry Pi (Trading) Ltd | `third_party/pico-sdk/LICENSE.TXT` | added |
| TinyUSB (via the Pico SDK) | USB serial logging | MIT, (c) 2018 hathach | `third_party/pico-sdk/lib/tinyusb/LICENSE` | added (SDK submodule) |
| Pimoroni PicoSystem SDK (`9a26b2a`) | Display, input, audio for PicoSystem | MIT, (c) 2021 Pimoroni Ltd | `third_party/picosystem/LICENSE` | added |
| Peanut-GB v1.3.0 | Game Boy emulation core | MIT, (c) 2018-2023 Mahyar Koshkouei | full text at the top of `third_party/peanut_gb/peanut_gb.h` | added |
| minigb_apu | Game Boy audio (Phase 5) | _to verify on pinned version_ | | not added |

Build tools (not shipped in the firmware): Arm GNU Toolchain 14.2.Rel1, picotool 2.3.1, pioasm 2.3.1.

This project is an emulator for original Game Boy games. It does not include or distribute any game ROM or boot ROM.
