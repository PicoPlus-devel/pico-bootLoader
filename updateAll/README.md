# updateAll

`updateAll` installs arcade ROM sets on the SD card of a pico-bootLoader console or of a standalone arcade application. It takes MAME ROM zips from a folder, downloads the zips that are not in it from the configured databases, and extracts each set into the folder on the SD card where its application looks for it.

The ROM sets are copyright of their manufacturers. They are not included, and no download source is configured: the user supplies the zips, or a database to download them from, and is responsible for holding the rights to them.

Three equivalent implementations are provided: `updateAll.exe`, a Windows program with a window, `updateAll.py` (Python) and `updateAll.ps1` (PowerShell). All three read the same configuration file, `updateAll.json`; the Windows program also carries a built-in copy of it.

## Supported sets

| ID | Application | MAME zip | SD card folder |
| --- | --- | --- | --- |
| `outrun` | pico-outrun | `outrun.zip` (revision B) | `/roms/ORUN` |
| `phoenix` | pico-phoenix | `phoenix.zip` | `/roms/arcade/PHOENIX` |
| `mooncresta` | pico-mooncresta | `mooncrst.zip` | `/roms/arcade/MOONCRESTA` |

## Windows program

`updateAll.exe` is a single file of about 180 KB. It needs no installation and no runtime, and runs on Windows 10 and 11. The SD-card archive of pico-bootLoader holds it in the `updateAll` folder, next to `emu`.

1. Start `updateAll.exe`. The SD card is selected for you: the card the program was started from, or else the first removable drive that holds a pico-bootLoader card (an `emu` folder), or else the first removable drive.
2. Under **SD card**, choose another card if needed. Any existing folder can be chosen instead.
3. Under **Zip folder**, choose the folder with the MAME zips you already have, if any. When **Download missing zips** is ticked, zips that are not in the folder are downloaded from the configured databases; without a zip folder they are downloaded into a temporary folder that is removed afterwards.
4. Untick the sets that are not wanted. The **Status** column shows which sets are already complete on the card.
5. Select **Install**. The log shows the same report as the scripts. **Dry run** reports what would be downloaded and written without doing it. **Cancel** stops a run before the next file.

The program uses an `updateAll.json` that is next to it, and its built-in copy otherwise, so a set added to the file is used without a new program. Apart from the sets on the card, downloaded zips in the zip folder and its temporary folder, it writes nothing: no settings, no registry entries.

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
| `--zips DIR` | `-Zips DIR` | Folder with MAME zips you already have. Downloaded zips are kept here as well; it is created if necessary. Without it, zips are downloaded into a temporary folder that is removed after the run. Required when no database is configured. |
| `--core ID[,ID]` | `-Core ID[,ID]` | Install only these sets. Default: all configured sets. |
| `--db SRC[,SRC]` | `-Database SRC[,SRC]` | Download databases to use in addition to those in the configuration: URLs or files. |
| `--no-download` | `-NoDownload` | Use only the zips already in the zip folder. |
| `--dry-run` | `-DryRun` | Report what would be downloaded and written without doing it. |
| `--list` | `-List` | List the configured sets and databases. |
| `--config FILE` | `-Config FILE` | Use another configuration file. Default: `updateAll.json` next to the script. |

## Behaviour

- The card is checked first. A set that is already complete on the card needs no zip, so nothing is read or downloaded for it.
- Zips are looked up by name in the zip folder, ignoring case. A zip that is not there is downloaded from the first database that lists it. A set whose zip is neither in the folder nor in a database is skipped.
- Databases are only read when a zip is needed and missing, so a run on an up-to-date card, or with every zip at hand, needs no network access. A zip that is already in the zip folder is never downloaded again.
- A download is written to a `.part` file and kept only when its size and MD5 match the database and it is a valid zip. Failed downloads are retried twice, except after an HTTP 4xx response.
- Only the files listed in the configuration are extracted. They are identified by size and CRC32, as the applications identify them, so merged, split and non-merged sets are all accepted, as are sets whose files carry other names. In a merged set, the files at the root of the zip take precedence over the clone subfolders.
- Files are written under the names listed in the configuration. pico-outrun opens its files by name, so for OutRun this is required.
- A file that is already on the card with the correct size and CRC32 is left as it is. Other files on the card are never modified or deleted.
- Every file is verified before it is written. A damaged entry, or a file of the expected name with other contents, is reported and not written.
- The exit status is 0 when every set that was found is complete on the card, and 1 when a set is incomplete, a download or extraction failed, a database could not be read, no set was found at all, or a set requested with `--core` was not found. A dry run counts the sets it would download as found.

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

Databases are listed in `updateAll.json` under `databases`, as URLs or as file paths relative to the configuration file, or given on the command line:

```json
{
  "databases": ["https://example.org/arcade_roms_db.json.zip"],
  "cores": [ ... ]
}
```

## Adding a set

Sets are defined in `updateAll.json`; the scripts themselves do not change. Add an entry to `cores`:

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
