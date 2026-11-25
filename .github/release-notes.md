**PicoBoy: original Game Boy games on the Pimoroni PicoSystem.**

`picoboy.uf2` is the program for the PicoSystem. **It contains no games.** You add your own with a second file you make on your computer.

### Install in two minutes
1. Switch the PicoSystem off, **hold X**, switch it on, plug in the USB cable. A drive called `RPI-RP2` appears.
2. Drag `picoboy.uf2` onto that drive. It restarts into the game list.
3. To add games, follow the step-by-step guide: [docs/USER_GUIDE.md](https://github.com/alexJSanchez/picoboi/blob/main/docs/USER_GUIDE.md) (you need Python 3 and your own `.gb` game files).

### Files
- `picoboy.uf2`: the firmware.
- `SHA256SUMS.txt`: the checksum of the firmware, to check your download.
- `picoboy-licenses.zip`: the licence of PicoBoy and of the open-source libraries inside the firmware (MIT/BSD). Keep it with any copy you share.

PicoBoy plays original Game Boy (DMG) games, not Game Boy Color or Game Boy Advance. See [docs/COMPAT.md](https://github.com/alexJSanchez/picoboi/blob/main/docs/COMPAT.md) for what has been tested.

---
