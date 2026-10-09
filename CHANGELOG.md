# CHANGELOG

A resident .uf2 bootloader / front-end for the RP2350 retro-emulator family (pico-infonesPlus, pico-pcePlus, pico-genesisPlus, pico-smsplus, pico-peanutGB, …).

## General Info

[Binaries for each configuration and PCB design are at the end of this page](#downloads___).

## v0.7

A release of both halves, the loader and the SD card. The loader shows an
overview of the board and supports one more, the **Olimex RP2040-PICO-PC**.
Every application on the card has a new version, and the card gains two arcade
games, **Phoenix** and **Moon Cresta**, and **updateAll**, a tool that installs
the arcade ROMs.

> **Do you need to update?** Yes, both parts. Re-flash the board with the
> loader `.uf2` for your hardware, and replace the `/emu` folder on your card
> with the one in the new `pico-bootLoader_sdcard.zip`; copy its `updateAll`
> folder as well if you want to use it. Your ROMs, save states and everything
> else outside `/emu` are untouched. The emulators return their settings to the
> defaults once, the first time they start after the update.

### The loader

**System information.** The options menu (SELECT) now shows the board's
hardware configuration, and how much flash, SRAM, PSRAM and SD-card space is
used and free.

**Clearer progress while flashing.** When an application is written to flash,
the screen says whether it is erasing or writing, and how far along it is.

**Faster start-up and flashing.** The menu appears sooner: the loader no longer
reads the whole application file at every start to check that it still matches
what is in flash. For this it keeps a small file, `.flashed`, in the board's
folder on the card, which can be deleted at any time. Writing an application to
flash takes less time, and on boards without PSRAM a game started from an
emulator's menu starts slightly sooner.

**SNES controllers on the controller port.** A SNES controller on the board's
own controller port, such as the built-in pad of the PicoSNES, now starts an
application with A and goes back with B, as a USB SNES controller does. Before,
B started an application and Y went back.

### New board and PCB

The **Olimex RP2040-PICO-PC** with a Raspberry Pi Pico 2 in place of the original
Pico (HW_CONFIG 15). It has its own loader, `pico-bootLoader_OlimexPicoPC_arm.uf2`,
and its own folder on the card, `/emu/15/`, with the emulators, *Doom* and
*Duke Nukem 3D*. Sound plays on HDMI and on the board's audio jack. A USB
controller goes in the USB-A port, a NES or SNES controller in the UEXT
connector. The Super Nintendo emulator, PC Engine CD games, *OutRun*,
*Duke Nukem 3D* and the full version of *Doom* need a PSRAM chip fitted to the
Pico 2 (GPIO 8). Thanks to [DnCraptor](https://github.com/DnCraptor), who
contributed the support for this board.

The **PicoSNES**, Gavin Knight's PCB that builds the complete console into a
SNES controller. It runs the Murmulator M2 loader,
`pico-bootLoader_MurmulatorM2_arm.uf2`, and reads its applications from
`/emu/13/`. The README's
[PicoSNES section](https://github.com/PicoPlus-devel/pico-bootLoader#picosnes-pcb-hw_config-13)
links to the build guide and the PCB files. Thanks Gavin!

### New on the card

**Phoenix**, Amstar's 1980 arcade shooter, runs on every board. The game ROMs
are copyright Amstar and not included: copy MAME's `phoenix.zip` to
`/roms/arcade/PHOENIX` on the card, as it is or unzipped. The game tells you on
screen if any files are missing. Phoenix was made for a monitor standing on its
side; on a normal screen the picture is turned upright, and the new **Tate
mode** setting shows it unrotated for a monitor turned on its side.

**Moon Cresta**, Nichibutsu's 1980 arcade shooter, runs on every board as well.
The game ROMs are copyright Nichibutsu and not included: copy MAME's
`mooncrst.zip` to `/roms/arcade/MOONCRESTA` on the card, as it is or unzipped.
Like Phoenix, it is a vertical game and has the same **Tate mode** setting.

**updateAll** puts the ROMs of the three arcade games, OutRun, Phoenix and
Moon Cresta, on the card. It is in the `updateAll` folder of the SD-card
archive: `updateAll.exe` for Windows, and the same tool as a Python and a
PowerShell script for other systems. Give it the MAME zips you have and it puts
each game's files in the folder the game reads. Zips you do not have are
downloaded from a third-party ROM database that is set up by default; untick
**Download missing zips** to use only your own. The ROMs are copyright of their
makers and are not included, and you are responsible for holding the rights to
them. See the
[updateAll README](https://github.com/PicoPlus-devel/pico-bootLoader/blob/main/updateAll/README.md).

### Updated applications

Every application that was already on the card has a new version. Most of them
now run on the Olimex board, close the controller test by holding SELECT + UP for 2 seconds
(SELECT + START clashed with some 8BitDo controllers), and make B work
straight away on the AliExpress SNES-style USB controller. The NES, Super
Nintendo, Genesis, PC Engine and Master System emulators also have a new
**Video Clock Fix** setting, for TVs and monitors that show small dots or lines
in the picture. Each version below links to its own release notes.

- **Nintendo Entertainment System**
  [v0.53](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/tag/v0.53)
  and [v0.54](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/tag/v0.54):
  more games start, among them *Super Mario Bros. + Tetris + Nintendo World Cup*
  and the Datach games. Nine colour palettes to choose from, and a
  **Button Layout** setting for controllers with four face buttons.
- **Super Nintendo**
  [v0.7](https://github.com/PicoPlus-devel/pico-snesPlus/releases/tag/v0.7)
  and [v0.8](https://github.com/PicoPlus-devel/pico-snesPlus/releases/tag/v0.8):
  *Far East of Eden Zero*, *Street Fighter Alpha 2* and *Star Ocean* now play,
  and scrolling games no longer tear. A game too large for PSRAM now starts
  straight after it has been copied to flash, instead of returning to this
  menu.
- **Genesis / Mega Drive**
  [v0.17](https://github.com/PicoPlus-devel/pico-genesisPlus/releases/tag/v0.17),
  [v0.18](https://github.com/PicoPlus-devel/pico-genesisPlus/releases/tag/v0.18)
  and [v0.19](https://github.com/PicoPlus-devel/pico-genesisPlus/releases/tag/v0.19):
  MD+ games, games larger than 4 MB such as *Super Street Fighter II*, 6-button
  controllers, and some Sega CD games, although most of those are too slow to
  play. Controllers other than Genesis ones have a new button layout. On boards
  without PSRAM, a game too large for the flash can no longer overwrite the
  loader. As on the Super Nintendo, a game too large for PSRAM now starts
  straight after it has been written to flash, instead of returning to this
  menu.
- **PC Engine**
  [v0.8](https://github.com/PicoPlus-devel/pico-pcePlus/releases/tag/v0.8) and
  **Master System / Game Gear**
  [v0.32](https://github.com/PicoPlus-devel/pico-smsplus/releases/tag/v0.32):
  the Olimex board, the Video Clock Fix setting and the fixes above.
- **Game Boy**
  [v0.15](https://github.com/PicoPlus-devel/pico-peanutGB/releases/tag/v0.15),
  **Videopac**
  [v0.6](https://github.com/PicoPlus-devel/pico-pacPlus/releases/tag/v0.6),
  **TI-99/4A**
  [v0.3](https://github.com/PicoPlus-devel/pico-994A/releases/tag/v0.3) and
  **OutRun**
  [v0.4](https://github.com/PicoPlus-devel/pico-outrun/releases/tag/v0.4):
  the Olimex board and the fixes above.
- **Doom**
  [v0.3](https://github.com/PicoPlus-devel/pico-doom/releases/tag/v0.3) and
  **Duke Nukem 3D**
  [v0.4](https://github.com/PicoPlus-devel/pico-duke3D/releases/tag/v0.4): the
  Olimex board.
- **ColecoVision**
  [1.6](https://github.com/cogliano/Adafruit_ColecoJam/releases/tag/1.6) and
  [1.7](https://github.com/cogliano/Adafruit_ColecoJam/releases/tag/1.7): a
  start-up screen with sound, and screensaver fixes.

The exact version of every application is in the table at the end of this page
and in `/emu/versions.txt`.

## v0.6.3

A new version of the Master System / Game Gear emulator, which also appears in
the **Handheld** category as **Game Gear**. See the
[v0.6.3 release notes](https://github.com/PicoPlus-devel/pico-bootLoader/releases/tag/v0.6.3).

## v0.6.2

New versions of ten applications, most of them with a setting for TVs that cut
off the edges of the screen. See the
[v0.6.2 release notes](https://github.com/PicoPlus-devel/pico-bootLoader/releases/tag/v0.6.2).

## v0.6.1

A new version of the NES emulator, with a setting to switch off the
8-sprites-per-line limit, and a fix for games that failed to start a second time
on boards without PSRAM. See the
[v0.6.1 release notes](https://github.com/PicoPlus-devel/pico-bootLoader/releases/tag/v0.6.1).

## v0.6

A release of both halves: the menu gained categories, an options menu and USB
drive mode, and the card gained the TI-99/4A, OutRun and the ColecoVision. See
the [v0.6 release notes](https://github.com/PicoPlus-devel/pico-bootLoader/releases/tag/v0.6).

## v0.4

Sega Genesis at full speed on HSTX boards, a recently played list in every
emulator, and DVI-only monitors working again. See the
[v0.4 release notes](https://github.com/PicoPlus-devel/pico-bootLoader/releases/tag/v0.4).

## v0.3

Documentation and Gerber files for the three custom PCBs; the loader itself did
not change. See the
[v0.3 release notes](https://github.com/PicoPlus-devel/pico-bootLoader/releases/tag/v0.3).

## v0.2

The first release with the SD-card archive, artwork themes, on-screen help and *Duke Nukem 3D*.
See the [v0.2 release notes](https://github.com/PicoPlus-devel/pico-bootLoader/releases/tag/v0.2)
for the full list.

## Getting started

For board-by-board wiring, supported display modes and more refer to the [pico-infonesPlus documentation](https://github.com/PicoPlus-devel/pico-infonesPlus#setup). The set of supported boards and their pinouts is identical between the two projects.

1. **Flash the bootloader.** Download the loader `.uf2` for your board. Hold
   BOOTSEL, connect the board over USB, and copy the `.uf2` onto the
   `RP2350` drive. See [Supported hardware](https://github.com/PicoPlus-devel/pico-bootLoader#supported-hardware) in the README.
2. **Prepare the SD card.** Download `pico-bootLoader_sdcard.zip` from the same
   Releases page and unpack it onto a FAT32- or exFAT-formatted card. The
   archive contains the emulators, the native ports, the menu artwork, a
   sample configuration file and the `updateAll` tool for the arcade ROMs.
3. **Run it.** Insert the card and power on the board. The menu appears.


<a name="downloads___"></a>

## Metadata
Download the full metadata pack [here](https://1drv.ms/u/c/db8991463e5b8b0c/IQD1kFD0-j94QoCW3Tw447AaAVq2XdkmUH4T40Bcmtf9ZL0?e=8eNkSe) and extract it to the root of the SD card. This should create a `/metadata` folder on the card.


