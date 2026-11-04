#!/usr/bin/env python3
"""
Runs updateAll.py and ua_host (the core of updateAll.exe) on the same cases
and checks that they print the same, exit with the same status and leave the
same files behind. Downloads come from a local HTTP server.

It also checks what the core tells the list of files of updateAll.exe against
the report the script printed, and that cancelling leaves no file unfinished.

Usage: build.sh && compare.py [-k NAME] [-v]
"""

import argparse
import difflib
import hashlib
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "..", "updateAll.py")
HOST = os.path.join(HERE, "build", "ua_host")

# (name, arguments, starting card)
CASES = [
    ("local", "--config cfg_local.json --zips {zips} --sd {card}", None),
    ("local-dry-run", "--config cfg_local.json --zips {zips} --sd {card} --dry-run", None),
    ("local-again", "--config cfg_local.json --zips {zips} --sd {card}", "full"),
    ("local-partial", "--config cfg_local.json --zips {zips} --sd {card}", "card_partial"),
    ("core-filter", "--config cfg_local.json --zips {zips} --sd {card} --core alpha,BETA --core gamma", None),
    ("core-skipped", "--config cfg_local.json --zips {zips} --sd {card} --core eta", None),
    ("core-unknown", "--config cfg_local.json --zips {zips} --sd {card} --core alpha,nope,Zap", None),
    ("sd-missing", "--config cfg_local.json --zips {zips} --sd {fx}/no-card", None),
    ("nothing-to-install", "--config cfg_local.json --sd {card}", None),
    ("zips-missing", "--config cfg_local.json --zips {fx}/no-zips --sd {card}", None),
    ("config-missing", "--config no-such.json --zips {zips} --sd {card}", None),
    ("db-http", "--config cfg_db.json --zips {zips} --sd {card}", None),
    ("db-http-dry-run", "--config cfg_db.json --zips {zips} --sd {card} --dry-run", None),
    ("db-temp-folder", "--config cfg_db.json --sd {card} --core theta,mu,pi,kappa", None),
    ("db-new-zip-folder", "--config cfg_db.json --zips {run}/new/zips --sd {card} --core theta,omicron", None),
    ("db-no-download", "--config cfg_db.json --zips {zips} --sd {card} --no-download", None),
    ("db-file-relative", "--config cfg_dbfile.json --zips {zips} --sd {card} --core theta,mu,omicron,pi", None),
    ("db-zipped-file", "--config cfg_nodb.json --db db_zipped.json.zip --zips {zips} --sd {card} --core theta,mu,pi", None),
    ("db-errors", "--config cfg_nodb.json --db missing.json,{url}/nope.json --db {url}/nofiles.json "
                  "--db {url}/broken.json,db_nojson.zip --zips {zips} --sd {card} --core theta,omicron", None),
    ("db-error-and-good", "--config cfg_nodb.json --db {url}/nope.json,{url}/db.json --zips {zips} "
                          "--sd {card} --core theta", None),
    ("config-url", "--config cfg_fetch.json --zips {zips} --sd {card} --core theta,omicron", None),
    ("config-url-404", "--config cfg_fetch404.json --zips {zips} --sd {card} --core alpha", None),
    ("config-url-broken", "--config cfg_fetchbroken.json --zips {zips} --sd {card} --core alpha", None),
    ("config-url-nocores", "--config cfg_fetchnocores.json --zips {zips} --sd {card} --core alpha", None),
    ("config-url-off", "--config cfg_fetchoff.json --zips {zips} --sd {card} --core alpha", None),
    ("config-url-sd-missing", "--config cfg_fetch.json --zips {zips} --sd {fx}/no-card", None),
    ("data-http", "--config cfg_data.json --zips {zips} --sd {card}", None),
    ("data-dry-run", "--config cfg_data.json --zips {zips} --sd {card} --dry-run", None),
    ("data-temp-folder", "--config cfg_data.json --sd {card} --core rho,sigma,tau,omega", None),
    ("data-again", "--config cfg_data.json --sd {card} --core rho,sigma,tau,omega", "full_data"),
    ("data-partial", "--config cfg_data.json --zips {zips} --sd {card} --core sigma", "card_data_partial"),
    ("data-no-download", "--config cfg_data.json --zips {zips} --sd {card} --no-download --core rho,sigma", None),
    ("data-no-download-no-zips", "--config cfg_data.json --sd {card} --no-download", None),
    ("data-local", "--config cfg_data.json --zips {fx}/zips_data --sd {card} --no-download "
                   "--core rho,sigma,tau", None),
    ("data-uf2-local", "--config cfg_data.json --zips {fx}/zips_data --sd {card} --core omega", None),
    ("data-uf2-local-dry-run", "--config cfg_data.json --zips {fx}/zips_data --sd {card} --dry-run "
                               "--core omega", None),
    ("data-uf2-mismatch", "--config cfg_data.json --zips {zips} --sd {card} --core omegabad", None),
    ("data-core-filter", "--config cfg_data.json --zips {zips} --sd {card} --core TAU,theta", None),
    ("data-only", "--config cfg_data.json --zips {zips} --sd {card} --core upsilon,phi,chi,psi", None),
    ("bios-http", "--config cfg_bios.json --zips {zips} --sd {card}", None),
    ("bios-dry-run", "--config cfg_bios.json --zips {zips} --sd {card} --dry-run", None),
    ("bios-alias", "--config cfg_bios.json --sd {card} --core bios", None),
    ("bios-alias-case", "--config cfg_bios.json --sd {card} --core BIOS,b_plain", None),
    ("bios-alias-and-set", "--config cfg_bios.json --zips {zips} --sd {card} --core theta --core bios", None),
    ("bios-one", "--config cfg_bios.json --sd {card} --core b_slice,b_join", None),
    ("bios-again", "--config cfg_bios.json --sd {card} --core b_plain,b_zip,b_slice,b_join", "full_bios"),
    ("bios-partial", "--config cfg_bios.json --zips {zips} --sd {card} --core b_slice", "card_bios_partial"),
    ("bios-local", "--config cfg_bios.json --zips {fx}/zips_bios --sd {card} --no-download "
                   "--core b_plain,b_slice,b_join", None),
    ("bios-local-dry-run", "--config cfg_bios.json --zips {fx}/zips_bios --sd {card} --no-download "
                           "--dry-run --core b_slice,b_join", None),
    ("bios-alias-without-bios", "--config cfg_data.json --zips {zips} --sd {card} --core bios", None),
]
CASES += [(f"bad-config-{n}", f"--config bad_{n}.json --zips {{zips}} --sd {{card}}", None)
          for n in ("syntax", "nocores", "missing_key", "twice", "dest", "dest_empty", "nozips",
                    "zipname", "filename", "crc", "size", "databases", "config_url",
                    "config_url_type", "data_type", "data_nourl", "data_url", "data_url_name",
                    "data_name", "data_archives", "data_dup_id", "data_slash_in_core",
                    "data_uf2", "data_uf2_type", "data_uf2_nofamily", "data_uf2_big",
                    "bios_type", "bios_reserved", "bios_reserved_data", "bios_dup_id",
                    "bios_offset", "bios_offset_type", "bios_offset_null", "bios_parts_type",
                    "bios_parts_empty", "bios_parts_name", "bios_parts_item",
                    "bios_offset_parts", "bios_offset_uf2", "bios_parts_uf2", "bios_slash",
                    "sdcard_type", "sdcard_name", "sdcard_url", "sdcard_asset",
                    "sdcard_asset_zip", "sdcard_reserved", "sdcard_reserved_data")]


def rel(scenario, card, args=""):
    return (f"--config cfg_rel_{scenario}.json --zips {{zips}} --sd {{card}} {args}".strip(), card)


# The SD-card archive. Each has an entry in EXPECT.
REL_CASES = {
    "rel-newer": rel("main", "card_rel_v09"),
    "rel-patch": rel("patch", "card_rel_v010"),
    "rel-current": rel("gone", "card_rel_v010"),
    "rel-current-explicit": rel("gone", "card_rel_v010", "--core sdcard"),
    "rel-nomarker": rel("main", "card_rel_nomarker", "--core sdcard"),
    "rel-crlf": rel("main", "card_rel_crlf"),
    "rel-garbage": rel("main", "card_rel_garbage"),
    "rel-empty-marker": rel("main", "card_rel_empty_marker"),
    "rel-future": rel("gone", "card_rel_future"),
    "rel-dry-run": rel("main", "card_rel_v09", "--dry-run"),
    "rel-dry-kept": rel("kept", "card_rel_v09", "--dry-run"),
    "rel-core-other": rel("main", "card_rel_v09", "--core alpha"),
    "rel-no-download": rel("main", "card_rel_v09", "--no-download"),
    "rel-explicit-no-download": rel("main", "card_rel_v09", "--core sdcard --no-download"),
    "rel-noemu": rel("check404", None),
    "rel-noemu-explicit": rel("main", None, "--core sdcard"),
    "rel-noemu-dry": rel("main", None, "--core sdcard --dry-run"),
    "rel-warn-newer": rel("main", "card_rel_v09"),
    "rel-warn-nomarker": rel("main", "card_rel_nomarker", "--core sdcard"),
    "rel-warn-dry": rel("main", "card_rel_v09", "--dry-run"),
    "rel-warn-empty": rel("main", None, "--core sdcard"),
    "rel-warn-current": rel("gone", "card_rel_v010", "--core sdcard"),
    "rel-warn-other": rel("main", "card_rel_v09", "--core alpha"),
    "rel-warn-no-download": rel("main", "card_rel_v09", "--no-download"),
    "rel-temp-folder": ("--config cfg_rel_main.json --sd {card} --core SDCARD", None),
    "rel-with-set": rel("main", None, "--core alpha,sdcard"),
    "rel-check-404": rel("check404", "card_rel_v09"),
    "rel-check-broken": rel("broken", "card_rel_v09"),
    "rel-check-notlist": rel("notlist", "card_rel_v09"),
    "rel-check-noasset": rel("noasset", "card_rel_v09"),
    "rel-check-explicit": rel("check404", "card_rel_v09", "--core sdcard"),
    "rel-filter": rel("filter", "card_rel_v08"),
    "rel-fwonly": rel("fwonly", "card_rel_v010"),
    "rel-slip": rel("slip", "card_rel_v09"),
    "rel-abs": rel("abs", "card_rel_v09"),
    "rel-backslash": rel("back", "card_rel_v09"),
    "rel-drive": rel("drive", "card_rel_v09"),
    "rel-nonascii": rel("ascii", "card_rel_v09"),
    "rel-dots": rel("dots", "card_rel_v09"),
    "rel-badsize": rel("badsize", "card_rel_v09"),
    "rel-notzip": rel("notzip", "card_rel_v09"),
    "rel-assetgone": rel("assetgone", "card_rel_v09"),
    "rel-damaged": rel("damaged", "card_rel_v09"),
    "rel-kept": rel("kept", "card_rel_v09"),
    "rel-kept-stale": rel("stale", "card_rel_v09"),
    "rel-bak-exists": rel("main", "card_rel_bak"),
    "rel-bak-same": rel("main", "card_rel_bak_same"),
    "rel-keep-json": rel("main", "card_rel_json_own"),
    "rel-keep-json-bad": rel("main", "card_rel_json_bad"),
    "rel-replace-json": rel("main", "card_rel_json_online"),
    "rel-fetch": ("--config cfg_rel_fetch.json --zips {zips} --sd {card}", "card_rel_v09"),
    "rel-unconfigured": ("--config cfg_local.json --zips {zips} --sd {card} --core sdcard", None),
    # The key of the Windows program must not change what anything prints.
    "rel-program-key": rel("prog_key", "card_rel_v09"),
    "rel-program-bad": rel("prog_badname", "card_rel_v09"),
}
CASES += [(name, args, card) for name, (args, card) in REL_CASES.items()]

NOTICE = "New archive:  pico-bootLoader_sdcard.zip v0.10 is available (card: {card}): {url}/rel/tag/v0.10"
FLASH = "Firmware:     flash the board with the bootloader of v0.10 as well; " \
        "updateAll cannot do that: {url}/rel/tag/v0.10"
ANY_NOTICE, ANY_FLASH, ANY_SET = "New archive:", "Firmware:", "SD card files:"
REPLACES = "Warning:      files on the card that the archive also holds are replaced; edited lists in /emu are saved as .bak"
MARKER = "card/emu/release.txt"
LIST = "card/emu/emulators.txt"
NEW_LIST = b"piconesPlus;nes;NES\npicoNew;new;New\n"
UNCHANGED = {MARKER: b"v0.9\n", LIST: b"edited list\n", "card/emu/8/new.uf2": None,
             "card/emu/emulators.txt.bak": None, "card/boot.txt": b"GUI=1\n",
             "card/emu/8/own.uf2": b"the user's own"}
INSTALLED = {MARKER: b"v0.10\n", LIST: NEW_LIST, "card/emu/emulators.txt.bak": b"edited list\n",
             "card/boot.txt": b"GUI=1\n", "card/emu/8/own.uf2": b"the user's own",
             "card/emu/versions.txt.bak": None, "card/updateAll/updateAll.py": b"# new\n"}
UNSAFE = {"has": ["holds an unsafe file name, so nothing was installed from it"],
          "not": [ANY_FLASH], "rc": 1, "files": UNCHANGED}

# What the output and the card must show, checked on the script's run: agreeing
# with ua_host does not prove that the message appears, or the file is kept.
# "rc" is the exit status, "has" and "not" are texts, and "files" gives the
# contents of files below the run's folder, None for one that must not be there.
EXPECT = {
    "rel-newer": {"rc": 0, "has": [NOTICE.format(card="v0.9", url="{url}"), FLASH,
                                   "saved emu/emulators.txt as emu/emulators.txt.bak",
                                   "downloaded pico-bootLoader_sdcard-v0.10.zip",
                                   "SD card files: pico-bootLoader_sdcard-v0.10.zip -> /",
                                   "5 written, 1 up to date"],
                  "files": dict(INSTALLED, **{"card/updateAll/updateAll.json":
                                              b'{"config_url": "http://x/new.json", "cores": []}\n'})},
    "rel-patch": {"rc": 0, "has": ["v0.10.1 is available (card: v0.10)"], "not": [ANY_FLASH],
                  "files": {MARKER: b"v0.10.1\n"}},
    "rel-current": {"rc": 0, "has": ["SD card files: /\n  v0.10 up to date"],
                    "not": [ANY_NOTICE, ANY_FLASH, "ERROR", "downloaded pico"],
                    "files": {MARKER: b"v0.10\n", LIST: b"edited list\n"}},
    "rel-current-explicit": {"rc": 0, "has": ["  v0.10 up to date", "1 complete"],
                             "not": [ANY_NOTICE, "Alpha:"]},
    "rel-nomarker": {"rc": 0, "has": [NOTICE.format(card="unknown", url="{url}"), FLASH,
                                      "6 up to date"],
                     "not": ["written", "saved emu"],
                     "files": {MARKER: b"v0.10\n", "card/emu/emulators.txt.bak": None}},
    "rel-crlf": {"rc": 0, "has": ["(card: v0.9)"], "files": {MARKER: b"v0.10\n"}},
    "rel-garbage": {"rc": 0, "has": ["(card: unknown)", FLASH], "files": {MARKER: b"v0.10\n"}},
    "rel-empty-marker": {"rc": 0, "has": ["(card: unknown)"], "files": {MARKER: b"v0.10\n"}},
    "rel-future": {"rc": 0, "has": ["  v0.99 up to date"], "not": [ANY_NOTICE, "ERROR"],
                   "files": {MARKER: b"v0.99\n"}},
    "rel-dry-run": {"rc": 0, "has": [ANY_NOTICE, FLASH, "Dry run:",
                                     "would download pico-bootLoader_sdcard-v0.10.zip from 127.0.0.1",
                                     "1 to download"],
                    "files": UNCHANGED},
    "rel-dry-kept": {"rc": 0, "has": [ANY_NOTICE, FLASH, "5 to write, 1 up to date",
                                      "would save emu/emulators.txt as emu/emulators.txt.bak"],
                     "not": ["would download kept"], "files": UNCHANGED},
    "rel-core-other": {"rc": 0, "has": [NOTICE.format(card="v0.9", url="{url}")],
                       "not": [ANY_SET, ANY_FLASH], "files": UNCHANGED},
    "rel-no-download": {"rc": 0, "has": [NOTICE.format(card="v0.9", url="{url}")],
                        "not": [ANY_SET, ANY_FLASH], "files": UNCHANGED},
    "rel-explicit-no-download": {"rc": 1, "has": [ANY_NOTICE, "SD card files: skipped, "
                                                  "pico-bootLoader_sdcard-v0.10.zip not found"],
                                 "not": [ANY_FLASH], "files": UNCHANGED},
    "rel-noemu": {"rc": 0, "not": [ANY_NOTICE, ANY_SET, ANY_FLASH, "could not check"],
                  "files": {MARKER: None}},
    "rel-noemu-dry": {"rc": 0, "has": ["SD card files: pico", "Dry run:"], "not": [ANY_NOTICE, ANY_FLASH]},
    # The warning that files are overwritten: only where that will happen.
    "rel-warn-newer": {"rc": 0, "has": [REPLACES]},
    "rel-warn-nomarker": {"rc": 0, "has": [REPLACES]},
    "rel-warn-dry": {"rc": 0, "not": [REPLACES]},
    "rel-warn-empty": {"rc": 0, "not": [REPLACES]},
    "rel-warn-current": {"rc": 0, "not": [REPLACES]},
    "rel-warn-other": {"rc": 0, "not": [REPLACES]},
    "rel-warn-no-download": {"rc": 0, "not": [REPLACES]},
    "rel-noemu-explicit": {"rc": 0, "has": ["SD card files: pico"], "not": [ANY_NOTICE, ANY_FLASH],
                           "files": {MARKER: b"v0.10\n", LIST: NEW_LIST,
                                     "card/emu/emulators.txt.bak": None}},
    "rel-temp-folder": {"rc": 0, "has": ["Zips from:    downloads only", "1 complete"],
                        "files": {MARKER: b"v0.10\n"}},
    "rel-with-set": {"rc": 0, "has": ["SD card files: pico", "Alpha: "], "not": [ANY_NOTICE]},
    "rel-check-404": {"rc": 0, "has": ["New archive:  could not check: HTTP 404"],
                      "not": ["is available", ANY_SET], "files": UNCHANGED},
    "rel-check-broken": {"rc": 0, "has": ["could not check: the answer is not a list of releases"],
                         "files": UNCHANGED},
    "rel-check-notlist": {"rc": 0, "has": ["could not check: the answer is not a list of releases"],
                          "files": UNCHANGED},
    "rel-check-noasset": {"rc": 0, "has": ["could not check: no release holds "
                                           "pico-bootLoader_sdcard.zip"], "files": UNCHANGED},
    "rel-check-explicit": {"rc": 1, "has": ["could not check"], "files": UNCHANGED},
    "rel-filter": {"rc": 0, "has": ["sdcard.zip v0.10 is available (card: v0.8)"],
                   "files": {MARKER: b"v0.10\n"}},
    "rel-fwonly": {"rc": 0, "has": ["  v0.10 up to date"], "not": [ANY_NOTICE, ANY_FLASH]},
    "rel-slip": UNSAFE, "rel-abs": UNSAFE, "rel-backslash": UNSAFE, "rel-drive": UNSAFE,
    "rel-nonascii": UNSAFE, "rel-dots": UNSAFE,
    "rel-badsize": {"rc": 1, "has": ["ERROR: download of pico-bootLoader_sdcard-v0.10.zip from "
                                     "127.0.0.1 failed: received"], "not": [ANY_FLASH],
                    "files": dict(UNCHANGED, **{"zips/pico-bootLoader_sdcard-v0.10.zip": None})},
    "rel-notzip": {"rc": 1, "has": ["the file received is not a zip"], "not": [ANY_FLASH],
                   "files": dict(UNCHANGED, **{"zips/pico-bootLoader_sdcard-v0.10.zip": None})},
    "rel-assetgone": {"rc": 1, "has": ["failed: HTTP 404"], "not": [ANY_FLASH], "files": UNCHANGED},
    "rel-damaged": {"rc": 1, "has": ["Bad CRC-32"], "not": [ANY_FLASH],
                    "files": {MARKER: b"v0.9\n", LIST: NEW_LIST, "card/emu/8/new.uf2": None}},
    "rel-kept": {"rc": 0, "has": ["kept_sdcard-v0.10.zip -> /"], "not": ["downloaded kept"],
                 "files": INSTALLED},
    "rel-kept-stale": {"rc": 0, "has": ["downloaded stale_sdcard-v0.10.zip"],
                       "files": {MARKER: b"v0.10\n", "card/emu/8/old.uf2": None}},
    "rel-bak-exists": {"rc": 0, "has": ["saved emu/emulators.txt as emu/emulators.txt.bak2"],
                       "files": {"card/emu/emulators.txt.bak": b"edits saved earlier\n",
                                 "card/emu/emulators.txt.bak2": b"edited list\n", LIST: NEW_LIST}},
    "rel-bak-same": {"rc": 0, "not": ["saved emu"],
                     "files": {"card/emu/emulators.txt.bak": b"edited list\n",
                               "card/emu/emulators.txt.bak2": None, LIST: NEW_LIST}},
    "rel-keep-json": {"rc": 0, "not": ["saved updateAll"],
                      "files": {"card/updateAll/updateAll.json": b'{"databases": [], "cores": []}\n',
                                "card/updateAll/updateAll.py": b"# new\n", MARKER: b"v0.10\n"}},
    "rel-keep-json-bad": {"rc": 0, "files": {"card/updateAll/updateAll.json": b'{"edited": \n',
                                             MARKER: b"v0.10\n"}},
    "rel-replace-json": {"rc": 0, "files": {"card/updateAll/updateAll.json":
                                            b'{"config_url": "http://x/new.json", "cores": []}\n'}},
    "rel-fetch": {"rc": 0, "has": ["Sets from:", ANY_NOTICE, "SD card files: pico"],
                  "files": {MARKER: b"v0.10\n"}},
    "rel-unconfigured": {"rc": 1, "has": ["ERROR: unknown core: sdcard."]},
    "rel-program-key": {"rc": 0, "has": [NOTICE.format(card="v0.9", url="{url}")],
                        "not": ["updateAll-"], "files": {MARKER: b"v0.10\n"}},
    "rel-program-bad": {"rc": 0, "has": [NOTICE.format(card="v0.9", url="{url}")],
                        "files": {MARKER: b"v0.10\n"}},
}

# ua_host only: how updateAll.exe compares its version with the newest one found.
VERSION_CASES = [
    ("v0.9", "v0.10", "older"), ("v0.10", "v0.9", "not older"), ("v0.9", "v0.9", "not older"),
    ("v0.9", "v0.9.1", "older"), ("v0.9.1", "v0.9", "not older"), ("v0.9.1", "v0.9.2", "older"),
    ("v0.9.0", "v0.9", "not older"), ("v1.0", "v1.1", "older"), ("v1.9", "v2.0", "older"),
    ("v1.10", "v1.9", "not older"),
    # Only the two forms of a version count, as anything else cannot be compared.
    ("dev", "v0.10", "not older"), ("d8a6932", "v0.10", "not older"),
    ("d8a6932-dirty", "v0.10", "not older"), ("v0.8-7-gd8a6932-dirty", "v0.10", "not older"),
    ("v0.9-3-gabc1234", "v0.10", "not older"), ("v0.9-dirty", "v0.10", "not older"),
    ("v0.9-alpha", "v0.10", "not older"), ("v1", "v0.10", "not older"), ("", "v0.10", "not older"),
    ("v0.9", "v0.10-alpha", "not older"), ("v0.9", "garbage", "not older"), ("v0.9", "", "not older"),
]


def prog(scenario, to=True):
    return (f"--program --config cfg_rel_{scenario}.json" + (" --to {run}/new.exe" if to else ""))


FAKE_EXE = "new.exe"
NOTHING_LEFT = {FAKE_EXE: None, FAKE_EXE + ".part": None}

# ua_host only: (name, arguments, what it must show). "same" names a fixture that
# the downloaded file must equal, and "under" a time limit in seconds.
PROGRAM_CASES = [
    ("prog-older", prog("prog_older", False), {"rc": 0, "has": ["archive: v0.10", "program: v0.9 "]}),
    ("prog-alone", prog("prog_alone", False), {"rc": 0, "has": [
        "archive: no release holds pico-bootLoader_sdcard.zip", "program: v0.10 "]}),
    ("prog-filter", prog("prog_filter", False), {"rc": 0, "has": ["program: v0.12 "]}),
    ("prog-none", prog("prog_none", False), {"rc": 0, "has": ["archive: v0.10", "program: none"]}),
    ("prog-nokey", prog("prog_nokey", False), {"rc": 0, "has": ["program: none"]}),
    ("prog-badname", prog("prog_badname", False), {"rc": 0, "has": ["archive: v0.10", "program: none"]}),
    ("prog-notexe", prog("prog_notexe", False), {"rc": 0, "has": ["program: none"]}),
    ("prog-nostar", prog("prog_nostar", False), {"rc": 0, "has": ["program: none"]}),
    ("prog-twostars", prog("prog_twostars", False), {"rc": 0, "has": ["program: none"]}),
    ("prog-separate", prog("prog_separate", False), {"rc": 0, "has": ["archive: v0.10", "program: v1.5 "]}),
    ("prog-highest", prog("prog_highest", False), {"rc": 0, "has": ["program: v1.2 "]}),
    ("prog-oldname", prog("prog_oldname", False), {"rc": 0, "has": ["program: none"]}),
    ("prog-badversion", prog("prog_badversion", False), {"rc": 0, "has": ["program: none"]}),
    ("prog-badtype", prog("prog_badtype", False), {"rc": 0, "has": ["program: none"]}),
    ("prog-list-404", prog("prog_404", False), {"rc": 0, "has": ["archive: HTTP 404", "program: none"]}),
    ("prog-no-sdcard", "--program --config cfg_local.json", {"rc": 0, "has": [
        "archive: not configured", "program: none"]}),
    ("prog-download", prog("prog_ok"), {"rc": 0, "has": ["downloaded"], "same": "fake_exe_v0.10.bin",
                                        "files": {FAKE_EXE + ".part": None}}),
    ("prog-separate-download", prog("prog_sep_ok"), {"rc": 0, "has": ["downloaded"], "same": "fake_exe_v1.5.bin",
                                                     "files": {FAKE_EXE + ".part": None}}),
    ("prog-separate-title", prog("prog_sep_title"), {"rc": 1, "has": ["failed: the file received is not updateAll v1.5"],
                                                     "files": NOTHING_LEFT}),
    ("prog-size", prog("prog_size"), {"rc": 1, "has": ["failed: received", "the release says"],
                                      "files": NOTHING_LEFT}),
    ("prog-mz", prog("prog_mz"), {"rc": 1, "has": ["failed: the file received is not a program"],
                                  "files": NOTHING_LEFT}),
    ("prog-title", prog("prog_title"), {"rc": 1, "has": ["failed: the file received is not updateAll v0.10"],
                                        "files": NOTHING_LEFT}),
    ("prog-longer-tag", prog("prog_longer"), {"rc": 1, "has": ["is not updateAll v0.10"], "files": NOTHING_LEFT}),
    ("prog-gone", prog("prog_gone"), {"rc": 1, "has": ["failed: HTTP 404"], "files": NOTHING_LEFT, "under": 1.5}),
]

# ua_host only, with what the row of updateAll.exe passes: (name, UA_SDCARD,
# arguments, card, EXPECT entry).
ROW_CASES = [
    ("row-tick-empty-card", "1", "--config cfg_rel_main.json --zips {zips} --sd {card}", None,
     {"rc": 0, "has": ["SD card files: pico", "Alpha: "], "not": [ANY_NOTICE, ANY_FLASH, REPLACES],
      "files": {MARKER: b"v0.10\n"}}),
    ("row-untick", "-1", "--config cfg_rel_main.json --zips {zips} --sd {card}", "card_rel_v09",
     {"rc": 0, "has": [ANY_NOTICE, "Alpha: "], "not": [ANY_SET, ANY_FLASH, REPLACES], "files": UNCHANGED}),
    ("row-only", "1", "--config cfg_rel_main.json --zips {zips} --sd {card} --core sdcard", "card_rel_v09",
     {"rc": 0, "has": [ANY_NOTICE, "SD card files: pico", REPLACES], "not": ["Alpha: "],
      "files": {MARKER: b"v0.10\n"}}),
]

# updateAll.py only, as only the scripts ask: (name, arguments, card, what is typed
# in, what it must show). Nothing is installed unless it is chosen.
ASK = "--config cfg_ask.json --zips {zips} --sd {card}"
Q_BIOS, Q_SETS, Q_DATA = ("Download and install the BIOS files of the emulators? [y/N]",
                          "Download and install the arcade ROM sets? [y/N]",
                          "Download and install the shareware and demo data of the ported games? [y/N]")
S_BIOS, S_SETS, S_DATA, S_NONE = "Test BIOS: ", "Alpha: ", "Rho data: ", "Nothing to install"
RIGHTS = "The ROM sets and BIOS files are copyright of their manufacturers and are not part of this project. Holding the rights to them is your responsibility."
ASK_CASES = [
    ("ask-nobody", ASK, None, "", {"rc": 0, "has": [RIGHTS, Q_BIOS, Q_SETS, Q_DATA, S_NONE],
                                   "not": [S_BIOS, S_SETS, S_DATA]}),
    ("ask-all-no", ASK, None, "n\nno\n\n", {"rc": 0, "has": [S_NONE], "not": [S_BIOS, S_SETS, S_DATA]}),
    ("ask-bios", ASK, None, "y\nn\nn\n", {"rc": 0, "has": [S_BIOS], "not": [S_SETS, S_DATA, S_NONE]}),
    ("ask-sets", ASK, None, "n\ny\nn\n", {"rc": 0, "has": [S_SETS], "not": [S_BIOS, S_DATA, S_NONE]}),
    ("ask-data", ASK, None, "n\nn\nyes\n", {"rc": 0, "has": [S_DATA], "not": [S_BIOS, S_SETS, S_NONE]}),
    ("ask-all-yes", ASK, None, "y\nYES\n Y \n", {"rc": 0, "has": [S_BIOS, S_SETS, S_DATA], "not": [S_NONE]}),
    ("ask-other-answer", ASK, None, "ja\nmaybe\n1\n", {"rc": 0, "has": [S_NONE], "not": [S_BIOS, S_SETS, S_DATA]}),
    ("ask-no-download", ASK + " --no-download", None, "n\nn\nn\n",
     {"rc": 0, "has": ["Install the arcade ROM sets? [y/N]"], "not": ["Download and install"]}),
    ("ask-core", ASK + " --core alpha", None, "", {"rc": 0, "has": [S_SETS], "not": ["[y/N]", S_BIOS, S_DATA, RIGHTS]}),
    ("ask-all-flag", ASK + " --all", None, "", {"rc": 0, "has": [S_BIOS, S_SETS, S_DATA], "not": ["[y/N]", RIGHTS]}),
    # The archive does not depend on the answers, on a card that holds one.
    ("ask-archive", "--config cfg_rel_main.json --zips {zips} --sd {card}", "card_rel_v09", "",
     {"rc": 0, "has": [Q_SETS, "SD card files: pico", REPLACES], "not": [S_SETS, S_NONE],
      "files": {MARKER: b"v0.10\n"}}),
    ("ask-empty-card", "--config cfg_rel_main.json --zips {zips} --sd {card}", None, "",
     {"rc": 0, "has": [S_NONE], "not": ["SD card files:", S_SETS], "files": {MARKER: None}}),
]

# ua_host only, stopped after this many calls of on_bytes: (name, arguments, calls,
# the state the file it stopped on went through). Python has no way to cancel.
CANCEL_CASES = [
    ("cancel-download", "--config cfg_db.json --zips {zips} --sd {card} --core theta", 1, "Downloading"),
    ("cancel-install", "--config cfg_local.json --zips {zips} --sd {card} --core alpha", 1, "Installing"),
    ("cancel-rel-download", "--config cfg_rel_main.json --zips {zips} --sd {card} --core sdcard", 1, "Downloading"),
    ("cancel-rel-install", "--config cfg_rel_kept.json --zips {zips} --sd {card} --core sdcard", 1, "Installing"),
]
# The Windows program's download of itself, which has no report of sets: the same
# call count, and nothing may be left of the file.
PROGRAM_CANCEL_CASE = ("cancel-program", "--program --config cfg_rel_prog_ok.json --to {run}/new.exe", 1)
# Run without the hooks as well; they must not change anything.
NO_HOOK_CASES = ("local", "db-http", "data-uf2-local", "bios-http", "rel-newer")

# The states of ua_core.h.
DOWNLOADING, BUSY, DONE, NOTE, FAILED, CANCELLED = range(6)
REPORT_HEADERS = ("Sets from:", "Zips from:", "SD card:", "New archive:", "Databases:", "Dry run:",
                  "Firmware:", "ERROR:")

# Error texts that only the platform decides: a JSON parser's own wording.
NORMALISE = [
    (re.compile(r"[A-Z][^:\n]*: line \d+ column \d+ \(char \d+\)"), "<json error>"),
    (re.compile(r"invalid JSON at line \d+ column \d+"), "<json error>"),
]


def normalise(text):
    for rx, repl in NORMALISE:
        text = rx.sub(repl, text)
    return text


def tree(root):
    """relative path -> MD5 of every file below root."""
    out = {}
    for dirpath, _, files in os.walk(root):
        for name in files:
            path = os.path.join(dirpath, name)
            with open(path, "rb") as f:
                out[os.path.relpath(path, root)] = hashlib.md5(f.read()).hexdigest()
    return out


def read_rows(path):
    """The rows a list of files would show: a file belongs to the set being processed."""
    rows, by_key, set_number = [], {}, -1
    if not os.path.exists(path):
        return rows
    with open(path, encoding="utf-8") as f:
        for line in f:
            kind, *rest = line.rstrip("\n").split("\t")
            if kind == "set":
                set_number = int(rest[0])
                continue
            state, set_name, file, detail = rest
            key = (set_number, file.lower())
            if key not in by_key:
                by_key[key] = {"set": set_name, "file": file, "states": [], "details": []}
                rows.append(by_key[key])
            by_key[key]["states"].append(int(state))
            by_key[key]["details"].append(detail)
    return rows


def unfinished(rows):
    """Rows that do not end in a final state, or go on after one."""
    problems = []
    for r in rows:
        if r["states"][-1] in (DOWNLOADING, BUSY):
            problems.append(f"{r['set']}/{r['file']} is left at {r['details'][-1]!r}")
        if any(s not in (DOWNLOADING, BUSY) for s in r["states"][:-1]):
            problems.append(f"{r['set']}/{r['file']} goes on after a final state: {r['details']}")
    return problems


def report(out):
    """The sets in a printed report: (name, rest of its first line, its indented lines)."""
    sets = []
    for line in out.splitlines():
        if line.startswith("  ") and sets:
            sets[-1][2].append(line)
        elif not line.startswith(REPORT_HEADERS) and ": " in line:
            name, rest = line.split(": ", 1)
            sets.append((name, rest, []))
    return sets


def check_rows(rows, out):
    """What the rows say against the report that updateAll.py printed."""
    problems = unfinished(rows)
    sets = report(out)
    for name, rest, lines in sets:
        # Set names can repeat, so a row belongs to the set whose report names its file.
        named = [r for r in rows if r["set"] == name]
        if rest.startswith("skipped,"):
            mine = [r for r in named if r["file"] in rest]
            if [r["states"][-1] for r in mine] != [NOTE]:
                problems.append(f"{name}: skipped, but its rows are {mine}")
        elif " -> " in rest:
            files = rest.split(" -> ")[0].lower().split(", ")
            mine = [r for r in named if r["file"].lower() in files]
            if not mine:
                problems.append(f"{name}: has work to report, but no row")
        else:
            mine = []
            if named and sum(s[0] == name for s in sets) == 1:
                problems.append(f"{name}: complete on the card, but has rows: {named}")
        last = [r["states"][-1] for r in mine]
        wrong = any(l.startswith(("  ERROR:", "  INCOMPLETE:")) for l in lines)
        if wrong != (FAILED in last):
            problems.append(f"{name}: report says {lines}, but its rows end as {last}")
    bad_dbs = out.count("ERROR: database ")
    bad_rows = sum(r["set"] == "Database" and r["states"][-1] == FAILED for r in rows)
    if bad_dbs != bad_rows:
        problems.append(f"{bad_dbs} database errors in the report, {bad_rows} failed rows")
    return problems


def check_expected(want, res, url):
    """What the run must have shown and left behind, against EXPECT."""
    problems = []
    text = res["out"] + res["err"]
    if "rc" in want and res["rc"] != want["rc"]:
        problems.append(f"exit {res['rc']}, expected {want['rc']}")
    for s in want.get("has", ()):
        if s.replace("{url}", url) not in text:
            problems.append(f"does not say {s!r}")
    for s in want.get("not", ()):
        if s.replace("{url}", url) in text:
            problems.append(f"says {s!r}")
    for path, data in want.get("files", {}).items():
        expected = None if data is None else hashlib.md5(data).hexdigest()
        if res["tree"].get(path) != expected:
            problems.append(f"{path} is not as expected: {data!r}")
    return problems


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", help="run only the cases whose name contains this")
    ap.add_argument("-v", action="store_true", help="show the output of every case")
    args = ap.parse_args()

    work = tempfile.mkdtemp(prefix="ua-compare-")
    fx = os.path.join(work, "fx")
    run_dir = os.path.join(work, "run")
    port = free_port()
    url = f"http://127.0.0.1:{port}"
    subprocess.run([sys.executable, os.path.join(HERE, "make_fixtures.py"), fx, str(port)], check=True)

    env = {k: v for k, v in os.environ.items() if "proxy" not in k.lower()}
    env["no_proxy"] = env["NO_PROXY"] = "*"
    env["ASAN_OPTIONS"] = "detect_leaks=1"
    server = subprocess.Popen([sys.executable, "-m", "http.server", str(port), "--bind", "127.0.0.1",
                               "--directory", os.path.join(fx, "srv")],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.1)

        # The card a complete local run leaves, for "local-again".
        full = os.path.join(fx, "full")
        os.makedirs(full)
        subprocess.run([sys.executable, SCRIPT, "--config", "cfg_local.json", "--zips",
                        os.path.join(fx, "zips"), "--sd", full, "--all"], cwd=fx, env=env,
                       stdout=subprocess.DEVNULL)
        # And the card a complete game-data run leaves, for "data-again".
        full_data = os.path.join(fx, "full_data")
        os.makedirs(full_data)
        subprocess.run([sys.executable, SCRIPT, "--config", "cfg_data.json", "--core",
                        "rho,sigma,tau,omega", "--sd", full_data], cwd=fx, env=env,
                       stdout=subprocess.DEVNULL)
        # And the card a BIOS run leaves, for "bios-again".
        full_bios = os.path.join(fx, "full_bios")
        os.makedirs(full_bios)
        subprocess.run([sys.executable, SCRIPT, "--config", "cfg_bios.json", "--core",
                        "b_plain,b_zip,b_slice,b_join", "--sd", full_bios], cwd=fx, env=env,
                       stdout=subprocess.DEVNULL)

        events = os.path.join(work, "events.txt")

        def run_case(tool, argline, card, extra_env=None, answers=None):
            if os.path.exists(events):
                os.remove(events)
            shutil.rmtree(run_dir, ignore_errors=True)
            os.makedirs(run_dir)
            shutil.copytree(os.path.join(fx, "zips"), os.path.join(run_dir, "zips"))
            if card:
                shutil.copytree(os.path.join(fx, card), os.path.join(run_dir, "card"))
            else:
                os.makedirs(os.path.join(run_dir, "card"))
            argv = argline.format(zips=os.path.join(run_dir, "zips"),
                                  card=os.path.join(run_dir, "card"),
                                  run=run_dir, fx=fx, url=url).split()
            # What these cases are about comes after the scripts' questions.
            if answers is None and "--core" not in argv:
                argv.append("--all")
            start = time.time()
            p = subprocess.run(tool + argv, cwd=fx, env=dict(env, **(extra_env or {})),
                               capture_output=True, text=True, input=answers or "")
            return {"rc": p.returncode, "out": normalise(p.stdout),
                    "err": normalise(p.stderr), "tree": tree(run_dir),
                    "time": time.time() - start}

        failures = 0
        total = 0
        cases = [c for c in CASES if not args.k or args.k in c[0]]
        for name, argline, card in cases:
            total += 1
            py = run_case([sys.executable, SCRIPT], argline, card)
            c = run_case([HOST], argline, card, {"UA_EVENTS": events})
            rows = read_rows(events)
            diffs = [f"rows: {p}" for p in check_rows(rows, py["out"])]
            if name in REL_CASES:
                diffs += [f"expected: {p}" for p in check_expected(EXPECT[name], py, url)]
            for key in ("rc", "out", "err", "tree"):
                if py[key] != c[key]:
                    if isinstance(py[key], str):
                        diffs.append(f"{key}:\n" + "".join(difflib.unified_diff(
                            py[key].splitlines(True), c[key].splitlines(True),
                            "updateAll.py", "ua_host")))
                    else:
                        diffs.append(f"{key}: updateAll.py {py[key]!r}\n{' ' * len(key)}  ua_host      {c[key]!r}")
            status = "FAIL" if diffs else "ok"
            print(f"{status:4} {name:22} exit {py['rc']}  ({py['time']:.1f} s / {c['time']:.1f} s)")
            if diffs:
                failures += 1
                for d in diffs:
                    print("     " + d.replace("\n", "\n     "))
            if args.v or diffs:
                print("     --- updateAll.py stdout/stderr ---")
                print("     " + (py["out"] + py["err"]).replace("\n", "\n     "))
                print("     --- rows ---")
                for r in rows:
                    print(f"     {r['set']}/{r['file']}: {r['details']} {r['states']}")

        for name, argline, card, answers, want in ASK_CASES:
            if args.k and args.k not in name:
                continue
            total += 1
            py = run_case([sys.executable, SCRIPT], argline, card, answers=answers)
            problems = check_expected(want, py, url)
            failures += bool(problems)
            print(f"{'FAIL' if problems else 'ok':4} {name:22} exit {py['rc']}")
            for p in problems:
                print("     " + p)
            if args.v or problems:
                print("     " + (py["out"] + py["err"]).replace("\n", "\n     "))

        for name, argline, calls, doing in CANCEL_CASES:
            if args.k and args.k not in name:
                continue
            total += 1
            c = run_case([HOST], argline, None, {"UA_EVENTS": events, "UA_CANCEL_AFTER": str(calls)})
            rows = read_rows(events)
            problems = unfinished(rows)
            if c["rc"] != 1 or not c["out"].endswith("Cancelled\n") or c["err"]:
                problems.append(f"exit {c['rc']}, stdout {c['out']!r}, stderr {c['err']!r}")
            if not any(r["states"][-1] == CANCELLED and doing in r["details"] for r in rows):
                problems.append("no row was cancelled while " + doing)
            if MARKER in c["tree"]:
                problems.append("a cancelled run wrote the marker")
            failures += bool(problems)
            print(f"{'FAIL' if problems else 'ok':4} {name:22} exit {c['rc']}")
            for p in problems:
                print("     " + p)
            if args.v or problems:
                for r in rows:
                    print(f"     {r['set']}/{r['file']}: {r['details']} {r['states']}")

        if not args.k or args.k in "version-older":
            total += 1
            wrong = []
            for running, tag, expected in VERSION_CASES:
                p = subprocess.run([HOST, "--version-older", running, tag], capture_output=True, text=True)
                if p.stdout.strip() != expected:
                    wrong.append(f"{running!r} against {tag!r}: {p.stdout.strip()!r}, expected {expected!r}")
            failures += bool(wrong)
            print(f"{'FAIL' if wrong else 'ok':4} {'version-older':22} {len(VERSION_CASES)} comparisons")
            for w in wrong:
                print("     " + w)

        for name, argline, want in PROGRAM_CASES:
            if args.k and args.k not in name:
                continue
            total += 1
            c = run_case([HOST], argline, None, {"UA_EVENTS": events})
            problems = check_expected(want, c, url) + unfinished(read_rows(events))
            if "same" in want:
                with open(os.path.join(fx, want["same"]), "rb") as f:
                    if c["tree"].get(FAKE_EXE) != hashlib.md5(f.read()).hexdigest():
                        problems.append(f"{FAKE_EXE} is not {want['same']}")
            if "under" in want and c["time"] >= want["under"]:
                problems.append(f"took {c['time']:.1f} s, so it was retried")
            failures += bool(problems)
            print(f"{'FAIL' if problems else 'ok':4} {name:22} exit {c['rc']}")
            for p in problems:
                print("     " + p)
            if args.v or problems:
                print("     " + (c["out"] + c["err"]).replace("\n", "\n     "))

        name, argline, calls = PROGRAM_CANCEL_CASE
        if not args.k or args.k in name:
            total += 1
            c = run_case([HOST], argline, None, {"UA_EVENTS": events, "UA_CANCEL_AFTER": str(calls)})
            rows = read_rows(events)
            problems = unfinished(rows) + check_expected(
                {"rc": 1, "has": ["failed: cancelled"], "files": NOTHING_LEFT}, c, url)
            if not any(r["states"][-1] == CANCELLED for r in rows):
                problems.append("no row was cancelled")
            failures += bool(problems)
            print(f"{'FAIL' if problems else 'ok':4} {name:22} exit {c['rc']}")
            for p in problems:
                print("     " + p)

        for name, value, argline, card, want in ROW_CASES:
            if args.k and args.k not in name:
                continue
            total += 1
            c = run_case([HOST], argline, card, {"UA_EVENTS": events, "UA_SDCARD": value})
            problems = check_expected(want, c, url) + unfinished(read_rows(events))
            failures += bool(problems)
            print(f"{'FAIL' if problems else 'ok':4} {name:22} exit {c['rc']}")
            for p in problems:
                print("     " + p)
            if args.v or problems:
                print("     " + (c["out"] + c["err"]).replace("\n", "\n     "))

        for name, argline, card in CASES:
            if name not in NO_HOOK_CASES or (args.k and args.k not in name):
                continue
            total += 1
            with_hooks = run_case([HOST], argline, card, {"UA_EVENTS": events})
            without = run_case([HOST], argline, card)
            same = all(with_hooks[k] == without[k] for k in ("rc", "out", "err", "tree"))
            failures += not same
            print(f"{'ok' if same else 'FAIL':4} {name + ' no hooks':22} exit {without['rc']}")

        print(f"\n{total - failures} of {total} cases agree")
        return 1 if failures else 0
    finally:
        server.terminate()
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
