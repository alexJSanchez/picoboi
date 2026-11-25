# Building PicoBoy from source

You only need this if you want to change the code or build the firmware yourself. To just use PicoBoy, see [USER_GUIDE.md](USER_GUIDE.md).

## What you need

| Tool | Version used | Notes |
|---|---|---|
| Arm GNU Toolchain (`arm-none-eabi-gcc`) | **14.2.Rel1** | [Download from Arm](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads). Put its `bin` folder on your `PATH`. Other versions probably work; this one is what CI and the author use. Distribution packages may be older. |
| CMake | 3.13 or newer | |
| Ninja | any | `-G Ninja` is used in the examples |
| Python 3 | 3.8 or newer | the library tool and some tests |
| A host C compiler (gcc, clang) | any | only for the PC tests, and so the SDK can build `picotool` and `pioasm` itself |
| git | | the dependencies are submodules |

## Get the code

```sh
git clone --recurse-submodules <this repository's URL>
cd picoboi
# or, after a plain clone:
git submodule update --init --recursive
```

The submodules are pinned: `third_party/pico-sdk` (2.3.1), `third_party/picosystem` and `third_party/peanut_gb` (v1.3.0).

## Build the firmware

```sh
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=third_party/pico-sdk
cmake --build build -j
```

The result is `build/picoboy.uf2`. Copy it to the PicoSystem as in the [user guide](USER_GUIDE.md#step-2--put-picoboy-on-the-picosystem). The firmware must fit in 1 MiB; the linker refuses to produce a bigger one so that it can never overwrite the game library.

**If your machine has no host C compiler** (typical on a fresh Windows install), the SDK cannot build its helper tools `picotool` and `pioasm` from source. Point the build at prebuilt copies of version 2.3.1 (Raspberry Pi publishes them in the [`pico-sdk-tools` releases](https://github.com/raspberrypi/pico-sdk-tools/releases)):

```sh
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=third_party/pico-sdk \
      -Dpicotool_DIR=<folder with picotoolConfig.cmake> -Dpioasm_DIR=<folder with pioasmConfig.cmake>
```

### Options

| Option | Default | What it does |
|---|---|---|
| `-DPB_SAVES=OFF` | ON | Build without saving: no code that can write to flash at all. |
| `-DPB_EMBED_ROM=<file>` | none | Development only: build one ROM into the firmware and run it directly, skipping the game list. Such a build is bigger than 1 MiB and **must not be flashed over a device that holds your game library**. Never publish a UF2 built with it. |
| `-DPB_BOOT_BENCH=ON` | OFF | Print frame timings for the first frames of an embedded ROM (needs `PB_EMBED_ROM`). |
| `-DPB_AUDIO_TEST_TONE=ON` | OFF | Play test tones instead of game sound. |
| `-DPB_FLASH_TEST=ON` | OFF | A diagnostic firmware that reports what is in the flash (see [HARDWARE_TESTS.md](HARDWARE_TESTS.md)). |
| `-DPB_HIGH_LCD_ACCURACY=0` | 1 | A Peanut-GB sprite option (about 1.5% faster, less accurate). |

## Run the tests (on the PC)

```sh
sh tools/fetch_test_roms.sh        # downloads two free test ROMs into tests/roms/ (git-ignored)
cmake -S tests -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

13 tests; they need no hardware. Tests that need a ROM that is not there are skipped, not failed. `build-host/play_rom` is a developer tool that runs a ROM with scripted buttons and takes screenshots (see [COMPAT.md](COMPAT.md)).

## Make a release (maintainers)

Pushing a tag that starts with `v` builds the firmware on GitHub and attaches `picoboy.uf2` to a new release (see `.github/workflows/release.yml`):

```sh
git tag -a v1.1 -m "PicoBoy v1.1"
git push origin v1.1
```

Only the firmware is published, never a library or a UF2 with games in it.

## Where things are

The layout of the repository and how the pieces fit together are in the [README](../README.md#how-it-works); the measurements and decisions behind them are in [DECISIONS.md](DECISIONS.md), the flash formats in [FLASH_LAYOUT.md](FLASH_LAYOUT.md), the RAM use in [MEMORY_BUDGET.md](MEMORY_BUDGET.md).
