#!/usr/bin/env python3
"""Make the popular-games lists attract mode favours (emu/attract/<EMU>.txt).

Reads the LaunchBox Games Database (Metadata.zip, a free download refreshed
daily at https://gamesdb.launchbox-app.com/Metadata.zip) and writes, per
emulator, the most popular games of its systems: the games with the most
community ratings, leaving out those rated below --min-rating. The number of
ratings stands in for popularity; the average keeps well-known bad games out.

Each line of a list is one game: the keys its ROM files may match, then '#'
and a comment. A key is a title reduced by title_key() below, which pico_shared
(menu.cpp, attractTitleKey) repeats exactly for every file name on the card:
the region and version tags go, articles go ("Legend of Zelda, The" and "The
Legend of Zelda" meet), and only letters and digits stay. A game gets a key for
its database name, for each alternate name (the Japanese titles, mostly) and
for each ROM file name the database's Files.xml links to it.

Usage:
  tools/make_attract_lists.py [--metadata Metadata.zip] [--out emu/attract]
                              [--top 100] [--min-ratings 10] [--min-rating 3.5]

Without --metadata the archive is downloaded (about 105 MB).
"""
import argparse
import datetime
import io
import os
import sys
import unicodedata
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from collections import defaultdict

METADATA_URL = "https://gamesdb.launchbox-app.com/Metadata.zip"
MIN_EXTRA_KEY = 5      # shortest key taken from an alternate or file name
MIN_GAMES = 20         # below this, a system's bar is lowered step by step

# pico_shared's emulator type string (the list's file name) -> LaunchBox platforms
EMULATORS = {
    "NES": ["Nintendo Entertainment System"],
    "SNES": ["Super Nintendo Entertainment System"],
    "MD": ["Sega Genesis"],
    "SMS": ["Sega Master System", "Sega Game Gear"],
    "PCE": ["NEC TurboGrafx-16", "NEC TurboGrafx-CD"],
    "GB": ["Nintendo Game Boy", "Nintendo Game Boy Color"],
    "O2E": ["Magnavox Odyssey 2"],
    "TI99": ["Texas Instruments TI 99/4A"],
    "C64": ["Commodore 64"],
}
# A TI-99/4A cartridge kept as C/D/G parts is listed under the file of its first
# part (PARSECC.bin, or PARSECG.bin when it has GROM only; 8 and 9 end multi-bank
# images), so its keys also match with that letter after them.
PART_SUFFIXES = {"TI99": "cg89"}
SYSTEM_NAMES = {
    "NES": "Nintendo Entertainment System", "SNES": "Super Nintendo", "MD": "Sega Genesis / Mega Drive",
    "SMS": "Sega Master System and Game Gear", "PCE": "NEC PC Engine / TurboGrafx-16 and its CD-ROM",
    "GB": "Nintendo Game Boy and Game Boy Color", "O2E": "Philips Videopac / Magnavox Odyssey 2",
    "TI99": "Texas Instruments TI-99/4A", "C64": "Commodore 64",
}


def title_key(name):
    """The matching key of a title. Must stay identical to attractTitleKey() in
    pico_shared/menu.cpp, which works on the file names on the card (ASCII; a
    byte above 127 is dropped there, here accents are folded first, which gives
    the same result for the plain-ASCII names No-Intro uses)."""
    s = unicodedata.normalize("NFKD", name)
    s = "".join(c for c in s if ord(c) < 128)
    # the title: up to the first " (" or " [" (region, version, dump tags)
    for tag in (" (", " ["):
        i = s.find(tag)
        if i >= 0:
            s = s[:i]
    s = s.lower().replace("&", " and ")
    # ", the" / ", an" / ", a" before the end or a non-alphanumeric character
    out = []
    i = 0
    while i < len(s):
        hit = False
        for art in (", the", ", an", ", a"):
            if s.startswith(art, i):
                j = i + len(art)
                if j == len(s) or not s[j].isalnum():
                    i = j
                    hit = True
                    break
        if not hit:
            out.append(s[i])
            i += 1
    s = "".join(out)
    for art in ("the ", "an ", "a "):
        if s.startswith(art):
            s = s[len(art):]
            break
    return "".join(c for c in s if ("a" <= c <= "z") or ("0" <= c <= "9"))


def open_metadata(path):
    if path:
        return zipfile.ZipFile(path)
    print(f"Downloading {METADATA_URL} ...", file=sys.stderr)
    req = urllib.request.Request(METADATA_URL, headers={"User-Agent": "make_attract_lists"})
    with urllib.request.urlopen(req, timeout=600) as r:
        return zipfile.ZipFile(io.BytesIO(r.read()))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--metadata", help="LaunchBox Metadata.zip (downloaded when left out)")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "emu", "attract"))
    ap.add_argument("--top", type=int, default=100, help="games per system (default 100)")
    ap.add_argument("--min-ratings", type=int, default=10, help="least community ratings (default 10)")
    ap.add_argument("--min-rating", type=float, default=3.5, help="lowest average rating (default 3.5)")
    args = ap.parse_args()

    platforms = {p for ps in EMULATORS.values() for p in ps}
    zf = open_metadata(args.metadata)
    built = datetime.datetime(*zf.getinfo("Metadata.xml").date_time[:3]).date().isoformat()

    games = {}                       # DatabaseID -> dict
    by_name = defaultdict(dict)      # platform -> exact name -> DatabaseID
    by_key = defaultdict(dict)       # platform -> title key -> DatabaseID
    alternates = defaultdict(list)   # DatabaseID -> alternate names
    with zf.open("Metadata.xml") as f:
        for _, el in ET.iterparse(f):
            if el.tag == "Game":
                p = el.findtext("Platform")
                if p in platforms:
                    gid = el.findtext("DatabaseID")
                    name = el.findtext("Name") or ""
                    games[gid] = {
                        "name": name, "platform": p,
                        "rating": float(el.findtext("CommunityRating") or 0),
                        "count": int(el.findtext("CommunityRatingCount") or 0),
                        "keys": {title_key(name)} - {""},   # the name's key, however short ("ys")
                    }
                    by_name[p][name] = gid
                    by_key[p].setdefault(title_key(name), gid)
                el.clear()
            elif el.tag == "GameAlternateName":
                alternates[el.findtext("DatabaseID")].append(el.findtext("AlternateName") or "")
                el.clear()
    # Keys from alternate names and file names need some length: those lists
    # also hold catalogue codes and fragments ("3", "fc"), which would match
    # unrelated files.
    def extra(k):
        return len(k) >= MIN_EXTRA_KEY
    for gid, names in alternates.items():
        if gid in games:
            games[gid]["keys"] |= {k for k in map(title_key, names) if extra(k)}
    with zf.open("Files.xml") as f:
        for _, el in ET.iterparse(f):
            if el.tag == "File":
                p = el.findtext("Platform")
                if p in platforms:
                    gname = el.findtext("GameName") or ""
                    gid = by_name[p].get(gname) or by_key[p].get(title_key(gname))
                    if gid:
                        k = title_key(el.findtext("FileName") or "")
                        if extra(k):
                            games[gid]["keys"].add(k)
                el.clear()

    os.makedirs(args.out, exist_ok=True)
    for emu, plats in EMULATORS.items():
        chosen = []
        for p in plats:
            plat = [g for g in games.values() if g["platform"] == p]
            # a system few people rate: fewer ratings, then a lower average
            for least, lowest in ((args.min_ratings, args.min_rating), (3, args.min_rating),
                                  (3, args.min_rating - 0.5)):
                rated = [g for g in plat if g["count"] >= least and g["rating"] >= lowest]
                if len(rated) >= MIN_GAMES:
                    break
            rated.sort(key=lambda g: (-g["count"], -g["rating"], g["name"]))
            chosen += rated[:args.top]
        seen = set()
        lines = []
        for g in chosen:
            gk = g["keys"] | {k + s for k in g["keys"] for s in PART_SUFFIXES.get(emu, "")}
            keys = sorted(gk - seen)
            if not keys:
                continue
            seen |= set(keys)
            lines.append(f"{' '.join(keys)}  # {g['name']} ({g['count']} ratings, {g['rating']:.1f})")
        path = os.path.join(args.out, f"{emu}.txt")
        with open(path, "w", encoding="ascii", errors="replace", newline="\n") as out:
            out.write(f"# pico-bootLoader attract mode: popular {SYSTEM_NAMES[emu]} games, most rated first.\n")
            out.write(f"# Made by tools/make_attract_lists.py from the LaunchBox Games Database of {built}:\n")
            out.write(f"# the {args.top} most rated games per system with an average of {args.min_rating} or more.\n")
            out.write("# A line is one game: the keys its ROM files may match, then '#' and a comment.\n")
            out.write("# Attract mode starts these about four times as often as the other games.\n")
            out.write("\n".join(lines) + "\n")
        print(f"{emu:5s} {len(lines):4d} games  -> {path}")


if __name__ == "__main__":
    main()
