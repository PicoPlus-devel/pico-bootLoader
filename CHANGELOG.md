# CHANGELOG

A resident .uf2 bootloader / front-end for the RP2350 retro-emulator family (pico-infonesPlus, pico-pcePlus, pico-genesisPlus, pico-smsplus, pico-peanutGB, …).

## General Info

[Binaries for each configuration and PCB design are at the end of this page](#downloads___).

## v0.9

USB controllers on the Adafruit Fruit Jam and the other boards with PIO-USB
are now found at every start, in the menu and in the applications. On boards
with HSTX video the applications no longer show small dots or lines in the
picture. *Diablo* joins the Ports category, *Salamander* the Arcade category,
the *Commodore 64 and 128* the Computer category, and **updateAll** now also
installs the BIOS files the emulators need.

> **Do you need to update?** Yes. On boards with PIO-USB, re-flash the board.
> On every board, replace the `/emu` and `/updateAll` folders on the card with
> the ones in the new `pico-bootLoader_sdcard.zip`, and run **updateAll** to
> install *Diablo* and the BIOS files.

### Changes

- **USB controllers on the Adafruit Fruit Jam and other boards with PIO-USB are
  now found at every start.** On boards with an older RP2350 chip they were
  often not detected, in the menu and in the applications.
- **No more dots or lines in the picture.** On boards with HSTX video, some TVs
  and monitors showed small dots or short dotted lines in some applications, for
  example in the Genesis and Super Nintendo emulators, or with Overclock on. The
  picture is now clean, and the built-in USB port can still be used for
  controllers and USB drive mode. The **Video Clock Fix** setting of the
  emulators is no longer needed and has been removed. Thanks to @szuping for the
  hint that led to this fix.
- **Diablo** joins the Ports category, as an early test version. It runs on the
  Adafruit Fruit Jam and on a Pimoroni Pico Plus 2 with the Adafruit DVI and
  microSD breakouts, and needs 8 MB of PSRAM and 16 MB of flash. updateAll
  installs the shareware version and the files the game needs in
  `/roms/devilutionx/`.
- **Salamander** joins the Arcade category: Konami's *Salamander* and
  *Lifeforce*. It runs on the boards with PSRAM and HSTX video, the same boards
  as *OutRun*. The games are not included: copy MAME's `salamand.zip` to
  `/roms/arcade/SALAMAND` and `lifefrce.zip` to `/roms/arcade/LIFEFRCE`, or let
  updateAll install them.
- **Commodore 64 and 128** join the Computer category. The Commodore 64 runs on
  every board; the Commodore 128 needs HSTX video and PSRAM. The emulator has so
  far been tried only on the Adafruit Fruit Jam and with the Adafruit DVI and
  microSD breakouts. Programs, disks, tapes and cartridges go in `/roms/C64`, and
  updateAll installs the ROM files it needs. See the
  [pico-c64 README](https://github.com/PicoPlus-devel/pico-c64#readme).
- **updateAll installs the BIOS files**, as MiSTer's Update All does: for the
  TI-99/4A, Commodore 64 and 128, Videopac, ColecoVision and PC Engine CD, and
  for Famicom Disk System games and Sega CD discs. The Windows program shows
  them as one entry, **BIOS**, that can be unticked. As with the ROM sets,
  holding the rights to them is your responsibility.
- **The updateAll Windows program is easier to follow.** It lists each file with
  its own progress bar, and the bar at the bottom now shows the whole run. A
  **Select all** box ticks or unticks all sets, and the full report is still
  there under **Show log**.

## v0.8

**updateAll** now also installs the free shareware and demo data of *Doom*,
*Duke Nukem 3D* and *Quake II*, so these games no longer need files from
elsewhere.

> **Do you need to update?** Yes. Re-flash the board, replace the `/emu` and
> `updateAll` folders on the card with the ones in the new
> `pico-bootLoader_sdcard.zip`, and run **updateAll**: *Doom!* needs it, as the
> archive no longer contains Doom's game data.

### Changes

- **Quake II** joins the Ports category. It runs on the Adafruit Fruit Jam and
  on a Pimoroni Pico Plus 2 with the Adafruit DVI and microSD breakouts, and
  needs 8 MB of PSRAM and 16 MB of flash.
- The picture no longer drops out, and the board no longer locks up, while an
  application is being written to flash. This affected the boards that show a
  progress bar during flashing, since v0.7.
- *Doom!*'s game data now lives in `/roms/doom/` on the card, where updateAll
  puts it. It is the same file for every board.
- When that file is missing, the menu says so and names the folder it belongs
  in, instead of starting the game without it.
- updateAll installs the shareware version of *Duke Nukem 3D* in
  `/roms/duke3d/` and the *Quake II* demo in `/roms/quake2/`.
- **PicoNES PCB with SNES controller ports.** PCB design v2.8, designed by
  John Edgar Park ([@johnedgarpark](https://twitter.com/johnedgarpark)), has
  two SNES controller ports, so original SNES controllers plug in without an
  adapter cable. The Gerber file `pico_nesPCB_v2.8_SNES_PORTS.zip` is attached
  to the release. See
  [Custom PCBs](https://github.com/PicoPlus-devel/pico-bootLoader#custom-pcbs).
- All PicoNES designs, with parts lists and pictures, are now described in the
  [PCB README](https://github.com/PicoPlus-devel/pico_shared/blob/main/PCB/README.md).

## v0.7.2

An SD-card update: **Galagino** now plays 47 arcade games instead of six, and
**1943** joins the Arcade category. **updateAll** installs the ROM sets of all
of them, and now fetches the latest list of sets from GitHub each time it
starts, so sets added later no longer need an SD-card update. The loader itself
is unchanged since v0.7.

> **Do you need to update?** Only the card. Replace the `/emu` folder on it with
> the one in the new `pico-bootLoader_sdcard.zip`, and copy its `updateAll`
> folder as well. The board does not have to be re-flashed, and your ROMs, save
> states and everything else outside `/emu` are untouched.

**Update:** the SD-card archive now has **1943**
[v0.1.1](https://github.com/PicoPlus-devel/pico-1943/releases/tag/v0.1.1). On
boards without PSRAM, such as a Pico 2 on the PicoNES PCB, 1943 stopped at
start-up with an "Out of memory" error when its games had been installed
unzipped, the way updateAll installs them. If you downloaded the archive
before, replace the `/emu` folder on your card again.

### New on the card

**1943** plays Capcom's *1943: The Battle of Midway* and *1943 Kai: Midway
Kaisen* on every board. When both games are on the card, a menu chooses one at
start-up. The games are not included: copy MAME's `1943.zip` to
`/roms/arcade/1943` and `1943kai.zip` to `/roms/arcade/1943KAI`, or let
**updateAll** install them.

### Updated applications

- **Galagino**
  [v0.2](https://github.com/PicoPlus-devel/pico-galagino/releases/tag/v0.2):
  41 more games, from [GalaginoPlus](https://github.com/VirtualClaudioBoy/GalaginoPlus),
  among them *Ms. Pac-Man*, *Donkey Kong Jr.*, *Galaxian*, *Space Invaders*,
  *Mr. Do!*, *BurgerTime*, *Time Pilot*, *Mappy* and *Xevious*. Each game reads
  its MAME zip from its own folder below `/roms/arcade`; the
  [pico-galagino README](https://github.com/PicoPlus-devel/pico-galagino#game-data)
  lists them, and **updateAll** installs them. High scores are now saved on the
  card. On boards without PSRAM, only the larger games are copied into flash,
  the first time they are started. Left alone, the game menu shows one game
  after another.
- **Phoenix**
  [v0.2.2](https://github.com/PicoPlus-devel/pico-phoenix/releases/tag/v0.2.2),
  **Moon Cresta**
  [v0.2.2](https://github.com/PicoPlus-devel/pico-mooncresta/releases/tag/v0.2.2)
  and **OutRun**
  [v0.5.1](https://github.com/PicoPlus-devel/pico-outrun/releases/tag/v0.5.1)
  play as before; their own release pages now also offer `updateAll.exe`.

Every other application is the same version as in v0.7.1. The exact version of
every application is in the table at the end of this page and in
`/emu/versions.txt`.

## v0.7.1

An SD-card update: **Dragon's Lair** joins the Arcade category, and Phoenix,
Moon Cresta and Galagino have new versions. The loader itself is unchanged
since v0.7.

> **Do you need to update?** Only the card. Replace the `/emu` folder on it with
> the one in the new `pico-bootLoader_sdcard.zip`, and copy its `updateAll` and
> `ld2pico` folders as well if you want to use them. The board does not have to
> be re-flashed, and your ROMs, save states and everything else outside `/emu`
> are untouched.

### New on the card

**Dragon's Lair**, Cinematronics' 1983 laserdisc game, runs on every board.
The game is not included: besides MAME's `dlair.zip`, it needs its laserdisc,
which you convert once on a computer from MAME's `dlair.chd` into the file
`lair.pld`. The Windows program that does this, `ld2pico.exe`, is in the
`ld2pico` folder of the SD-card archive. Both files go to `/roms/arcade/DLAIR`
on the card; see the
[ld2pico README](https://github.com/PicoPlus-devel/pico-bootLoader/blob/main/ld2pico/README.md).

**updateAll** now also installs the ROMs of Dragon's Lair, but not its
laserdisc.

### Updated applications

- **Phoenix**
  [v0.2.1](https://github.com/PicoPlus-devel/pico-phoenix/releases/tag/v0.2.1)
  and **Moon Cresta**
  [v0.2.1](https://github.com/PicoPlus-devel/pico-mooncresta/releases/tag/v0.2.1):
  on boards such as the Adafruit Fruit Jam, every line of the picture is now
  shown when Tate mode is off.
- **Galagino**
  [v0.1.2](https://github.com/PicoPlus-devel/pico-galagino/releases/tag/v0.1.2):
  *1942* runs at full speed and its sound no longer stutters. On boards such as
  the Adafruit Fruit Jam, the sound of the games no longer drops out every few
  seconds, and every line of the picture is shown when Tate mode is off.

Every other application is the same version as in v0.7. The exact version of
every application is in the table at the end of this page and in
`/emu/versions.txt`.

## v0.7

A release of both halves, the loader and the SD card. The loader shows an
overview of the board and supports one more, the **Olimex RP2040-PICO-PC**.
Every application on the card has a new version, and the card gains three arcade
entries, **Phoenix**, **Moon Cresta** and **Galagino** (six games), and
**updateAll**, a tool that installs the arcade ROMs.

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

**Galagino** brings six arcade games in one application: Pac-Man, Galaga,
Donkey Kong, Frogger, Dig Dug and 1942, chosen from a menu of their logos. It is
a port of Till Harbaum's [Galagino](https://github.com/harbaum/galagino) and
runs on every board. The game ROMs are not included: copy each game's MAME zip
to its own folder under `/roms/arcade` on the card, as it is or unzipped; see
the [pico-galagino README](https://github.com/PicoPlus-devel/pico-galagino#game-data).
On boards without PSRAM it copies the ROMs into flash the first time it starts,
which takes a few seconds, and again after another application has been written
over them. Like Phoenix, the games are vertical and have the
**Tate mode** setting.

**updateAll** puts the ROMs of the arcade games on the card: OutRun, Phoenix,
Moon Cresta and the six Galagino games. It is in the `updateAll` folder of the SD-card
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
~~**Video Clock Fix** setting, for TVs and monitors that show small dots or lines
in the picture~~ (removed in v0.9). Each version below links to its own release notes.

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
  the Olimex board, ~~the Video Clock Fix setting~~ and the fixes above.
- **Game Boy**
  [v0.15](https://github.com/PicoPlus-devel/pico-peanutGB/releases/tag/v0.15),
  **Videopac**
  [v0.6](https://github.com/PicoPlus-devel/pico-pacPlus/releases/tag/v0.6)
  and **TI-99/4A**
  [v0.3](https://github.com/PicoPlus-devel/pico-994A/releases/tag/v0.3):
  the Olimex board and the fixes above.
- **OutRun**
  [v0.4](https://github.com/PicoPlus-devel/pico-outrun/releases/tag/v0.4) and
  [v0.5](https://github.com/PicoPlus-devel/pico-outrun/releases/tag/v0.5):
  the Olimex board and the fixes above. Its settings are now kept in the same
  file as those of the other arcade games.
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
3. **Add your games.** The archive contains no games, ROMs or BIOS files. Each
   emulator opens in its own folder: `/roms/NES`, `/roms/SNES`, `/roms/MD`
   (Genesis), `/roms/PCE`, `/roms/GB`, `/roms/SMS` (Master System and Game
   Gear), `/roms/O2E` (Videopac) or `/roms/TI99`, or at the root of the card
   when that folder does not exist. The BIOS files and the folders of the other
   applications are listed in the README under
   [Bootable applications](https://github.com/PicoPlus-devel/pico-bootLoader#bootable-applications);
   `updateAll` installs the arcade ROM sets.
4. **Run it.** Insert the card and power on the board. The menu appears.


<a name="downloads___"></a>

## Metadata
Download the full metadata pack [here](https://1drv.ms/u/c/db8991463e5b8b0c/IQD1kFD0-j94QoCW3Tw447AaAVq2XdkmUH4T40Bcmtf9ZL0?e=8eNkSe) and extract it to the root of the SD card. This should create a `/metadata` folder on the card.


