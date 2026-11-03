# updateAll

`updateAll` installs arcade ROM sets on the SD card of a pico-bootLoader console or of a standalone arcade application. It takes MAME ROM zips from a folder, downloads the zips that are not in it from the configured databases, and extracts each set into the folder on the SD card where its application looks for it. It also installs the freely distributed shareware and demo data of the ported games *Doom*, *Duke Nukem 3D*, *Quake II* and *Diablo*, and the assets of the DevilutionX engine that *Diablo* runs on, see [Game data](#game-data), and the BIOS files that the emulators need, see [BIOS files](#bios-files).

The ROM sets are copyright of their manufacturers and are not included. The shipped `updateAll.json` configures one download database, [ArcadeROMsDB_MiSTer](https://github.com/zakk4223/ArcadeROMsDB_MiSTer), a third-party database that is not part of this project, so zips that are not in the zip folder are downloaded from it by default. The user is responsible for holding the rights to the ROM sets, whether they are supplied as zips or downloaded. The same holds for the BIOS files, which are copyright of their manufacturers as well and are downloaded from the third-party sources listed under [BIOS files](#bios-files); to leave them out, untick **BIOS** in the Windows program, or name the sets to install with `--core` (Python) or `-Core` (PowerShell). To use only zips you supply, untick **Download missing files** in the Windows program, pass `--no-download` (Python) or `-NoDownload` (PowerShell), or set `"databases": []` in `updateAll.json` and remove its `config_url`; see [Download databases](#download-databases).

Three equivalent implementations are provided: `updateAll.exe`, a Windows program with a window, `updateAll.py` (Python) and `updateAll.ps1` (PowerShell). All three read the same configuration file, `updateAll.json`; the Windows program also carries a built-in copy of it. At every start, all three first read the latest version of that file from this repository, so that sets added after the SD-card archive was made are installed as well; see [Latest sets](#latest-sets).

## Supported sets

| ID | Application | MAME zip | SD card folder |
| --- | --- | --- | --- |
| `outrun` | pico-outrun | `outrun.zip` (revision B) | `/roms/ORUN` |
| `phoenix` | pico-phoenix, pico-galagino | `phoenix.zip` | `/roms/arcade/PHOENIX` |
| `mooncresta` | pico-mooncresta, pico-galagino | `mooncrst.zip` | `/roms/arcade/MOONCRESTA` |
| `pacman` | pico-galagino | `puckman.zip` (merged), or `pacman.zip` | `/roms/arcade/PACMAN` |
| `galaga` | pico-galagino | `galaga.zip` | `/roms/arcade/GALAGA` |
| `dkong` | pico-galagino | `dkong.zip` | `/roms/arcade/DONKEYKONG` |
| `frogger` | pico-galagino | `frogger.zip` | `/roms/arcade/FROGGER` |
| `digdug` | pico-galagino | `digdug.zip` | `/roms/arcade/DIGDUG` |
| `1942` | pico-galagino | `1942.zip` (revision B) | `/roms/arcade/1942` |
| `alibaba` | pico-galagino | `alibaba.zip` | `/roms/arcade/ALIBABA` |
| `amidar` | pico-galagino | `amidar.zip` | `/roms/arcade/AMIDAR` |
| `anteater` | pico-galagino | `anteater.zip` | `/roms/arcade/ANTEATER` |
| `bagman` | pico-galagino | `bagman.zip` (merged), or `bagmanm2.zip` | `/roms/arcade/BAGMAN` |
| `bombjack` | pico-galagino | `bombjack.zip` | `/roms/arcade/BOMBJACK` |
| `bnj` | pico-galagino | `brubber.zip` (merged), or `bnj.zip` | `/roms/arcade/BUMPNJUMP` |
| `btime` | pico-galagino | `btime.zip` | `/roms/arcade/BURGERTIME` |
| `circusc` | pico-galagino | `circusc.zip` | `/roms/arcade/CIRCUSCHARLIE` |
| `crush` | pico-galagino | `crush.zip` | `/roms/arcade/CRUSHROLLER` |
| `dkong3` | pico-galagino | `dkong3.zip` | `/roms/arcade/DONKEYKONG3` |
| `dkongjr` | pico-galagino | `dkongjr.zip` (merged), or `dkongjrj.zip` | `/roms/arcade/DONKEYKONGJR` |
| `eyes` | pico-galagino | `eyes.zip` | `/roms/arcade/EYES` |
| `fantasy` | pico-galagino | `fantasyu.zip`, or `fantasy.zip` (its former name) | `/roms/arcade/FANTASY` |
| `galaxian` | pico-galagino | `galaxian.zip` | `/roms/arcade/GALAXIAN` |
| `gaplus` | pico-galagino | `gaplus.zip` | `/roms/arcade/GAPLUS` |
| `gyruss` | pico-galagino | `gyruss.zip` | `/roms/arcade/GYRUSS` |
| `ladybug` | pico-galagino | `ladybug.zip` | `/roms/arcade/LADYBUG` |
| `lizwiz` | pico-galagino | `lizwiz.zip` | `/roms/arcade/LIZARDWIZARD` |
| `mappy` | pico-galagino | `mappy.zip` | `/roms/arcade/MAPPY` |
| `mrdo` | pico-galagino | `mrdo.zip` | `/roms/arcade/MRDO` |
| `mrtnt` | pico-galagino | `mrtnt.zip` | `/roms/arcade/MRTNT` |
| `mspacman` | pico-galagino | `mspacman.zip` | `/roms/arcade/MSPACMAN` |
| `nibbler` | pico-galagino | `nibbler.zip` | `/roms/arcade/NIBBLER` |
| `pengo` | pico-galagino | `pengo.zip` | `/roms/arcade/PENGO` |
| `pbaction` | pico-galagino | `pbaction.zip` | `/roms/arcade/PINBALLACTION` |
| `pooyan` | pico-galagino | `pooyan.zip` | `/roms/arcade/POOYAN` |
| `rocnrope` | pico-galagino | `rocnrope.zip` | `/roms/arcade/ROCNROPE` |
| `scramble` | pico-galagino | `scramble.zip` | `/roms/arcade/SCRAMBLE` |
| `invaders` | pico-galagino | `invaders.zip` | `/roms/arcade/SPACEINVADERS` |
| `starforc` | pico-galagino | `starforc.zip` | `/roms/arcade/STARFORCE` |
| `scobra` | pico-galagino | `scobra.zip` | `/roms/arcade/SUPERCOBRA` |
| `theglob` | pico-galagino | `suprglob.zip` (merged), or `theglobp.zip` | `/roms/arcade/THEGLOB` |
| `timeplt` | pico-galagino | `timeplt.zip` | `/roms/arcade/TIMEPILOT` |
| `todruaga` | pico-galagino | `todruaga.zip` | `/roms/arcade/TOWEROFDRUAGA` |
| `turtles` | pico-galagino | `turtles.zip` | `/roms/arcade/TURTLES` |
| `tutankhm` | pico-galagino | `tutankhm.zip` | `/roms/arcade/TUTANKHAM` |
| `vanvan` | pico-galagino | `vanvan.zip` | `/roms/arcade/VANVANCAR` |
| `vanguard` | pico-galagino | `vanguard.zip` | `/roms/arcade/VANGUARD` |
| `xevious` | pico-galagino | `xevious.zip` | `/roms/arcade/XEVIOUS` |
| `dlair` | pico-dragonslair | `dlair.zip` (revision F2) | `/roms/arcade/DLAIR` |
| `1943` | pico-1943 | `1943.zip` (merged), or `1943u.zip` with `1943.zip` | `/roms/arcade/1943` |
| `1943kai` | pico-1943 | `1943kai.zip` | `/roms/arcade/1943KAI` |
| `salamand` | pico-salamander | `salamand.zip` (version D) | `/roms/arcade/SALAMAND` |
| `lifefrce` | pico-salamander | `salamand.zip` (merged), or `lifefrce.zip` with `salamand.zip` | `/roms/arcade/LIFEFRCE` |

pico-dragonslair also needs its laserdisc, the file `lair.pld`, which updateAll does not install: it is converted from MAME's `dlair.chd` with `ld2pico.exe`, which the SD-card archive holds in the `ld2pico` folder next to this one. See [ld2pico/README.md](../ld2pico/README.md), and for other systems the [pico-dragonslair README](https://github.com/PicoPlus-devel/pico-dragonslair#game-data).

pico-galagino plays the Midway Pac-Man, MAME's `pacman`, a clone of `puckman`. A merged `puckman.zip`, which the download database provides, holds all of its files; with a split set, have `pacman.zip` and `puckman.zip` in the zip folder together. The same holds for the other games that use a clone: Bagman (`bagmanm2`), Bump 'n' Jump (`bnjm`, a clone of `brubber`), Donkey Kong Jr. (`dkongjrj`), Fantasy (`fantasy`, now `fantasyu` in MAME) and The Glob (`theglobp`, a clone of `suprglob`). Phoenix and Moon Cresta share their folders and sets with pico-phoenix and pico-mooncresta.

pico-1943 plays four versions of 1943; updateAll installs the US one, MAME's `1943u` (revision C), a clone of `1943`. A merged `1943.zip`, which the download database provides, holds all of its files; with a split set, have `1943u.zip` and `1943.zip` in the zip folder together. pico-1943's table of sets, `c1943/c1943_sets.c`, is the source of the two entries: `tools/mkupdateall.py` in pico-1943 writes them into `updateAll.json`.

pico-salamander plays Salamander and Lifeforce; updateAll installs Salamander version D, MAME's `salamand`, and Lifeforce US, MAME's `lifefrce`, a clone of `salamand`. A merged `salamand.zip`, which the download database provides, holds both games; with a split set, have `lifefrce.zip` and `salamand.zip` in the zip folder together. pico-salamander's table of sets, `salamand/sal_sets.c`, is the source of the two entries: `tools/mkupdateall.py` in pico-salamander writes them into `updateAll.json`.

The pico-galagino sets are those of the 47 games it plays from GalaginoPlus, all of them from its v0.2 on. Its table of sets, `galagino/gal_games.c`, is the source of these entries: `tools/mkupdateall.py` in pico-galagino writes them into `updateAll.json`.

## Game data

| ID | Application | Download | SD card folder |
| --- | --- | --- | --- |
| `doom` | *Doom!* (`doom_tiny`) | `doom1.whx`, the shareware `DOOM1.WAD` as stored in the [pico-doom](https://github.com/PicoPlus-devel/pico-doom) repository, installed as the UF2 image `doom1-whx-bootloader.uf2` for the loader to flash | `/roms/doom` |
| `duke3d` | *Duke Nukem 3D* | `3dduke13.zip`, the shareware version 1.3d, from the [Internet Archive](https://archive.org/details/3dduke13): `DUKE3D.GRP` and `LICENSE.TXT` from the archive `DN3DSW13.SHR` inside it | `/roms/duke3d` |
| `quake2` | *Quake II* | `pico-quake2_demo_sdcard.zip` from the [pico-quake2](https://github.com/PicoPlus-devel/pico-quake2/releases) release: `pak0.pak` and the player models of the Quake II demo, and its licence | `/roms/quake2` |
| `diablo` | *Diablo* (`pico-lutionX`) | `pico-lutionX_sdcard.zip` from the [pico-lutionX](https://github.com/PicoPlus-devel/pico-lutionX/releases) release: the assets of the DevilutionX engine (fonts, interface and level data) and their licences | `/roms/devilutionx` |
| `diablo_spawn` | *Diablo* (`pico-lutionX`) | `spawn.mpq`, the shareware data of Diablo, from the download of the [DevilutionX](https://github.com/diasurgical/devilutionx-assets/releases) project | `/roms/devilutionx` |

These are the shareware and demo releases their publishers made freely available. Each is installed with its licence where it has one, and is downloaded from the address in its entry, not from a database. The Quake II demo is a download of about 35 MB, the Diablo shareware about 25 MB. The `diablo` entry is not game data of a publisher but the assets of the DevilutionX engine, which are under open licences; *Diablo* needs them together with `spawn.mpq`.

The *Doom!* data file is flashed by the loader together with the game; `/roms/doom` is the first place it looks for it. *Doom Full!* (`doom_tiny_full`) needs a registered or Ultimate DOOM WAD instead, which updateAll does not install; see the pico-bootLoader README. Likewise, the full game of *Diablo*, `DIABDAT.MPQ`, is not installed; copied into `/roms/devilutionx` next to the assets, it is used instead of `spawn.mpq`.

## BIOS files

| Application | Files | SD card folder | Download |
| --- | --- | --- | --- |
| Videopac (`picoPacPlus`) | `o2rom.bin`, the Odyssey² BIOS | `/bios` | [RetroPieBIOS](https://github.com/archtaurus/RetroPieBIOS) |
| ColecoVision (`colecojam`) | `COLECO.BIN` | `/coleco` | RetroPieBIOS |
| TI-99/4A (`pico994A`) | `994aROM.bin` and `994aGROM.bin`, the console ROM and GROM | `/bios` | MiSTer BIOS database: cut out of its `boot0.rom` |
| | `994aDISK.bin`, the TI Disk Controller | `/bios` | [retrobios](https://github.com/Abdess/retrobios): the two halves in MAME's `ti99_fdc.zip`, joined |
| | `spchrom.bin`, the vocabulary of the Speech Synthesizer | `/bios` | The [RetroPie BIOS pack](https://archive.org/details/retropiebiosfilesconfiguredforeverysystem_20190904) on the Internet Archive |
| PC Engine CD (`picopcePlus`) | `Super CD-ROM System (Japan) (v3.0).pce` | `/bios` | MiSTer BIOS database |
| NES (`piconesPlus`), for Famicom Disk System games | `fds-bios.rom` | `/bios` | MiSTer BIOS database |
| Genesis (`picogenesisPlus`), for Sega CD discs | `Sega CD 2 (USA) (v2.00).md`, `Mega-CD 2 (Europe) (v2.00).md` and `Mega-CD 2 (Japan) (v2.00C).md` | `/bios` | MiSTer BIOS database |
| Commodore 64 (`picoC64`) | `c64basic.rom`, `c64kernal.rom`, `c64chargen.rom` and `1541.rom` | `/bios` | [zimmers.net](https://www.zimmers.net/anonftp/pub/cbm/firmware/) |
| Commodore 128 (`picoC64`) | `c128basic.rom`, `c128kernal.rom`, `c128chargen.rom`, `c128c64.rom` and `1571.rom` | `/bios` | zimmers.net |

The MiSTer BIOS database is [BiosDB_MiSTer](https://github.com/ajgowans/BiosDB_MiSTer), the database that the BIOS option of MiSTer's Update All uses; its files are held by the Internet Archive. None of the sources is part of this project, and each file is checked against the size and CRC32 in the configuration before it is written.

The BIOS files are listed under `bios` in `updateAll.json`, one entry for each download, with an id that starts with `bios_`. Together they form the set `bios`: the Windows program shows them as one row, **BIOS**, and `--core bios` or `-Core bios` selects all of them.

The files are installed under the names the applications look for. The Videopac, PC Engine CD and Genesis emulators recognise a BIOS by its contents rather than by its name, so a BIOS already in `/bios` under another name is used as well. pico-c64 also finds its ROMs in `/roms/C64/bios`, `/bios/c64` and `/roms/bios`; a copy in either of the first two is used before the one in `/bios`.

## Windows program

`updateAll.exe` is a single file of about 270 KB. It needs no installation and no runtime, and runs on Windows 10 and 11. The SD-card archive of pico-bootLoader holds it in the `updateAll` folder, next to `emu`.

1. Start `updateAll.exe`. The SD card is selected for you: the card the program was started from, or else the first removable drive that holds a pico-bootLoader card (an `emu` folder), or else the first removable drive.
2. Under **SD card**, choose another card if needed. Any existing folder can be chosen instead.
3. Under **Zip folder**, choose the folder with the MAME zips you already have, if any. When **Download missing files** is ticked, which it is by default, zips that are not in the folder are downloaded from the configured databases, and game data from its own address; without a zip folder they are downloaded into a temporary folder that is removed afterwards.
4. Untick the sets that are not wanted. **Select all** ticks or unticks all sets at once, and shows a mixed state when only some are ticked. The BIOS files of all emulators are one row, **BIOS**. The **Status** column shows which sets are already complete on the card.
5. Select **Install**. The list of files shows each file that is downloaded or installed, with its own progress bar and its result; a set that is already complete on the card is not listed. The bar at the bottom shows the progress of the whole run. Each set has an equal share of it, so a large download moves it slowly. **Show log** switches between the list and the log, which holds the same report as the scripts. **Dry run** reports what would be downloaded and written without doing it. **Cancel** stops a run: a download in progress is abandoned, and files already written stay on the card.

The program reads the latest configuration from this repository when it starts; until it has, **Install** is unavailable. When it cannot be read, the program uses an `updateAll.json` that is next to it, and its built-in copy otherwise. The line `Configuration:` in the log shows which one is used. Apart from the sets on the card, downloaded files in the zip folder and its temporary folder, it writes nothing: no settings, no registry entries.

The program is not signed, so Windows may show "Windows protected your PC" the first time it is started. Select **More info**, then **Run anyway**.

## Scripts

Python 3.6 or later, on Windows, Linux or macOS:

```sh
python3 updateAll.py --sd /media/user/SDCARD                        # download from the databases
python3 updateAll.py --zips ~/mame/roms --sd /media/user/SDCARD     # use zips you already have
```

Windows PowerShell 5.1 or PowerShell 7:

```powershell
.\updateAll.ps1 -Sd E:\
.\updateAll.ps1 -Zips D:\MAME\roms -Sd E:\
```

`--sd` is the root of the SD card. The sets are written below it, for example to `E:\roms\ORUN`. Any existing folder can be given instead of a card, for example `C:\temp\test` to inspect the result first.

If Windows refuses to run the script because it is not signed, start it with `powershell -ExecutionPolicy Bypass -File .\updateAll.ps1 -Zips D:\MAME\roms -Sd E:\`.

| Python | PowerShell | Description |
| --- | --- | --- |
| `--sd DIR` | `-Sd DIR` | Root of the SD card. Required. |
| `--zips DIR` | `-Zips DIR` | Folder with MAME zips you already have. Downloaded zips and game data are kept here as well; it is created if necessary. Without it, they are downloaded into a temporary folder that is removed after the run. Required when nothing is to be downloaded. |
| `--core ID[,ID]` | `-Core ID[,ID]` | Install only these sets; `bios` stands for all BIOS files. Default: all configured sets. |
| `--db SRC[,SRC]` | `-Database SRC[,SRC]` | Download databases to use in addition to those in the configuration: URLs or files. |
| `--no-download` | `-NoDownload` | Use only the files already in the zip folder. |
| `--dry-run` | `-DryRun` | Report what would be downloaded and written without doing it. |
| `--list` | `-List` | List the configured sets and databases. |
| `--config FILE` | `-Config FILE` | Use another configuration file. Default: `updateAll.json` next to the script. When it has a `config_url`, the configuration is read from there, and from this file only if that fails. |

## Behaviour

- The card is checked first. A set that is already complete on the card needs no zip, so nothing is read or downloaded for it.
- Zips are looked up by name in the zip folder, ignoring case. A zip that is not there is downloaded from the first database that lists it. A set whose zip is neither in the folder nor in a database is skipped.
- Game data is looked up in the zip folder under the last part of its address, for example `3dduke13.zip`, and downloaded from that address when it is not there.
- Databases are only read when a zip is needed and missing, so a run on an up-to-date card, or with every zip at hand, needs no network access beyond reading the configuration (see [Latest sets](#latest-sets)). A zip that is already in the zip folder is never downloaded again.
- A download is written to a `.part` file and kept only when its size and MD5 match the database and it is a valid zip. Game data has no database entry and need not be a zip; its files are checked against the configuration when they are installed. Failed downloads are retried twice, except after an HTTP 4xx response.
- Only the files listed in the configuration are extracted. They are identified by size and CRC32, as the applications identify them, so merged, split and non-merged sets are all accepted, as are sets whose files carry other names. In a merged set, the files at the root of the zip take precedence over the clone subfolders.
- Game data that is a single file rather than a zip is installed as it is, or, when its entry has `uf2`, as a UF2 image of it. The report then names the conversion, for example `converted doom1.whx to a UF2 image at 0x10200000`, and an image that does not match the configuration is reported as not the expected file. The zips that an entry names under `archives`, such as the self-extracting archive inside the Duke Nukem 3D shareware zip, are searched as well.
- A BIOS file with `offset` or `parts` is built from its download first: a part of a download that is one file, or entries of a zip joined. It is then checked like any other file, and one that does not match the configuration is reported as not the expected file. A part that is not in the zip is reported by name.
- Files are written under the names listed in the configuration. pico-outrun opens its files by name, so for OutRun this is required.
- A file that is already on the card with the correct size and CRC32 is left as it is. Other files on the card are never modified or deleted.
- Every file is verified before it is written. A damaged entry, or a file of the expected name with other contents, is reported and not written.
- The exit status is 0 when every set that was found is complete on the card, and 1 when a set is incomplete, a download or extraction failed, a database could not be read, no set was found at all, or a set requested with `--core` was not found. A dry run counts the sets it would download as found. A configuration that cannot be read from `config_url` does not change the exit status.

## Latest sets

The shipped `updateAll.json` names its own copy in this repository under `config_url`:

```json
"config_url": "https://raw.githubusercontent.com/PicoPlus-devel/pico-bootLoader/main/updateAll/updateAll.json"
```

At every start, updateAll reads the configuration from that address. It replaces the local configuration in full, the sets as well as the databases; its own `config_url` is not followed. The first line of the output, `Sets from:`, shows the address. When the configuration cannot be read there, for example without network access, the local `updateAll.json` is used, or the built-in copy of the Windows program, and `Sets from:` gives the reason.

To use the local configuration only, for example after editing it, remove `config_url` from it or set it to `""`.

## Download databases

A database uses the format of the [MiSTer Downloader](https://github.com/MiSTer-devel/Downloader_MiSTer/blob/main/docs/custom-databases.md), so a database written for MiSTer can be used without changes. It is a JSON file, or a zip that contains one. Only the fields below are read; any others are ignored.

```json
{
  "db_id": "my_arcade_roms",
  "base_files_url": "https://example.org/roms/",
  "files": {
    "games/mame/phoenix.zip": {
      "size": 202758,
      "hash": "<MD5 of the zip>",
      "url": "https://example.org/roms/games/mame/phoenix.zip"
    }
  }
}
```

| Field | Meaning |
| --- | --- |
| `db_id` | Name shown in the output. Optional. |
| `base_files_url` | Prefix for files without a `url`: the download address is this prefix followed by the key in `files`. Optional. |
| `files` | The downloadable files. The key is a path whose last component is the zip name the configuration asks for. A leading `\|`, which MiSTer uses to mark paths for external storage, is ignored. When a database lists the same zip name in several folders, the one in a folder named `mame` is used. |
| `url` | Download address. Required unless `base_files_url` is set. |
| `size` | Size in bytes. Optional, checked when present. |
| `hash` | MD5 of the zip. Optional, checked when present. |

Databases are listed in `updateAll.json` under `databases`, as URLs or as file paths relative to the configuration file, or given on the command line. The shipped configuration lists the ArcadeROMsDB_MiSTer database; an empty list, `"databases": []`, switches downloading of ROM sets off. Game data is still downloaded, unless **Download missing files** is unticked or `--no-download` is given. Since the configuration read from `config_url` replaces the local one, remove `config_url` as well; see [Latest sets](#latest-sets). The Windows program reads an `updateAll.json` placed next to it in preference to its built-in copy, so editing the file in the `updateAll` folder is enough. The format of the list:

```json
{
  "databases": ["https://example.org/arcade_roms_db.json.zip"],
  "cores": [ ... ]
}
```

## Adding a set

Sets are defined in `updateAll.json`; the scripts themselves do not change. Once the file on the `main` branch has the new set, every copy of updateAll from v0.7.2 on reads it at its next start, so no new SD-card archive is needed. Every released copy reads that one file, so a change that older copies cannot read goes into a file with another name, and the next release points `config_url` at it.

Add an entry to `cores`:

```json
{
  "id": "example",
  "name": "Example",
  "set": "MAME example (description)",
  "zips": ["example.zip"],
  "destination": "/roms/arcade/EXAMPLE",
  "files": [
    { "name": "file1.bin", "size": 2048, "crc": "0123abcd" }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `id` | Short name used with `--core` and `-Core`. |
| `name` | Name shown in the output. |
| `set` | Description shown by `--list`. |
| `zips` | Zips to search, in order. For a clone in a split set, list the parent zip as well, since the clone zip does not contain the files it shares with the parent. |
| `destination` | Folder on the SD card, relative to its root. |
| `files` | Every file the application needs: the name it is written under, its size in bytes, and its CRC32 in hexadecimal. The values are those of the application's ROM table, and of `mame -listxml <set>`. |

## Adding game data

Game data that comes from one address is added to `data`, a list next to `cores`. Copies of updateAll older than the list skip it, so they keep working with the same file.

```json
"data": [
  {
    "id": "quake2",
    "name": "Quake II",
    "set": "demo 3.14",
    "url": "https://github.com/PicoPlus-devel/pico-quake2/releases/download/v0.2-alpha/pico-quake2_demo_sdcard.zip",
    "destination": "/roms/quake2",
    "files": [
      { "name": "baseq2/pak0.pak", "size": 49951322, "crc": "cdd535b6" }
    ]
  }
]
```

The fields are those of a set, except:

| Field | Meaning |
| --- | --- |
| `url` | Download address, instead of `zips`. The download is kept in the zip folder under the last part of the address. It may be a zip, or the one file to install. Point it at a fixed release rather than the latest one, so that the sizes and CRC32s below stay correct. |
| `archives` | Optional. Names of zips inside the download to search as well, such as `DN3DSW13.SHR` in the Duke Nukem 3D shareware zip, which is a self-extracting archive. |
| `uf2` | Optional. `{ "address": "0x10200000", "family": "0xe48bff58" }`: the download is one binary file, installed as a UF2 image of it at that flash address with that family ID, as `picotool uf2 convert -t bin -o <address> --family <family>` writes it. The `files` entry gives the name, size and CRC32 of the image. |
| `files` | As for a set, but a name may include subfolders of `destination`, separated with `/`, such as `baseq2/pak0.pak`. |

## Adding BIOS files

BIOS files are added to `bios`, a list next to `data`. Copies of updateAll older than the list skip it. Its entries have the fields of game data, see [Adding game data](#adding-game-data), and two more fields of a file build it from the download instead of taking it as it is:

```json
"bios": [
  {
    "id": "bios_ti99",
    "name": "TI-99/4A BIOS",
    "set": "console ROM and GROM, from the MiSTer BIOS database",
    "url": "https://archive.org/download/mister_bios_db/TI-99_4A.zip/boot0.rom",
    "destination": "/bios",
    "files": [
      { "name": "994aROM.bin",  "size": 8192,  "crc": "db8f33e5", "offset": "0x2A000" },
      { "name": "994aGROM.bin", "size": 24576, "crc": "af5c2449", "offset": "0x10000" }
    ]
  }
]
```

| Field | Meaning |
| --- | --- |
| `offset` | Optional, in hexadecimal. The file is the `size` bytes at this offset of the download, which is then taken as one file, not as a zip. |
| `parts` | Optional. Names of entries of the zip, for example `["fdc_dsr.u26", "fdc_dsr.u27"]`: the file is these entries joined in this order. Each is looked up by name, ignoring case, at the root of the zip first and then in its subfolders. |

An entry uses `offset` or `parts`, not both, and neither together with `uf2`. In `cores` and `data` both fields are ignored. The id `bios` is reserved for the set of all BIOS files.

## Building the Windows program

`updateAll.exe` is built on Linux with mingw-w64 (`apt install mingw-w64`). `updateAll.json` is built in, so it is rebuilt after a set is added:

```sh
win/build.sh                              # -> win/build/updateAll.exe
```

`build_emulators.sh -z` builds it before it packs the SD-card archive.

`win/ua_core.c` is a port of `updateAll.py`. `win/hosttest/` builds it for Linux with AddressSanitizer and checks that both give the same output, exit status and files on the card, for local zips as well as for downloads from a local HTTP server:

```sh
win/hosttest/build.sh && win/hosttest/compare.py
```

A change to how one implementation behaves is made in all three. The icon is drawn by `win/make_icon.py`, which writes `win/updateAll.ico`.
