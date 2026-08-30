# CHANGELOG

A resident .uf2 bootloader / front-end for the RP2350 retro-emulator family (pico-infonesPlus, pico-pcePlus, pico-genesisPlus, pico-smsplus, pico-peanutGB, …).

## General Info

[Binaries for each configuration and PCB design are at the end of this page](#downloads___).

## v0.5

A loader-only release. The SD card is unchanged, so the
`pico-bootLoader_sdcard.zip` from v0.4 stays valid and does not need to be
downloaded again — only re-flash the board with the loader `.uf2` for your
hardware.

### Changes

**SELECT now opens an options menu** instead of switching the menu mode
directly. Move through it with UP / DOWN, confirm with A and return with B. It
holds four entries:

- **Help** — the same help screen, still also reachable with START.
- **Menu mode** — switch between the text list and the artwork view, as SELECT
  used to do on its own.
- **Enter BOOTSEL mode** — restart the board into the RP2350 ROM bootloader,
  where it appears on your computer as a drive named `RP2350`. This is how you
  update the bootloader itself without unplugging the board and holding the
  BOOTSEL button. Reset the board to return to the menu.
- **USB drive mode** — show the SD card on your computer over USB, so you can
  add applications or change artwork without taking the card out. Copy your
  files, then eject the drive on the computer and the menu comes back. If
  anything was written, the board restarts so that it reads the card afresh.

One note on USB drive mode. On some boards a USB game controller uses the very
port that USB drive mode needs for the cable to your computer: the Pimoroni Pico
DV Demo Base, the Adafruit DVI/microSD breakouts and the PicoNES PCB, the
Waveshare RP2350-Zero PCB and the Murmulator M2. Only one of the two can be
plugged in at a time, so on those boards the menu has to be operated with a
controller on a NES/SNES port or with a Wii Classic controller. Boards with a
separate controller port — the Adafruit Metro RP2350 and Fruit Jam, the
Waveshare RP2350-PiZero and RP2350-USB-A, and the Adafruit Feather RP2350 — are
not affected: game controllers go into that port, while the built-in port is the
one used for the cable to your computer.

**The separate Pico 2 W builds are gone.** The only thing they added was a
working on-board LED, which on that board is wired to the wireless chip and so
pulled the whole CYW43 driver in, filling the 512 KB bootloader partition to
within a few kilobytes. Pico 2 W boards are still supported: flash the ordinary
Pico 2 binary for your hardware. What you lose there is the LED, which no longer
blinks as a heartbeat or while an application is being flashed. What you gain is
USB drive mode, which never fit alongside the wireless driver.


## v0.4

A release of both halves: the loader **and** the SD card. The loader picks up the current shared
menu and support code, and every application on the card is rebuilt from its latest release. The
headline is Sega Genesis/Mega Drive, which now runs at full speed on HSTX boards.

> **Do you need to update?** Yes, both parts — download the new `pico-bootLoader_sdcard.zip`
> below and replace the `/emu` folder on your card, and re-flash the board with the loader `.uf2`
> for your hardware. The card is where nearly all of this release is: Genesis went from
> struggling to full speed, every emulator gained a **recently played** list, and DVI-only
> monitors that stayed black now show a picture. Your ROMs, save states and artwork are
> untouched — only `/emu` is replaced.

### The loader

- **DVI-only monitors show a picture again.** In DVI mode, some older screens that accept DVI but not HDMI stayed black.
- **Steadier start-up.** The board lets its power settle before switching to the higher clock speed.

### What's new on the card

All ten applications are rebuilt from their newest releases — the exact versions are in the table
at the end of this page and in `/emu/versions.txt` on the card. Every emulator picks up the same
shared fixes the loader did, and two things the loader has no use for:

- **A recently played list.** The last 20 games you started, newest first, kept separately for
  each emulator. Open it with **X** in the ROM browser — that is button 3 on any pad: X on a SNES
  controller, Y on XInput, Triangle on PlayStation, C on Genesis — or from the new **Recently
  played** entry at the top of the settings menu, which is the route for pads without a button 3.
  In the list **A** starts the game, **SELECT** removes it, **START** shows its artwork and **B**
  closes the list. Each list is plain text at the root of the card (`/recent_NES.txt`,
  `/recent_SNES.txt` and so on), so it survives a reboot and can be edited or deleted on a PC. A
  game that is no longer on the card is reported as missing rather than started. A damaged list
  simply comes up empty — unlike the settings file, nothing else is reset.
- **No more waiting for a re-flash you don't need.** On boards without PSRAM, starting a game
  always rewrote it into flash, even when that exact game was already there. The emulator now
  records what it wrote and skips programming when the selected game is exactly the image already
  in flash — verified with a checksum, not just a file name — which saves several seconds of black
  screen every time you restart the same title. That game is marked **[READY]** in the recently
  played list.

**Sega Genesis/Mega Drive is transformed.** pico-genesisPlus v0.14 rebuilds the emulator core:

- **Full speed on HSTX boards**, including in games with heavy sound — a Raspberry Pi Pico 2 or
  Pimoroni Pico Plus 2 (on the PicoNES PCB or on a breadboard), the Adafruit Fruit Jam, the
  Adafruit Metro RP2350 and the Murmulator M2. Sound is now produced on the second processor
  core, which leaves far more room for the game itself.
- **Sound works properly at last** — music, noise effects, the "SEGAAA!" voice and the audio of
  SGDK games, without dropouts in busy scenes.
- **Games can save your progress**, PAL (European) games run at the right speed, 256-wide games
  fill the screen, and starting one game after another is stable.
- Still slow on boards **without** HSTX — the Pimoroni Pico DV Demo Base, Waveshare RP2350-Zero /
  PicoNES Mini, Waveshare RP2350-USB-A / PicoNES Micro, the Spotpear HDMI board and the
  Murmulator M1. Putting the picture on screen takes so much of the board's attention that too
  little is left for the emulator. The games are playable but the action, music and sound all
  drag, and this is not something that can be tuned away.

The rest:

- **Nintendo Entertainment System** (v0.47) gains **beta support for the NES Zapper light gun** in
  controller port 2. It needs the custom PicoNES PCB (design v2.1 or later) and so is only in the
  `piconesPlus` build for the Adafruit DVI + MicroSD breakout / PicoNES PCB — on every other board
  those two pins are already in use, and the Murmulator M1 and M2 leave them unconnected
  entirely. There is nothing to switch on; the gun is detected when plugged in, and a normal pad
  in port 2 keeps working alongside it. Two things to know before buying one: games need the
  LCD-lag correction patches from [neslcdmod.com](https://neslcdmod.com/), and an **original
  Nintendo Zapper does not work on a flat panel** — it is built around the bright flash of a CRT.
  A third-party gun made for modern displays is required; the Tomee Zapp Gun is what this was
  developed and tested with. See
  [NES Zapper](https://github.com/fhoedemakers/pico-infonesPlus#nes-zapper-light-gun) for patching
  and calibration. Also in this release: a `.nes` file claiming **mapper 31** correctly reports
  "unsupported" again instead of booting into the NSF player, and the framerate overlay now shows
  a resync counter (`R<n>`) — a steadily rising number means the display is struggling to hold
  sync.
- **Doom!** (v0.2) and **Duke Nukem 3D** (v0.3) — a new **BOOTSEL Mode** item at the bottom of
  the Options menu restarts the board as the USB firmware drive, so you can re-flash it without
  unplugging it and holding the BOOT button.
- **Super Nintendo, PC Engine, Master System/Game Gear, Game Boy and Videopac** — the shared menu
  and controller work above; the emulator cores themselves are unchanged. On the SNES the 504 MHz
  overclock option is now documented as not advised, and the 378 MHz default is unchanged.
- **Controllers.** A SNES pad wired straight to a NES controller port now uses **A and B** as you
  would expect, in games as well as in the menu — previously the pad reported B and Y where a NES
  pad has A and B, so physical A did nothing at all and "choose" landed on B. In the menu **A**
  chooses, **B** goes back and **X** opens the recently played list, as on USB and Wii pads; on a
  Genesis pad **C** opens it. The Controller Test now names the buttons for the kind of pad it
  detects and shows the raw data the pad sends, which makes an adapter cable that quietly
  converts the signal easy to spot, and leaving that screen no longer drops into the screensaver.

## v0.3

A release about the hardware around the bootloader rather than the bootloader
itself. Nothing in the loader's behaviour changed since v0.2.

> **Do you need to update?** Nothing here changes what the board does. The
> loader `.uf2`s differ from v0.2 only in the version shown in the menu title
> and on the help screen, and the SD-card archive is a rebuild of the same
> emulators from their current release tags — they behave as before, they just
> report a version instead of an older one. Re-flashing and replacing `/emu`
> are both optional; the interesting part of this release is the documentation
> and the PCB gerbers below.

### Added

- **The three custom PCB designs are documented and shipped with the release.**
  The README has a [Custom PCBs](https://github.com/fhoedemakers/pico-bootLoader#custom-pcbs)
  chapter covering all of them — PicoNES (HW_CONFIG 2), PicoNES Mini (6) and
  PicoNES Micro (9) — with the parts each one needs, how the board is mounted,
  which loader binary to flash and the matching 3D-printed case. The Gerber
  archives are now release assets alongside the binaries: `pico_nesPCB_v2.6.zip`,
  `Gerber_PicoNES_Mini_PCB_v2.0.zip` and `Gerber_PicoNES_Micro_v1.2.zip`.
- **PicoNES PCB design v2.6 takes a Pimoroni Pico Plus 2.** The design gained
  through-holes, so instead of soldering the board flat you can fit male headers
  and plug in a Pico 2, Pico 2 W or a Pimoroni Pico Plus 2. On HW_CONFIG 2 the
  Pimoroni Pico Plus 2 is what unlocks the entries that need PSRAM — *Duke Nukem
  3D*, *PCEngine CD* and `doom_tiny_full` — and its 16 MB of flash gives the
  full 15.5 MB application partition instead of 3.5 MB. No separate binary is
  needed: the loader reads the flash size and detects PSRAM at boot, so the
  existing `pico2` image covers both. When the Pico is mounted on headers, print
  the **latest** top cover from Thingiverse — the older ones assume a board
  soldered flat and leave no room for the USB cable.
- **The README opens with the list of boards the loader runs on**, each entry
  linking to its binary in
  [Supported hardware](https://github.com/fhoedemakers/pico-bootLoader#supported-hardware)
  and, where one exists, to its PCB design.

### Changed

- **`duke3d_game` is built from pico-duke3D's first release tag, `v0.1`.**  The game itself is unchanged — relative to what  v0.2 shipped, the tag adds only a build fix for gcc 14 and a controller diagnostic that is compiled out by default.
- **`piconesPlus` is built from pico-infonesPlus `v0.45`.** That release carries
  the PCB v2.6 design and nothing else: its emulator binaries are the ones
  `v0.44` shipped, which is what was on the v0.2 card.
- **The SNES emulator is no longer shipped for a board that cannot run it.**
  pico-snesPlus needs 8 MB of PSRAM and supports only HW_CONFIG 2, 8, 13 and 14,
  but the v0.2 archive also carried a `picosnesPlus.uf2` for the Adafruit Metro
  RP2350 (HW_CONFIG 5). That build cannot work on the board, so `emu/5/` no
  longer contains one and the entry disappears from the picker there.

## v0.2

The first release with the SD-card archive, artwork themes, on-screen help and *Duke Nukem 3D*.
See the [v0.2 release notes](https://github.com/fhoedemakers/pico-bootLoader/releases/tag/v0.2)
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


