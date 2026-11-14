# Releasing

Maintainer checklist for publishing pico-bootLoader. Four artifacts ship, and
they have different lifecycles:

| Artifact | Built by | How it gets onto the release |
| --- | --- | --- |
| `pico-bootLoader_<board>_arm[_piousb].uf2` (11 files) | CI, on the self-hosted runner | attached automatically by the workflow |
| `pico-bootLoader_sdcard.zip` | **locally**, by `build_emulators.sh -c all -z` | `gh release upload`, by hand |
| `updateAll-<version>.exe` | CI, on the self-hosted runner | attached automatically by the workflow, to every release |
| PCB gerbers (4 zips: PicoNES v2.6, PicoNES v2.8, Mini v2.0, Micro v1.2) | nobody — vendored in the `pico_shared` submodule | attached automatically by the workflow |

The archive is not built in CI because it needs every application's toolchain and
takes hours; the loader builds take minutes. That split is why step 4 below is
manual.

## Tag convention

| Tag | Meaning | What users must do |
| --- | --- | --- |
| `v0.N` | bootloader firmware changed | re-flash the board **and** refresh the card |
| `v0.N.M` | same firmware, refreshed SD-card content | refresh the card only |

A three-component tag makes the workflow add a banner to the release notes,
after the CHANGELOG, saying the firmware is unchanged since `v0.N`, so users know
not to re-flash.
Both forms are matched by the existing `push:` tag globs, and `v*.*-alpha` is
available for dry runs.

updateAll reports a newer archive and installs it on a card with an `emu` folder:
it takes the newest release with a `vN.M` or `vN.M.P` tag that holds
`pico-bootLoader_sdcard.zip`, and compares it with `/emu/release.txt` on the card.
So once the archive is uploaded, every copy of updateAll from v0.9 on offers it. A
release without the archive is passed over, and is not reported. `-alpha` tags are
ignored, so a dry run is never offered. Upload a corrected archive under a new
tag: a card already on a tag does not notice that its archive was replaced.

A set added to `updateAll/updateAll.json` needs no release: every copy of
updateAll from v0.7.2 on reads that file from `main` at start-up (its
`config_url`), and falls back to the copy on the card only when it cannot. Keep
the file on `main` at least as complete as the one in the latest archive.

The game data in its `data` list is downloaded from fixed tags or releases of
other repositories, which must exist before the file on `main` names them.
Doom's entry downloads `doom1.whx` from the pico-doom tag v0.3 and converts it
into `doom1-whx-bootloader.uf2` at 0x10200000 itself; pico-doom does not release
that file, so that it cannot be flashed by mistake. The address in the entry
must match pico-doom's bootloader builds (`TINY_WAD_ADDR` in its
`*_cflags.h`). `build_emulators.sh` builds Doom from pico-doom's latest release
tag, so that tag must be v0.4 or later, the first to put the WAD there on every
board and to name it `doom1-whx-bootloader.uf2`. The SD-card archive no longer
packs the WAD image, so a card made from it runs *Doom!* only after updateAll
has run.

The pico-dragonslair and pico-quake2 releases attach an `updateAll.exe` too,
built by their own workflows from this repository's `main`. A change to
updateAll reaches them only once it is merged and their next release is cut.

This repository's `updateAll-<version>.exe` asset is what the Windows program offers
as an update of itself, to every copy from bootloader release v0.9 on, wherever that
copy came from. The program has a version of its own in `updateAll/win/VERSION`,
separate from the tags of this repository. Every build reports that number, whatever
the state of the tree, so nothing raises it by itself.

**Raise `updateAll/win/VERSION` with every change to `updateAll/win`** (`hosttest`
excepted). The workflow attaches the program to every release, so that a new user
finds it on the latest one, but a copy offers an update only when a release holds a
higher number than its own, so that users are asked only when the program really
changed. It stops at the start when `updateAll/win` changed and the number did not,
and `build_emulators.sh -z -r` stops on the same check before it builds anything. A
change to the sets in `updateAll/updateAll.json` needs no new version, as every copy
reads that file from `main` when it starts.

Commit changes to `updateAll/win` before building the archive, so that the copy in
the archive and the one on the release are the same. A copy already on a tag is not
told that its asset was replaced, so a bad exe needs a new version. To stop every copy
from offering an update, remove `program_asset` from `updateAll/updateAll.json` on
`main`.

## Prerequisites

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
export PICO_PIO_USB_PATH=~/pico/Pico-PIO-USB   # required for HW_CONFIG 7, 8, 9, 14, 18
export PICO_EXTRAS_PATH=~/pico/pico-extras     # required by pico-doom only
gh auth status                                  # needs repo write
```

`zip` or `python3` must be present for the packer (either is fine), and
mingw-w64 (`x86_64-w64-mingw32-gcc`, `apt install mingw-w64`) for
`updateAll.exe`, which `-z` builds and packs into the archive's `updateAll/`
folder. `-z` also needs `curl`: it downloads `ld2pico.exe`, the Dragon's Lair
laserdisc converter, from the pico-dragonslair release of the tag it bundled,
and packs it into the archive's `ld2pico/` folder.

`PICO_EXTRAS_PATH` is a pico-doom requirement, not an emulator one — pico-doom
takes its whole toolchain from the environment via its `pico-env.sh` rather than
vendoring it. Leave it unset and everything else still builds, pico-duke3D
included; the two Doom variants are reported as `SKIP` with the reason, and the
archive ships without Doom.

---

## Scenario A — bootloader firmware changed

The emulators are untouched, so the existing `pico-bootLoader_sdcard.zip` from
the previous release stays valid and does **not** need re-uploading. Note this
explicitly in the release notes, otherwise users will hunt for a missing asset.

```bash
# 1. Update CHANGELOG.md — its whole contents become the release body,
#    followed by the generated version table.
$EDITOR CHANGELOG.md
git commit -am "Changelog for v0.3" && git push

# 2. Build and publish. The release step creates the tag itself.
gh workflow run BuildAndRelease.yml -f tag=v0.3
gh run watch
```

Then follow [Verifying a release](#verifying-a-release). Pushing the tag by hand
(`git tag v0.3 && git push origin v0.3`) does exactly the same thing.

## Scenario B — an emulator was updated (firmware unchanged)

This is the common case: an emulator repo got a new tag, so the card content
changes but the board does not need re-flashing.

```bash
# 1. Build every emulator from its latest release tag and pack the archive.
#    -j 3 keeps total load near nproc; -z requires -c all. -r is the tag of step 3.
./build_emulators.sh -c all -j 3 -z -r v0.2.1
```

Check the grand summary before going further. Every emulator should be `BUILT`
for the boards it supports. `SKIP` is expected for excluded combinations
(`picogenesisPlus` on HW 7; `picosnesPlus`, `picoOutRun` and `picoSalamander` on
HW 1, 5, 6, 7, 9; Doom outside boards 2, 8, 13, 14, 15, 18; `duke3d_game` outside boards 2, 8, 13, 15, 18;
`pico-quake2` and `pico-lutionX` outside boards 2, 8, 18; `colecojam` outside
board 8). The updateAll entry `diablo` downloads the SD-card assets of
pico-lutionX's release v0.1-alpha, so that release must stay published.
A `FAIL` for
`pico-quake2` that says `fruitjam-build-forbootloader.sh not found` means its
latest release tag predates the bootloader build scripts; tag a newer
pico-quake2 release first. Likewise, a `FAIL` for `doom_tiny` that says
`aux image doom1-whx-bootloader.uf2 not produced` means pico-doom's latest tag
predates v0.4. Any `FAIL` or `MISSING` means the archive is
incomplete — fix it and re-run rather than shipping a partial card.

The packer refuses to build an archive containing a 0-byte `.uf2`, so a failed
link cannot ship silently. If it stops with
`refusing to build an archive that ships a broken build`, rebuild the emulator it
names.

```bash
# 2. Commit the manifest so the release notes can quote it.
#    emu/versions.txt records <program>;<repo>;<tag>;<pico_shared> per emulator.
git add emu/versions.txt
git commit -m "Refresh emulator bundle" && git push

# 3. Build the loader and publish, bumping the third component.
gh workflow run BuildAndRelease.yml -f tag=v0.2.1
gh run watch

# 4. Attach the archive to the release the workflow just created.
gh release upload v0.2.1 releases/pico-bootLoader_sdcard.zip
```

## Scenario C — both changed

Run Scenario B's steps 1–2, with the new tag as `-r`, fold the firmware notes into
`CHANGELOG.md`, then publish with a **two-component** tag (`v0.3`, not `v0.3.0`) so
users are told to re-flash, and upload the archive as in step 4.

---

## Dry run

Two ways to rehearse without publishing:

```bash
# Build everything, release nothing (empty tag input).
gh workflow run BuildAndRelease.yml
```

```bash
# Full rehearsal against a throwaway tag, then remove it.
gh workflow run BuildAndRelease.yml -f tag=v0.2.1-alpha
gh run watch
gh release upload v0.2.1-alpha releases/pico-bootLoader_sdcard.zip
gh release delete v0.2.1-alpha --cleanup-tag
```

The full rehearsal also checks the version of updateAll, and stops in the same way
when `updateAll/win` changed and `VERSION` did not. A program attached to an `-alpha`
release is offered to nobody.

## Verifying a release

```bash
gh release view v0.2.1
```

- **11 loader `.uf2` assets** — one per board. The `_pico2_w_` variants for
  HW_CONFIG 1 and 2 are no longer built; a Pico 2 W runs the `pico2` asset.
- `pico-bootLoader_sdcard.zip` present, unless this is a Scenario A release.
  Its `updateAll/` folder holds `updateAll.exe` next to the scripts
  (`unzip -l releases/pico-bootLoader_sdcard.zip | grep updateAll/`), and its
  `ld2pico/` folder holds `ld2pico.exe` and its README
  (`unzip -l releases/pico-bootLoader_sdcard.zip | grep ld2pico/`).
  `unzip -p releases/pico-bootLoader_sdcard.zip emu/release.txt` prints the tag
  of this release. If it does not, updateAll still installs the archive, but a
  card unpacked by hand downloads it once more.
- `updateAll-<version>.exe` present, with the version in `updateAll/win/VERSION`.
  The log of the workflow's check says whether that version is new or attached
  again. The window title of the program shows that version.
- **4 PCB gerber assets** — `pico_nesPCB_v2.6.zip`,
  `pico_nesPCB_v2.8_SNES_PORTS.zip`, `Gerber_PicoNES_Mini_PCB_v2.0.zip` and
  `Gerber_PicoNES_Micro_v1.2.zip`,
  attached by the workflow from the `pico_shared` submodule. See
  [Custom PCBs](README.md#custom-pcbs).
- For a `v0.N.M` tag, the "SD-card content update" banner follows the
  CHANGELOG text, just above the version table.
- The version table lists every emulator with a real tag — no `VX.X`, no blanks
  in the Version column.
- On hardware: unzip the archive at the root of a FAT32 card, boot, confirm
  the picker lists every entry, launch one emulator and check it shows its
  version rather than a build date. Run updateAll on the card, launch *Doom!*
  and confirm the WAD flashes from `/roms/doom/`. Without the file, *Doom!*
  shows "DATA FILE NOT FOUND" instead of starting.

## Rolling back

```bash
gh release delete v0.2.1 --cleanup-tag   # removes the release and its tag
```

Re-cutting the same tag afterwards is fine — the release step creates the tag
from `github.sha`, so it will point at whatever is current.

---

## Notes and gotchas

- **The version stamp is applied at build time, never committed.** CI seds the
  tag into `CMakeLists.txt` and `pico_shared/menu.h`; `build_emulators.sh` seds
  each emulator's own `pico_shared/menu.h`. In-tree both keep their placeholders
  (`"0.1"`, `"VX.X"`), and a build from an unstamped tree shows the build date
  instead — that is how a dev build identifies itself. updateAll is the exception:
  its version is committed in `updateAll/win/VERSION`, and no build stamps it.
- **`pico_shared` in tag mode is `main`, not the revision the tag pins.** Tag mode
  builds each emulator's tagged source against `pico_shared` `main` and records
  both refs in the manifest. The reason was historical: `bld.sh` gained `-b`
  (`BUILD_FOR_BOOTLOADER`) only in `pico_shared` `f2c8be9`, and the emulator tags
  of the time predated it, so their pinned revision rejected `-b` outright.
  That reason no longer applies — every current tag pins a descendant of
  `f2c8be9` — but the substitution is not always a no-op. For v0.9 Salamander
  v0.2 and Commodore 64 v0.2 pin `cd147e0`, every other emulator tag pins
  `8b93b28`, and the bundle is built against `main`, `f16f692`. For v0.10 every
  emulator tag pins `8048999`, which is `main`. The whole bundle is built against
  the one `pico_shared` `main` that `emu/versions.txt` records.
  An older tag that no longer builds against `main` shows up as a `FAIL` in the
  grand summary.
- **A tag pushed from CI would not trigger this workflow** — GitHub does not fire
  workflows for events created with the default `GITHUB_TOKEN`. That is why the
  release step passes `tag_name` + `target_commitish` and creates the tag itself
  rather than pushing one, and why no PAT is needed.
- **`CHANGELOG.md` is used whole** as the start of the release body; the
  generated banner (for `v0.N.M`) and version table are appended after it, so
  the table sits just above the asset list. There is no per-tag extraction, so
  prune it when it grows unwieldy.
- **The release body is unwrapped.** GitHub shows release notes like a comment,
  where every line break stays a break, while it joins the lines of a paragraph
  in a file in the repository. The workflow therefore passes the notes through
  `.github/scripts/unwrap_markdown.py`, which joins the wrapped lines of each
  paragraph, list item and quote, so `CHANGELOG.md` can stay wrapped. A body
  edited by hand needs the same: `python3 .github/scripts/unwrap_markdown.py
  CHANGELOG.md`.
- **`emu/<HW_CONFIG>/` is gitignored** — the built emulator UF2s live only in the
  archive. `emu/versions.txt` is tracked precisely so the shipped versions are
  recorded in git.
- **`emu/emulators.txt` remains the master list** even on a card that uses
  categories: `build_emulators.sh` and `pack_sdcard.sh` both work from it, so a
  program listed only in a category file would never be built or packed. The
  packer warns when it finds one. `emu/categories.txt` and the config files it
  names are packed alongside it, as is each theme's `Categories/` artwork.
- **The self-hosted runner hardcodes** `/datalocal/pico/pico-sdk` and
  `/datalocal/Pico-PIO-USB`; a GitHub-hosted runner cannot build this.
