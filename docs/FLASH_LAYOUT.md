# Flash layout

Source of truth: [`storage/flash_layout.h`](../storage/flash_layout.h). This page explains it; if the two ever disagree, the header wins (it has compile-time checks, and `tests/test_flash_layout.c` checks the write guards). Change both together (CLAUDE.md "definition of done").

The chip is 16 MiB. **We use only the first 12 MiB** (decision of 2026-11-25, see `docs/DECISIONS.md`): the PicoSystem SDK's own linker script (`memmap_picosystem.ld`) calls those 12 MiB "user flash" and reserves the top 4 MiB for a FAT filesystem. Staying out of it leaves whatever the stock firmware keeps there untouched.

Offsets are from the start of flash; the CPU sees flash at `0x10000000 + offset` (XIP).

| Region | Offset | Size | Written by | Purpose |
|---|---|---|---|---|
| Firmware | `0x000000` | 1 MiB | flashing the UF2 | Code and constants. The linker is capped here: a firmware over 1 MiB fails to link (`region FLASH overflowed`). Development builds with an embedded ROM (`-DPB_EMBED_ROM`) are the exception and may use the whole user flash |
| Library index | `0x100000` | 4 KiB | `tools/pack_library.py` (UF2) | One sector: header and ROM entries |
| ROM area | `0x101000` | 0x8FF000 (about 9.0 MiB) | `tools/pack_library.py` (UF2) | ROMs back to back, each 4 KiB aligned, one contiguous range per ROM, read in place |
| Settings | `0xA00000` | 8 KiB | firmware | Two sectors (A/B copies) |
| Save slots | `0xA02000` | 0x1FE000 (about 2.0 MiB) | firmware | Battery saves, A/B copies per game (Phase 4) |
| Not ours | `0xC00000` | 4 MiB | the SDK | Reserved for its FAT filesystem. Never touched |

## Rules

- The firmware only erases or programs the **settings and save-slot** regions (`0xA00000` to `0xC00000`). Everything else is written from the PC. `pb_flash_erase_allowed()` and `pb_flash_program_allowed()` in the header encode this (inside the writable area, sector or page aligned, no arithmetic overflow); all flash writes must check them (CLAUDE.md hard rule).
- Erase works on 4 KiB sectors, programming on 256-byte pages.
- ROMs are read in place through XIP; each ROM must be one contiguous range (that is why the ROM area is a flat index and not a filesystem).
- The firmware region is 1 MiB and the linker enforces it: `ld/picoboy_memmap.ld.in` is a template whose `FLASH` length CMake fills in from `PB_FW_SIZE` in `storage/flash_layout.h` (so the number is not repeated anywhere). The normal firmware uses about 180 KB of it. Development builds that embed a ROM (about 1.3 MiB for Pokemon) get the whole user flash instead and would overwrite a library: they are for benchmarking and tests only.

## Capacity

- ROM area: 9,433,088 bytes, so for example eight 1 MiB games (Pokemon Red/Blue/Yellow) with room to spare.
- Save slots: 2,088,960 bytes. A Pokemon save is 32 KiB; with two copies (A/B, PLAN.md section 5) that is 64 KiB per game, so about 31 games.

## Library index format, version 1

One 4 KiB sector at `0x100000`, little-endian. Written by `tools/pack_library.py`, read and validated in place by `storage/rom_library.c`. A golden file (`tests/golden/index_v1.bin`, made from synthetic ROMs) is checked by both the Python and the C tests, so the two sides cannot drift apart silently.

Header (16 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `PBLB` |
| 4 | 2 | version (1) |
| 6 | 2 | entry count (0 to 85) |
| 8 | 2 | entry size (48) |
| 10 | 2 | reserved (0) |
| 12 | 4 | CRC-32 of the entry bytes (`count * 48` bytes, standard zlib CRC-32) |

Entry (48 bytes), starting at offset 16 + 48 * i:

| Offset | Size | Field |
|---|---|---|
| 0 | 24 | name, printable ASCII, NUL terminated (from the ROM header title, else the file name) |
| 24 | 4 | flash offset of the ROM (from the start of flash, 4 KiB aligned; CPU address = `0x10000000` + this) |
| 28 | 4 | ROM size in bytes |
| 32 | 4 | CRC-32 of the whole ROM file |
| 36 | 4 | save RAM the cartridge needs in bytes (MBC2: 512) |
| 40 | 2 | number of 16 KiB ROM banks |
| 42 | 2 | flags (below) |
| 44 | 1 | mapper: 0 none, 1, 2, 3, 5 = MBC number, `0xFF` = not supported |
| 45 | 1 | cartridge type (header byte `0x147`) |
| 46 | 2 | reserved (0) |

The rest of the sector is `0xFF`.

Flags: `0x01` unsupported cartridge type, `0x02` Game Boy Color only, `0x04` bad header (checksum, size code or RAM size code the core cannot use), `0x08` file size does not match the header, `0x10` colour-enhanced but runs as DMG (informational). The first four block launching. The mapper table is copied from Peanut-GB v1.3.0 (`gb_init`); the core stays the authority and refuses a cartridge it cannot run.

What the firmware checks before using an index: not erased, magic, version, count and entry size, CRC, then for every entry: a printable non-empty name, size of at least `0x150`, sector-aligned offset inside the ROM area, the ROM (rounded up to whole sectors) ends inside the ROM area, and no two ROMs share a sector.

### Making a library

```sh
python tools/pack_library.py pokemon_red.gb homebrew.gb -o library.uf2
```

Hold X while powering on, then copy `library.uf2` onto the RPI-RP2 drive. The UF2 covers only the index sector and the ROM area, whole sectors, in ascending order. **Do not share a UF2 that contains ROMs** (CLAUDE.md). [VERIFY on hardware that it leaves the firmware and the saves alone.]

## What is not decided or verified yet

- Whether the UF2 trick (the bootloader only writes the blocks present in the UF2) really leaves the firmware and saves alone on this device. [VERIFY on hardware with a throwaway file before trusting it with a real save.] (PLAN.md backlog item 13.)
- What the stock PicoSystem firmware keeps inside the first 12 MiB, if anything. Our assumption is that it uses nothing below `0xC00000` apart from the program being run.
- The SDK linker script's `FAT` line reads `ORIGIN = 0x10003000, LENGTH = 4096K`, which overlaps `FLASH`; this looks like an upstream typo and does not matter while we keep the 12 MiB limit.
