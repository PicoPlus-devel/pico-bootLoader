# CHANGELOG

A resident .uf2 bootloader / front-end for the RP2350 retro-emulator family (pico-infonesPlus, pico-pcePlus, pico-genesisPlus, pico-smsplus, pico-peanutGB, …).

## General Info

[Binaries for each configuration and PCB design are at the end of this page](#downloads___).

## v0.4

A maintenance release. The loader picks up the current shared menu and support code. Nothing about how you use it changes, and the SD-card archive from v0.3 stays valid.

> **Do you need to update?** Only if your monitor accepts DVI but not HDMI and the loader menu stayed black. Otherwise the loader behaves exactly as it did in v0.3 and re-flashing is optional.

### Fixes

- **DVI-only monitors show a picture again.** In DVI mode, some older screens that accept DVI but not HDMI stayed black.
- **Steadier start-up.** The board lets its power settle before switching to the higher clock speed.

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

### Added

- **Duke Nukem 3D**, a native RP2350 port from
  [pico-duke3D](https://github.com/fhoedemakers/pico-duke3D), joins the menu as
  `duke3d_game`. It runs on the Adafruit Fruit Jam (HW_CONFIG 8), the Adafruit
  DVI + MicroSD breakout combination (2) and the Murmulator M2 (13), and needs
  PSRAM on all three. `DUKE3D.GRP` streams from `/roms/duke3d/` on the SD card,
  so there is no companion data image to flash.
- **Artwork themes.** The graphical menu can carry up to ten sets of artwork in
  `<BASEDIR>/assets/themes/0` … `themes/9`. Press **UP** or **DOWN** in the
  graphical menu to cycle through the themes present on the card; the choice is
  saved immediately and restored on the next boot. A theme need not be
  complete — any application it has no image for falls back to theme 0.
- **On-screen help.** Press **START** in either menu mode for a full-screen
  summary of the controls, the meaning of the `*` and `!` markers, and the
  current mode, theme, board configuration and index file.
- **Rejected-`.uf2` error screen.** When pre-flight validation refuses a file,
  the loader now says why in plain language instead of flashing a terse notice
  for three seconds. It names the actual problem — linked at the wrong address
  (the address is shown), built for the wrong chip or architecture, too large
  for the board's flash, corrupt, or an incomplete copy — and, where relevant,
  gives the build flags needed to produce an image the loader accepts. The page
  stays up until a button is pressed. Nothing is erased at that point, so the
  menu is still there afterwards.
- **`GUI` and `THEME` keys in `boot.txt`**, holding the menu mode and the active
  theme.

### Changed

- **The SD-card archive no longer leaves a menu entry without artwork.** It still
  ships the small `.444`/`.555` caches in preference to the source images, but
  where a theme has no cache for an entry its `.png`/`.jpg` now ships instead and
  the first boot converts it.
- `build_emulators.sh` builds *Doom* from pico-doom `main`, which carries the
  build scripts for both variants since the `full-version` merge.
- **Menu artwork moved** from `<BASEDIR>/assets` to
  `<BASEDIR>/assets/themes/0`. Existing cards are migrated automatically on the
  first boot — the cached `.444`/`.555` files move too, so nothing is
  re-converted, and the `screensaver/` folder is left where it is. The move is
  resumable if it is interrupted.
- **`boot.txt` is now written by the bootloader.** Only the `GUI=` and `THEME=`
  lines are rewritten; comments, ordering, spacing and every other key are
  preserved. The file is created on the first change if it does not exist,
  carrying the effective value of all keys. Updates go through a temporary file
  that is re-parsed before being renamed into place, so a power cut cannot
  corrupt the configuration. A card that cannot be written is not an error: the
  change applies for the session and the help screen says it was not saved.
- **The `.guimode` file is gone**, folded into `boot.txt`'s `GUI` key. An
  existing `.guimode` is read once, migrated, and deleted.
- Menu footer and text-mode hints updated for the new controls.
- Boards without PSRAM now convert artwork for *every* theme at boot rather than
  one folder, since the converter cannot run once the menu is up. The first boot
  after adding a theme is correspondingly slower.
- **`pico-bootLoader_sdcard.zip` is a release asset again.** The archive carries
  the emulators, the *Doom* port and its WAD, the artwork themes, the screensaver
  sprites, a sample `boot.txt` and a new `versions.txt` recording which release
  tag each emulator was built from. Unpack it at the root of the card.
- **Release tags now say what changed.** `v0.N` is a bootloader firmware release
  (re-flash the board and refresh the card); `v0.N.M` ships the same firmware with
  refreshed emulator binaries, so only `/emu` on the card needs replacing. Each
  release lists the emulator versions its archive contains.
- **Doom's WAD is named `doom1-whx.uf2`** in `emulators.txt`, matching what the
  build actually produces. Earlier cards declared `doom1-whx-for-fruitjam.uf2`,
  which the loader could never find, so it launched *Doom* without flashing the
  WAD.

### Fixed

- **A standalone build could be flashed and leave the board unbootable.** An
  application linked at `0x10000000` but larger than the 512 KB bootloader
  region straddles the partition boundary: validation skipped every block below
  `0x10080000` and accepted the tail, so the loader wrote a fragment from the
  middle of the image into the start of the application partition. The progress
  bar ran to 100 %, the flashed app then had no usable vector table, and the
  board rebooted into the menu with no explanation. Any block below the
  partition now rejects the whole file up front, before anything is erased, and
  the new error screen names the address it was linked at. Builds smaller than
  512 KB were already rejected, which is why this only showed up with larger
  applications.

## Getting started

For board-by-board wiring, supported display modes and more refer to the [pico-infonesPlus documentation](https://github.com/fhoedemakers/pico-infonesPlus#setup). The set of supported boards and their pinouts is identical between the two projects.

1. **Flash the bootloader.** Download the loader `.uf2` for your board. Hold
   BOOTSEL, connect the board over USB, and copy the `.uf2` onto the
   `RP2350` drive. See [Supported](https://github.com/fhoedemakers/pico-bootLoaders#supported-hardware) Hardware in the README.
2. **Prepare the SD card.** Download `pico-bootLoader_sdcard.zip` from the same
   Releases page and unpack it onto a FAT32- or exFAT-formatted card. The
   archive contains the emulators, the *Doom* port, the menu artwork, and a
   sample configuration file.
3. **Run it.** Insert the card and power on the board. The menu appears.


<a name="downloads___"></a>

## Metadata
Download the full metadata pack [here](https://1drv.ms/u/c/db8991463e5b8b0c/IQD1kFD0-j94QoCW3Tw447AaAVq2XdkmUH4T40Bcmtf9ZL0?e=8eNkSe) and extract it to the root of the SD card. This should create a `/metadata` folder on the card.


