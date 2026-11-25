# Contributing

Thanks for looking. Anything that helps people play Game Boy games on the PicoSystem is welcome: compatibility reports, bug reports, fixes, documentation.

## The one hard rule

**Never add game files to this repository, in any form**: no `.gb`/`.gbc` ROMs, no saves (`.sav`), no UF2 files that contain games (`library.uf2` or any firmware built with `-DPB_EMBED_ROM`), no Nintendo boot ROM. (The two existing screenshots of commercial games are the owner's illustration of the emulator; please do not add more without asking.) The `.gitignore` blocks the obvious ones; please do not work around it. Test with free homebrew or the test ROMs the scripts download.

## Reporting

Use the issue templates. For a game that misbehaves, the most useful information is what the game was doing, what you saw, and (if you can build) the output of `tests/play_rom.c` for it (see [docs/COMPAT.md](docs/COMPAT.md)).

## Changing code

- Build and test: [docs/BUILDING.md](docs/BUILDING.md). `ctest` must pass.
- The layering is deliberate: `core/` and `storage/` are plain C with no hardware headers and have PC tests; only `platform/` touches the Pico/PicoSystem SDK. Please keep it that way; see the README's "How it works".
- Logic that can lose data or miscount time needs a test that fails when the logic is wrong (the existing tests were also checked by breaking the code on purpose).
- Flash addresses live only in `storage/flash_layout.h`. Anything that writes flash goes through the guard functions in that header.
- Measure before optimising, and write down what you measured: [docs/DECISIONS.md](docs/DECISIONS.md) is the log of findings and decisions, including the ones that turned out wrong.
- Small, focused commits.

## How this project is made

The code was written by the repository owner together with Claude (Anthropic's AI assistant, through Claude Code), and was tested on a real PicoSystem. The notes in [CLAUDE.md](CLAUDE.md) are the working rules used in that collaboration; they are useful for any contributor.

## Licence

By contributing you agree that your contribution is released under the project's [MIT licence](LICENSE).
