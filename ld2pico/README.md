# ld2pico

`ld2pico` converts the laserdisc of *Dragon's Lair* into the file that pico-dragonslair plays from the SD card. All pictures and sound of the game are on its laserdisc. The game reads them from the file `lair.pld`, which is made once, on a computer, from MAME's laserdisc image `dlair.chd` (12.3 GB). The result is about 260 MB.

The game is copyright Cinematronics and Don Bluth, and neither `dlair.chd` nor MAME's ROM set `dlair.zip` is included. The user is responsible for holding the rights to them.

`ld2pico.exe` in this folder is the Windows program from the [pico-dragonslair](https://github.com/PicoPlus-devel/pico-dragonslair) release listed in `/emu/versions.txt`. It is a single file that needs no installation.

## Converting the laserdisc on Windows

1. Start `ld2pico.exe`.
2. Under **Laserdisc image**, choose `dlair.chd`. The file can also be dropped onto the window or onto the program's icon.
3. Under **Save lair.pld in**, choose where the file goes. When an SD card of this family is in the computer (a card with the folder `\roms\arcade`), its folder `\roms\arcade\DLAIR` is already offered.
4. Select **Convert**. The conversion runs on all processor cores at a lowered priority, so the computer stays usable; it takes a few minutes. **Cancel** stops it without leaving a partial file behind.

The program is not signed, so Windows may show "Windows protected your PC" the first time it is started. Select **More info**, then **Run anyway**.

## On the SD card

Both files go to the folder `/roms/arcade/DLAIR`:

- `lair.pld`, the converted laserdisc;
- MAME's `dlair.zip` (Dragon's Lair, US revision F2), as it is or unzipped. [updateAll](../updateAll/README.md) can install it.

Copy `lair.pld` with a card reader where possible. *USB drive mode* in the loader's options menu works too, but is slow for a file of this size. When a file is missing, the game names it on screen.

## Other systems

On Linux, or in WSL, build and run the command-line version of the converter, which also has options for picture quality and data rate. See [Game data](https://github.com/PicoPlus-devel/pico-dragonslair#game-data) in the pico-dragonslair README.
