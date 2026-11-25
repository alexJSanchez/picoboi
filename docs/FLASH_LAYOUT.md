# Flash layout

Source of truth: [`storage/flash_layout.h`](../storage/flash_layout.h). This page explains it; if the two ever disagree, the header wins (it has compile-time checks, and `tests/test_flash_layout.c` checks the write guards). Change both together (CLAUDE.md "definition of done").

The chip is 16 MiB. **We use only the first 12 MiB** (decision of 2026-11-25, see `docs/DECISIONS.md`): the PicoSystem SDK's own linker script (`memmap_picosystem.ld`) calls those 12 MiB "user flash" and reserves the top 4 MiB for a FAT filesystem. Staying out of it leaves whatever the stock firmware keeps there untouched.

Offsets are from the start of flash; the CPU sees flash at `0x10000000 + offset` (XIP).

| Region | Offset | Size | Written by | Purpose |
|---|---|---|---|---|
| Firmware | `0x000000` | 1 MiB | flashing the UF2 | Code and constants. The linker is capped here (not yet; planned in Phase 3) |
| Library index | `0x100000` | 4 KiB | `tools/pack_library.py` (UF2) | One sector: header and ROM entries |
| ROM area | `0x101000` | 0x8FF000 (about 9.0 MiB) | `tools/pack_library.py` (UF2) | ROMs back to back, each 4 KiB aligned, one contiguous range per ROM, read in place |
| Settings | `0xA00000` | 8 KiB | firmware | Two sectors (A/B copies) |
| Save slots | `0xA02000` | 0x1FE000 (about 2.0 MiB) | firmware | Battery saves, A/B copies per game (Phase 4) |
| Not ours | `0xC00000` | 4 MiB | the SDK | Reserved for its FAT filesystem. Never touched |

## Rules

- The firmware only erases or programs the **settings and save-slot** regions (`0xA00000` to `0xC00000`). Everything else is written from the PC. `pb_flash_erase_allowed()` and `pb_flash_program_allowed()` in the header encode this (inside the writable area, sector or page aligned, no arithmetic overflow); all flash writes must check them (CLAUDE.md hard rule).
- Erase works on 4 KiB sectors, programming on 256-byte pages.
- ROMs are read in place through XIP; each ROM must be one contiguous range (that is why the ROM area is a flat index and not a filesystem).
- The firmware region is 1 MiB. Until Phase 3's linker cap exists, a firmware larger than that would overwrite the index and ROM area; development builds that embed a ROM (about 1.3 MiB) are only for testing without the library.

## Capacity

- ROM area: 9,433,088 bytes, so for example eight 1 MiB games (Pokemon Red/Blue/Yellow) with room to spare.
- Save slots: 2,088,960 bytes. A Pokemon save is 32 KiB; with two copies (A/B, PLAN.md section 5) that is 64 KiB per game, so about 31 games.

## What is not decided or verified yet

- Whether the UF2 trick (the bootloader only writes the blocks present in the UF2) really leaves the firmware and saves alone on this device. [VERIFY on hardware with a throwaway file before trusting it with a real save.] (PLAN.md backlog item 13.)
- What the stock PicoSystem firmware keeps inside the first 12 MiB, if anything. Our assumption is that it uses nothing below `0xC00000` apart from the program being run.
- The SDK linker script's `FAT` line reads `ORIGIN = 0x10003000, LENGTH = 4096K`, which overlaps `FLASH`; this looks like an upstream typo and does not matter while we keep the 12 MiB limit.
