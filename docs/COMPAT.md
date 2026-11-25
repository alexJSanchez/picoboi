# Compatibility

What was actually tried, how, and what is still unknown. This is a small, honest list, not a compatibility database. PicoBoy runs original Game Boy (DMG) cartridges; Game Boy Color-only games are listed but not run.

## How things were checked

Two kinds of check, kept apart on purpose:

- **On the PicoSystem**, by the author, playing: does it start, look right, sound right, run at full speed, keep a save.
- **On a PC**, by a script: the same emulator code as the firmware (Peanut-GB through `core/pb_core.c`) runs the ROM with buttons pressed from a script, and screenshots are saved at chosen frames (`tests/play_rom.c`, converted by `tools/frames_to_png.py`). This is how pictures, error counts, sound and cartridge-RAM use were examined. It checks the *emulator*, not the PicoSystem (no scaling, display or speaker involved), and a scripted play-through only goes as far as the script gets.

Reproduce a run (with your own ROM):

```sh
cmake -S tests -B build-host -G Ninja && cmake --build build-host
build-host/play_rom "roms/YourGame.gb" 3000 --press START@1500+4 --press A@1650+4%30~2800 --shot 2000:shot.bmp
python tools/frames_to_png.py shot.bmp shot.png --scale 3
```

The repository holds two screenshots of commercial games (`docs/images/pokemon-red.png`, `docs/images/links-awakening.png`), made this way, only to show the emulator working; the artwork belongs to its owners. No game *files* are in the repository. The other pictures come from free test programs.

## Summary

| Game | Cartridge | On the device | PC script check | Status |
|---|---|---|---|---|
| Pokémon Red (US) | MBC3 + battery, 1 MiB, 32 KiB RAM | plays, sound, full speed, **saves persist across power-off** | intro, title, new game, first room and the in-game SAVE draw correctly; no emulator errors | works as far as tested |
| Zelda: Link's Awakening (US, v1.2) | MBC1 + battery, 512 KiB, 8 KiB RAM | author saw nothing wrong (no details recorded) | storm intro with its wave effect, title, file select and name entry draw correctly; no emulator errors | boots and menus fine; **gameplay not yet checked** |
| dmg-acid2 (free test) | ROM only | not run on the device | picture is the expected face; no errors | passes |
| Blargg `cpu_instrs` (free test) | MBC1 | passed on the device (author) | all 11 sub-tests "ok", "Passed all tests" | passes |

## Pokémon Red

**Checked on the PC (scripted, about 9,800 frames from power-on):**

- Copyright screen, the intro battle, and the title screen (it alternates Charmander and Weedle) are drawn correctly.
- New game: Professor Oak's speech, naming, the player's bedroom, the START menu (POKéMON, ITEM, name, SAVE, OPTION, EXIT), the trainer card, the "Would you like to SAVE the game?" question and "Now saving..." all draw correctly.
- No emulator errors in any run. Sound is produced (about 75% of the frames of the boot are not silent, peak level reasonable).
- **Cartridge RAM, per 8 KiB bank:** during the intro and play only **bank 0** changes (the game's scratch space for sprites; hundreds of frames, tens of thousands of byte changes), and it never changes in banks 1 to 3. The in-game SAVE changes **bank 1** (121 bytes within 6 frames at the first save). This is why PicoBoy only counts changes in banks 1 and up as "the game saved" for this game; with a plain "any change" rule it would have written to flash during battles.

**Checked on the device (author):** the game plays at full speed with sound; the game's own save survives turning the PicoSystem off and on.

**Not checked:** a battle (beyond the intro battle), the overworld while walking, a Pokémon Center, big towns (heavy ROM bank switching), the PC and boxes (RAM banks 2 and 3), a long session (30+ minutes), the Hall of Fame. Pokémon Blue and Yellow are expected to behave like Red (same engine) but have not been tried; the bank-0 rule was assigned to them by analogy and is unverified.

## Zelda: Link's Awakening

**Checked on the PC (scripted, up to 7,600 frames):**

- The opening storm and ship, with the wave bands that scroll at different speeds line by line (the kind of mid-frame effect that can break a line-by-line renderer), draw correctly; the beach scene with Link and Marin draws correctly.
- Title screen, "PLAYER SELECT" and "YOUR NAME?" with the letter grid draw correctly (font, borders and cursor).
- No emulator errors. Sound present. Cartridge RAM: 15 byte changes in the first 17 frames (the game checking or initialising its save), none afterwards in these runs. There is no scratch bank, so the plain "any change" rule applies.

**Checked on the device (author):** reported nothing wrong; no further detail was recorded.

**Not checked:** actual play. The scripted run did not get past the name entry (pressing START there returns to the file list; that is how the game works, not an emulator problem, and the script needs another approach). So the overworld, a dungeon, the window and status bar, scrolling, sprite-heavy scenes, the pause menu and **the game's own save and its flash write were not exercised**.

## Free test programs

- **dmg-acid2** (Matt Currie, MIT): renders the expected face with the "HELLO WORLD!" title; see `docs/images/dmg-acid2.png`. Checked by eye against the reference image's description, not pixel-for-pixel.
- **Blargg's cpu_instrs**: all 11 sub-tests report "ok"; see `docs/images/cpu_instrs.png`. Also part of the automatic tests.

## Known limits that affect compatibility

- The emulator (Peanut-GB) draws one line at a time. Games that change picture registers in the middle of a line, or rely on exact timing inside a frame, can look wrong. Link cable, printer and Game Boy Color features are not supported.
- Cartridge types: ROM only, MBC1, MBC2, MBC3, MBC5 are accepted; others (for example the Game Boy Camera, MBC6/7, HuC) are listed in the game list and greyed out. A cartridge with 64 KiB of save RAM is refused (the emulator cannot size it).
- The emulated screen is drawn at 1.5× on the PicoSystem; the speaker is a piezo, so sound is thin.

## Wanted

Reports for other games. To add one: play it, and record what you tried, whether it looked and sounded right, whether the save survives a power-off, and anything odd, in the same style as above. The most useful extra check for any game is the per-bank RAM report of `play_rom` during play compared with its in-game save (some games, like Pokémon, use battery RAM as scratch space).
