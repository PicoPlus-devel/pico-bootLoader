---
description: Prepare a pico-bootLoader release, up to the point of publishing
argument-hint: "[tag, such as v0.9 or v0.9.1]"
---

Prepare the next release of pico-bootLoader, up to the point of publishing.
Tag: $ARGUMENTS (if there is none above, propose one in step 1). Below, <TAG> is
this tag.

RELEASING.md is the procedure: read it first. If this prompt and RELEASING.md
disagree, follow RELEASING.md and tell me.

Do not commit, push, merge, tag, dispatch a workflow, upload, or delete anything
until I ask for that step. Approval of one step does not cover the next. Reading,
local builds and local tests are fine, but ask before the archive build (it takes
hours) and before any Windows GUI test (it takes the keyboard focus).

1. Work out what this release is
- List what changed since the last release tag, including the pico_shared pointer
  and the emulator tags in emu/versions.txt.
- Decide the scenario (A, B or C) and so the tag form, and show the evidence. Ask
  me if it is unclear whether the firmware changed.
- Say which branch this is on and what is not yet on main. `gh workflow run` uses
  main unless given --ref.

2. Check before building, and report each item as passed or failed
- Clean tree, branch level with origin, submodule pointers on pushed commits.
- The prerequisites in RELEASING.md.
- Each emulator's latest release tag against emu/versions.txt.
- The external dependencies RELEASING.md names: the pico-doom and pico-quake2 tags,
  pico-lutionX v0.1-alpha still published. Check that the URLs in the data and bios
  lists of updateAll/updateAll.json still answer.
- `python3 .github/scripts/updateall_version.py <TAG>`. If it stops, propose the new
  number for updateAll/win/VERSION.
- `updateAll/win/hosttest/build.sh`, then `updateAll/win/hosttest/compare.py`: all
  cases must agree. Show any failure in full.

3. Documents
- CHANGELOG.md: add a section for the tag at the top, for end users, brief and not
  too technical. It says whether to re-flash and whether to refresh the card. In
  Scenario A it says the existing archive stays valid.
- The whole file becomes the release body, so also check that General Info and the
  Getting started section at the end are still true.
- README.md and updateAll/README.md: formal, no promotional language. Update
  whatever this release made untrue, such as Getting started, Supported hardware
  and Bootable applications.
- Check each claim against the code or the git log, not against memory, and list
  what you could not verify.
- Show me the diff and stop. I decide the wording.

4. SD-card archive, for Scenario B or C
- It needs the documents agreed and all changes to updateAll/win committed, as it
  holds a copy of the program. An existing releases/pico-bootLoader_sdcard.zip is
  stale if it is older than the last commit touching updateAll or emu.
- Ask, then run `./build_emulators.sh -c all -j 3 -z -r <TAG>` in the background.
- Read the grand summary against the expected SKIP list in RELEASING.md. On any FAIL
  or MISSING, stop and report: do not work around it.
- Check the zip: emu/release.txt holds the tag, updateAll/ holds updateAll.exe
  reporting the number in VERSION, ld2pico/ is present, and no .uf2 is empty. Show
  what changed in emu/versions.txt.

5. Report
- The scenario and tag, the results of step 2, and what changes for users.
- What is unverified, including what only a board can show. Do not flash anything
  unless I ask.
- The remaining commands in order, not run: commit, merge to main, dispatch, watch,
  upload the archive, and the checks under "Verifying a release".
- Suggest an -alpha dry run only if the workflow or its scripts changed since the
  last real release. Warn that it is public and shows as Latest until deleted.
