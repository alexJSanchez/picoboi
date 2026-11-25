# Using PicoBoy — no programming needed

This guide gets you from "I have a PicoSystem" to "I'm playing my Game Boy games on it". You do not need to build anything: you download one file, copy it onto the device, and then copy a second file that holds your games.

PicoBoy plays **original Game Boy** games (the grey brick, 1989 to 1998). It does **not** play Game Boy Color (colour-only) or Game Boy Advance games.

## What you need

- A **Pimoroni PicoSystem**, charged.
- A **USB cable that carries data** (some cables only charge; if nothing shows up on your computer in step 2, try another cable).
- A **computer** (Windows, Mac or Linux).
- **Python 3**, free from [python.org](https://www.python.org/downloads/). On Windows, tick **"Add python.exe to PATH"** in the installer. (You only need it to prepare your games, in step 4.)
- Your own Game Boy game files (`.gb`), from cartridges you own. PicoBoy does not include any games and you should not download games you do not own.

## Step 1 — Download

1. On this page, open **Releases** (right-hand side) and download **`picoboy.uf2`**. This is the program for the PicoSystem. It contains no games.
2. Click the green **Code** button → **Download ZIP**, and unzip it somewhere. You need it for the tool in step 4 (it is the folder with `tools/`, `storage/` and `docs/` in it).

## Step 2 — Put PicoBoy on the PicoSystem

1. Plug the PicoSystem into the computer with the USB cable and switch it **off**.
2. **Hold the X button** and switch it **on**. A drive called **`RPI-RP2`** appears on your computer (this is the PicoSystem's built-in installer mode).
3. Drag **`picoboy.uf2`** onto that drive. The drive disappears by itself and the PicoSystem restarts.

You should now see the PicoBoy game list saying there are no games yet. That is correct.

## Step 3 — Collect your games

Make a folder called `roms` inside the folder you unzipped in step 1, and put your `.gb` files in it.

## Step 4 — Make your game library

Open a terminal in the unzipped folder (on Windows: open the folder, click the address bar, type `cmd` and press Enter) and run:

```
python tools/pack_library.py roms -o library.uf2
```

(On Windows, if `python` is not found, try `py` instead.)

It prints a table of your games with their sizes and tells you about any it cannot run (for example "Game Boy Color only"). It creates **`library.uf2`**, a file that holds all your games. About 9 MB of games fit, up to 85 titles.

## Step 5 — Put your games on the PicoSystem

Exactly like step 2: switch off, **hold X** while switching on, drag **`library.uf2`** onto the `RPI-RP2` drive. The PicoSystem restarts and shows your games.

## Step 6 — Play

| Button | Does |
|---|---|
| D-pad | D-pad (and moves up/down in the list) |
| A / B | A / B (and **A starts** a game in the list) |
| X | Select |
| Y | Start |
| Hold **X + Y** for 1 second | back to the game list |
| Hold **X + Y + B** for 1 second | mute / unmute the speaker |

### Saving your game

Use the game's **own save command** (for example the SAVE entry in the Pokémon START menu). About two seconds after you save, a green **SAVED** appears above the picture and the picture freezes for about half a second while it is written. After that you can switch the PicoSystem off. Next time, the game's CONTINUE (or equivalent) is there, and the game list shows a green **saved** next to it. Do not switch off during that short pause.

**The first time**, PicoBoy may ask **"SAVE AREA IN USE"**. That means a part of the PicoSystem's memory that PicoBoy wants for saves already holds something else (for example data from another program you ran on it).
- **A** erases that part and enables saving. Only that one area is erased; your games and PicoBoy are not touched.
- **B** lets you play without saving this time (it will ask again next time).

If your PicoSystem has never been used for anything else, you will not see this question.

### Deleting a game's save

In the game list, highlight a game that shows a green **saved** and **hold X and B together for two seconds** (nothing else pressed; the bottom line shows how far you are). A question appears: **A** deletes that game's save for good, **B** keeps it. Other games' saves are not touched. After deleting, the game starts fresh the next time.

Letting go before two seconds cancels, and starting a game with A is blocked while X or B is held, so it cannot happen by accident.

### Adding or removing games later

Make a new `library.uf2` with the games you want (step 4) and copy it as in step 5. It replaces the whole list. **Your saves are kept**: a save belongs to the game's file contents, so the same game finds its save again.

### Updating PicoBoy

Download a newer `picoboy.uf2` and copy it as in step 2. Your games and saves stay.

## If something goes wrong

| What you see | What to do |
|---|---|
| No `RPI-RP2` drive appears | You must hold **X before** switching on, and keep holding until the drive appears. Try another USB port or a different cable (some are charge-only). |
| "No games on this device yet" | You have not copied `library.uf2` yet (step 5), or the last thing you copied was a firmware file. Copying `picoboy.uf2` never removes games; if they are gone, copy `library.uf2` again. |
| `python` is not recognised | Install Python (see "What you need") and reopen the terminal; on Windows try `py` instead of `python`. |
| `error: ... no .gb files in this folder` | The `roms` folder is empty or the files are not named `.gb`. |
| A game is greyed out in the list | The line under the list says why (Game Boy Color only, cartridge type not supported, damaged file). Those cannot run. |
| "Emulator could not start this game" | Tell us which game: see "Reporting" below. |
| The picture looks wrong in a game | Some games use tricks this emulator cannot draw. See [COMPAT.md](COMPAT.md) for what has been tried. |
| No sound | The speaker is a small piezo: it is quiet and thin. You may have muted it with X + Y + B. |
| The game forgets its save | Wait for the green **SAVED** before switching off. If you see a red **SAVE FAILED**, or the top of the list says **SAVES OFF**, please report it. |
| Everything seems stuck | Switch the PicoSystem off and on. You cannot break anything with the buttons. |

To get back to what you had before PicoBoy, copy any other PicoSystem `.uf2` onto the `RPI-RP2` drive in the same way.

## Reporting

Please open an issue (the **Issues** tab) and say: which game (name and where it came from is not needed; the name shown in the list is enough), what you did, and what you saw. For compatibility reports there is a template that asks the right questions. **Never attach game files.**

## About game files

PicoBoy is an emulator, like a Game Boy in software. It contains no games. Use only game files that you have the right to use, such as dumps of cartridges you own, and free homebrew games (for example from [gbdev.io](https://gbdev.io/)).
