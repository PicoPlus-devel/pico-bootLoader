# CHANGELOG

A resident .uf2 bootloader / front-end for the RP2350 retro-emulator family (pico-infonesPlus, pico-pcePlus, pico-genesisPlus, pico-smsplus, pico-peanutGB, …).

## General Info

[Binaries for each configuration and PCB design are at the end of this page](#downloads___).

## v0.6

A release of both halves, the loader and the SD card, and the first since v0.4:
there was no v0.5. The menu gains categories, an options menu and USB drive
mode; the card gains the TI-99/4A, OutRun and the ColecoVision, and newer
versions of five emulators.

> **Do you need to update?** Yes, both parts. Re-flash the board with the
> loader `.uf2` for your hardware, and replace the `/emu` folder on your card
> with the one in the new `pico-bootLoader_sdcard.zip`. Your ROMs, save states
> and everything else outside `/emu` are untouched.

### The loader

**Categories.** The menu can now group applications instead of showing them all
in one long list. It opens on a set of categories — Arcade, Computer, Console,
Handheld, Ports and Settings — and opening one shows what is in it.

- LEFT / RIGHT (UP / DOWN in the text menu) moves between categories.
- The first button (`A` on a NES pad) opens the highlighted category, and
  starts an application once you are inside one.
- The second button (`B`) goes back to the categories.
- **Settings** opens the options menu described below.

A category that has nothing in it still appears, and says so when you open it.
Categories come from a `categories.txt` file on the card, with one list file
per category, so you decide what goes where; delete it and the menu is the
single list it always was. The README explains how to build your own
arrangement.

**SELECT opens an options menu** instead of switching the menu mode directly.
Move through it with UP / DOWN, confirm with A and return with B:

- **Help** — the help screen, still also reachable with START.
- **Menu mode** — switch between the text list and the artwork view.
- **Enter BOOTSEL mode** — restart the board as the `RP2350` drive, so the
  loader itself can be updated without unplugging the board and holding the
  BOOTSEL button.
- **USB drive mode** — show the SD card on your computer over USB, so you can
  add applications, ROMs or artwork without taking the card out. Eject the
  drive on the computer when you are done and the menu comes back.

On some boards a USB game controller uses the same port as the cable to your
computer, so only one of the two can be plugged in at a time: the Pimoroni Pico
DV Demo Base, the Adafruit DVI + microSD breakouts and the PicoNES PCB, the
Waveshare RP2350-Zero PCB and the Murmulator M2. On those, operate the menu with
a controller on a NES/SNES port or a Wii Classic controller. Boards with a
separate controller port are not affected.

**The menu remembers where you were.** Whichever category and application you
were last on is where the menu comes back the next time you switch the board on.

**Applications appear in the order you list them**, instead of in the card's
own, effectively arbitrary, file order. A list may now hold 32 applications
instead of 16.

**PSRAM is recognised reliably at start-up.** The check could occasionally miss
the chip, and the board then behaved as if it had none.

**Pico 2 W boards use the ordinary Pico 2 binary.** The separate Pico 2 W
builds are gone: all they added was the on-board LED, whose driver left no room
for USB drive mode. On a Pico 2 W the LED no longer blinks.

### New on the card

**TI-99/4A** fills the new Computer category and runs on every board. Put your
cartridges in `/roms/TI99` on the card, as `.rpk` files or as the classic `.bin`
sets, and the console files `994aROM.bin` and `994aGROM.bin` in `/bios` — the
emulator does not run without those two. A USB keyboard works as the TI
keyboard, and TI BASIC is there without a cartridge. Without PSRAM the disk
drives are unavailable and cartridges are limited to 32 KB.

**OutRun**, a port of SEGA's arcade game, fills the new Arcade category. It runs
on the Adafruit Fruit Jam, the Adafruit DVI + microSD breakouts or the PicoNES
PCB, the Murmulator M2, and the Adafruit Feather RP2350 with a TLV320DAC3100,
and needs PSRAM on all four. The game ROMs are copyright SEGA and not included:
copy the unzipped MAME `outrun` (revision B) set to `/roms/ORUN`. The game takes
a few seconds at every start to prepare them, and tells you on screen if any are
missing.

**ColecoVision** joins the Console category on the Adafruit Fruit Jam. It is
[ColecoJam](https://github.com/cogliano/Adafruit_ColecoJam) by Dan Cogliano
([@cogliano](https://github.com/cogliano)) — a big thank you to Dan for this
emulator and for making it available here. Put the BIOS, `COLECO.BIN`, and your
games (`.ROM` files) in `/coleco` on the card. Neither is included.

### Updated emulators

All five below now have **USB drive mode** in their own settings menu, opened
with SELECT from the game list, so games can be copied onto the card without
taking it out. Each version links to that emulator's own release notes.

- **Nintendo Entertainment System**
  [v0.50](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/tag/v0.50):
  new mappers and fixes for dozens of games, among them *Mike Tyson's
  Punch-Out!!*, *Castlevania III* and *Romance of the Three Kingdoms II*. Saved
  games in battery-backed MMC5 cartridges are kept, and no more than 8 sprites
  per line are shown, as on a real NES.
- **Super Nintendo**
  [v0.5](https://github.com/PicoPlus-devel/pico-snesPlus/releases/tag/v0.5):
  *Super Mario RPG* no longer freezes during battles.
- **PC Engine**
  [v0.6](https://github.com/PicoPlus-devel/pico-pcePlus/releases/tag/v0.6):
  a save state of a CD game can now be loaded after leaving the game and
  starting it again.
- **Master System / Game Gear**
  [v0.29](https://github.com/PicoPlus-devel/pico-smsplus/releases/tag/v0.29):
  also plays **Sega SG-1000** games — `.sg` files, with a lowercase extension,
  next to your Master System and Game Gear ROMs.
- **Genesis / Mega Drive**
  [v0.15](https://github.com/PicoPlus-devel/pico-genesisPlus/releases/tag/v0.15):
  USB drive mode only; the emulator itself is unchanged.

Videopac, Game Boy, *Doom!* and *Duke Nukem 3D* are the same versions as
before. The exact version of every application on the card is in the table at
the end of this page and in `/emu/versions.txt`.

### Note

`/boot.txt` gains three new settings that record your position in the menu. An
older bootloader does not know them and will report `BOOT.TXT INVALID` if you
put the card back in a board running one; delete the `VIEW`, `CATEGORY` and
`APP` lines to use it there again.

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

For board-by-board wiring, supported display modes and more refer to the [pico-infonesPlus documentation](https://github.com/fhoedemakers/pico-infonesPlus#setup). The set of supported boards and their pinouts is identical between the two projects.

1. **Flash the bootloader.** Download the loader `.uf2` for your board. Hold
   BOOTSEL, connect the board over USB, and copy the `.uf2` onto the
   `RP2350` drive. See [Supported hardware](https://github.com/fhoedemakers/pico-bootLoader#supported-hardware) in the README.
2. **Prepare the SD card.** Download `pico-bootLoader_sdcard.zip` from the same
   Releases page and unpack it onto a FAT32- or exFAT-formatted card. The
   archive contains the emulators, the *Doom* port, the menu artwork, and a
   sample configuration file.
3. **Run it.** Insert the card and power on the board. The menu appears.


<a name="downloads___"></a>

## Metadata
Download the full metadata pack [here](https://1drv.ms/u/c/db8991463e5b8b0c/IQD1kFD0-j94QoCW3Tw447AaAVq2XdkmUH4T40Bcmtf9ZL0?e=8eNkSe) and extract it to the root of the SD card. This should create a `/metadata` folder on the card.


