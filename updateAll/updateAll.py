#!/usr/bin/env python3
"""
updateAll - install arcade ROM sets and game data on the SD card.

Takes MAME ROM zips from one folder, the way MiSTer keeps them in /games/mame,
and extracts each set into the folder on the SD card where its application
looks for it. The sets, their zips and their folders are defined in
updateAll.json next to this script; a new core is added there, not here.

The "data" list of updateAll.json adds the shareware and demo data of the
ported games. Each entry is downloaded from its own URL, as a zip or as the one
file to install; its files may go into subfolders, and zips inside the
download that it names under "archives" are searched as well. An entry with
"uf2" turns the downloaded file into a UF2 image for the loader to flash.

The "bios" list adds the BIOS files that the emulators need, in the same form
as "data". The Windows program shows them as one set, and --core bios selects
them together. A BIOS file may be built from its download rather than taken
from it as is: "offset" cuts it out of a download that is one file, and
"parts" joins entries of a zip.

A zip that is not in the folder is downloaded into it first, from the
databases listed in updateAll.json or given with --db; without --zips, into a
temporary folder that is removed afterwards. A database uses the format of the
MiSTer Downloader: a JSON file, optionally zipped, whose "files" map gives each
zip a "url" (or a "base_files_url" to prefix), a "size" and an MD5 "hash". A
set that is already complete on the card needs no zip, so nothing is
downloaded for it.

When updateAll.json has a "config_url", the configuration is first read from
there, so that sets added since this copy was made are installed too. If it
cannot be read, the local file is used.

Only the files listed for a set are extracted. They are found by size and
CRC32 rather than by name, as the applications themselves do, so merged, split
and non-merged sets all work, and they are written under the names from the
configuration. Files already on the card with the right contents are left
alone, and nothing on the card is ever deleted.

Usage:
    python3 updateAll.py --zips <folder with MAME zips> --sd <SD card root>
    python3 updateAll.py --sd <SD card root>        (download from the databases)
    python3 updateAll.py --list

Exit status is 0 when every set that was found is complete on the card, and 1
otherwise.
"""

import argparse
import hashlib
import http.client
import io
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
import zlib

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CONFIG = os.path.join(SCRIPT_DIR, "updateAll.json")
USER_AGENT = "updateAll (pico-bootLoader)"
DOWNLOAD_ATTEMPTS = 3
TIMEOUT = 60
CONFIG_TIMEOUT = 15


class ConfigError(Exception):
    pass


class DownloadError(Exception):
    pass


def _bad_name(name):
    return (not name or name in (".", "..") or
            any(ch in name for ch in "/\\:*?\"<>|"))


def _is_url(src):
    return re.match(r"^https?://", src, re.I) is not None


def _hex32(text):
    """int(text, 16), as for a CRC, limited to 32 bits."""
    n = int(text, 16)
    if not 0 <= n <= 0xFFFFFFFF:
        raise ValueError(text)
    return n


def load_config(path):
    """Returns (cores, databases, config_url); relative database paths are
    made relative to the configuration file."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    except (OSError, ValueError) as e:
        raise ConfigError(f"cannot read {path}: {e}")
    return parse_config(text, path, os.path.dirname(os.path.abspath(path)))


def parse_config(text, path, config_dir):
    """load_config of a configuration that is already read; path names it in
    messages, and relative database paths are made relative to config_dir."""
    try:
        cfg = json.loads(text)
    except ValueError as e:
        raise ConfigError(f"cannot read {path}: {e}")

    cores = cfg.get("cores") if isinstance(cfg, dict) else None
    if not isinstance(cores, list) or not cores:
        raise ConfigError(f"{path}: no cores defined")
    # "data" holds game data that comes from one URL rather than from a MAME
    # zip in the databases. It is a key of its own because copies of updateAll
    # that predate it read this file too, and skip keys they do not know.
    data = cfg.get("data", [])
    if not isinstance(data, list):
        raise ConfigError(f"{path}: 'data' must be a list")
    # "bios" holds the BIOS files, in the form of game data; a key of its own
    # for the same reason, and so that they can be selected as one set.
    bios = cfg.get("bios", [])
    if not isinstance(bios, list):
        raise ConfigError(f"{path}: 'bios' must be a list")

    ids = set()
    for core, kind in ([(c, "cores") for c in cores] + [(d, "data") for d in data] +
                       [(b, "bios") for b in bios]):
        is_data = kind != "cores"
        cid = core.get("id", "?")
        for key in ("id", "name", "url" if is_data else "zips", "destination", "files"):
            if key not in core:
                raise ConfigError(f"{path}: core '{cid}' has no '{key}'")
        if cid.lower() == "bios":
            raise ConfigError(f"{path}: core '{cid}' has a reserved id")
        if cid.lower() in ids:
            raise ConfigError(f"{path}: core '{cid}' is defined twice")
        ids.add(cid.lower())

        core["_bios"] = kind == "bios"
        core["_url"] = None
        core["_archives"] = []
        core["_uf2"] = None
        if is_data:
            # The file is kept in the zip folder under the last part of the
            # URL, which is also the name in the "Zip" column.
            url = core["url"]
            zname = ""
            if isinstance(url, str) and _is_url(url):
                zname = urllib.parse.unquote(urllib.parse.urlsplit(url).path.rsplit("/", 1)[-1])
            if _bad_name(zname):
                raise ConfigError(f"{path}: core '{cid}' has an invalid url '{url}'")
            archives = core.get("archives", [])
            if (not isinstance(archives, list) or
                    any(not isinstance(a, str) or _bad_name(a) for a in archives)):
                raise ConfigError(f"{path}: core '{cid}' has an invalid archive name")
            uf2 = core.get("uf2")
            if uf2 is not None:
                try:
                    core["_uf2"] = (_hex32(uf2["address"]), _hex32(uf2["family"]))
                except (KeyError, TypeError, ValueError):
                    raise ConfigError(f"{path}: core '{cid}' needs a hexadecimal 'address' "
                                      "and 'family' in 'uf2'")
            core["_url"] = url
            core["_archives"] = archives
            core["zips"] = [zname]

        dest = core["destination"].replace("\\", "/").strip("/")
        parts = dest.split("/")
        if not dest or any(_bad_name(p) for p in parts):
            raise ConfigError(f"{path}: core '{cid}' has an invalid destination "
                              f"'{core['destination']}'")
        core["_dest_parts"] = parts
        core["_dest_shown"] = "/" + dest

        if not core["zips"] or not core["files"]:
            raise ConfigError(f"{path}: core '{cid}' needs at least one zip and one file")
        if any(_bad_name(z) for z in core["zips"]):
            raise ConfigError(f"{path}: core '{cid}' has an invalid zip name")
        for f in core["files"]:
            # Game data may go into subfolders of its destination; ROM sets
            # may not, as older copies of updateAll would reject that.
            name = f.get("name", "")
            f["_parts"] = name.split("/") if is_data and isinstance(name, str) else [name]
            if any(_bad_name(part) for part in f["_parts"]):
                raise ConfigError(f"{path}: core '{cid}' has an invalid file name "
                                  f"'{f.get('name', '')}'")
            try:
                f["_crc"] = int(f["crc"], 16)
                f["_size"] = int(f["size"])
            except (KeyError, TypeError, ValueError):
                raise ConfigError(f"{path}: core '{cid}', file '{f['name']}' needs "
                                  "a numeric 'size' and a hexadecimal 'crc'")
            # A BIOS file may be part of a download that is one file, or the
            # entries of a zip joined; older copies do not know either.
            f["_offset"] = None
            f["_join"] = []
            if kind == "bios" and "offset" in f:
                try:
                    f["_offset"] = _hex32(f["offset"])
                except (TypeError, ValueError):
                    raise ConfigError(f"{path}: core '{cid}', file '{f['name']}' needs "
                                      "a hexadecimal 'offset'")
            if kind == "bios" and "parts" in f:
                join = f["parts"]
                if (not isinstance(join, list) or not join or
                        any(not isinstance(p, str) or _bad_name(p) for p in join)):
                    raise ConfigError(f"{path}: core '{cid}', file '{f['name']}' has "
                                      "an invalid 'parts' list")
                f["_join"] = join
        core["_sliced"] = any(f["_offset"] is not None for f in core["files"])
        joined = any(f["_join"] for f in core["files"])
        if core["_sliced"] + joined + (core["_uf2"] is not None) > 1:
            raise ConfigError(f"{path}: core '{cid}' cannot combine 'offset', 'parts' and 'uf2'")

    databases = cfg.get("databases", [])
    if not isinstance(databases, list) or not all(isinstance(d, str) for d in databases):
        raise ConfigError(f"{path}: 'databases' must be a list of URLs or paths")
    databases = [d if _is_url(d) else os.path.join(config_dir, os.path.expanduser(d))
                 for d in databases]

    config_url = cfg.get("config_url")
    if config_url in (None, ""):
        config_url = None
    elif not (isinstance(config_url, str) and _is_url(config_url)):
        raise ConfigError(f"{path}: 'config_url' must be an http or https URL")
    return cores + data + bios, databases, config_url


def fetch_config(url, config_dir):
    """The configuration at url, as load_config returns it. Its own
    config_url is not followed."""
    try:
        with _open_url(url, CONFIG_TIMEOUT) as r:
            text = r.read().decode("utf-8")
    except (OSError, ValueError, http.client.HTTPException) as e:
        raise ConfigError(f"cannot read {url}: {_describe(e)}")
    cores, databases, _ = parse_config(text, url, config_dir)
    return cores, databases, url


# ---------------------------------------------------------------------------
# Databases and downloads
# ---------------------------------------------------------------------------

def _describe(e):
    if isinstance(e, urllib.error.HTTPError):
        return f"HTTP {e.code} {e.reason}"
    if isinstance(e, urllib.error.URLError):
        return str(e.reason)
    return str(e)


def _open_url(url, timeout=TIMEOUT):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    return urllib.request.urlopen(req, timeout=timeout)


class Database:
    def __init__(self, source):
        self.source = source
        if _is_url(source):
            with _open_url(source) as r:
                data = r.read()
        else:
            with open(source, "rb") as f:
                data = f.read()
        if data[:4] == b"PK\x03\x04":   # a zipped database, as MiSTer publishes them
            with zipfile.ZipFile(io.BytesIO(data)) as zf:
                names = [n for n in zf.namelist() if n.lower().endswith(".json")]
                if not names:
                    raise ValueError("the zip holds no .json file")
                data = zf.read(names[0])
        db = json.loads(data.decode("utf-8"))
        if not isinstance(db, dict) or not isinstance(db.get("files"), dict):
            raise ValueError("no 'files' map")
        self.id = db.get("db_id") or source
        self.base = db.get("base_files_url") or ""
        self.files = db["files"]

    def find(self, zip_name):
        """(url, size, md5) of the zip, or None."""
        hits = []
        for key, entry in self.files.items():
            path = key.lstrip("|")   # MiSTer marks paths for external storage with '|'
            if isinstance(entry, dict) and path.rsplit("/", 1)[-1].lower() == zip_name.lower():
                hits.append((path, entry))
        # A MiSTer database can hold the same zip name under games/mame and
        # games/hbmame; the MAME one is the set the applications expect.
        hits.sort(key=lambda h: (h[0].lower().split("/")[-2:-1] != ["mame"]))
        for path, entry in hits:
            url = entry.get("url") or (self.base + urllib.parse.quote(path) if self.base else "")
            if url:
                return url, entry.get("size"), entry.get("hash")
        return None


class Databases:
    """Loaded on first use, so that nothing is fetched while every zip is at hand."""

    def __init__(self, sources):
        self.sources = sources
        self.loaded = None
        self.errors = []

    def find(self, zip_name):
        if self.loaded is None:
            self.loaded = []
            for src in self.sources:
                try:
                    self.loaded.append(Database(src))
                except (OSError, ValueError, zipfile.BadZipFile, urllib.error.URLError) as e:
                    self.errors.append(f"database {src}: {_describe(e)}")
                    print(f"ERROR: database {src}: {_describe(e)}")
        for db in self.loaded:
            hit = db.find(zip_name)
            if hit:
                return (db,) + hit
        return None


def download(url, dest, size=None, md5=None, need_zip=True):
    """Downloads to dest via a .part file; returns the number of bytes."""
    tmp = dest + ".part"
    reason = "no attempt made"
    for attempt in range(DOWNLOAD_ATTEMPTS):
        if attempt:
            time.sleep(2 * attempt)
        try:
            digest = hashlib.md5()
            got = 0
            with _open_url(url) as r, open(tmp, "wb") as out:
                for chunk in iter(lambda: r.read(65536), b""):
                    out.write(chunk)
                    digest.update(chunk)
                    got += len(chunk)
            if size is not None and got != int(size):
                raise DownloadError(f"received {got} bytes, the database says {size}")
            if md5 and digest.hexdigest().lower() != str(md5).lower():
                raise DownloadError("the MD5 does not match the database")
            if need_zip and not zipfile.is_zipfile(tmp):
                raise DownloadError("the file received is not a zip")
            os.replace(tmp, dest)
            return got
        except urllib.error.HTTPError as e:
            reason = _describe(e)
            if 400 <= e.code < 500:
                break           # retrying will not help
        except (urllib.error.URLError, OSError, DownloadError) as e:
            reason = _describe(e)
        finally:
            if os.path.exists(tmp):
                try:
                    os.remove(tmp)
                except OSError:
                    pass
    raise DownloadError(reason)


def fetch_missing_zips(core, zip_dir, zip_listing, databases, dry_run, url_downloads):
    """Downloads the core's zips that are not in zip_dir, from the databases,
    or from the core's own URL when url_downloads is true. Returns (notes,
    errors, pending); pending is true when a dry run would have downloaded."""
    notes, errors, pending = [], [], False
    have = {e.lower() for e in zip_listing}
    for zname in core["zips"]:
        if zname.lower() in have:
            continue
        if core["_url"]:
            if not url_downloads:
                continue
            url, size, md5 = core["_url"], None, None
            source = urllib.parse.urlsplit(url).hostname or url
        else:
            hit = databases.find(zname)
            if not hit:
                continue
            db, url, size, md5 = hit
            source = db.id
        if dry_run:
            notes.append(f"would download {zname} from {source}")
            if core["_uf2"]:
                notes.append(uf2_note(zname, core["_uf2"][0], dry_run))
            pending = True
            continue
        try:
            os.makedirs(zip_dir, exist_ok=True)
            got = download(url, os.path.join(zip_dir, zname), size, md5,
                           need_zip=not core["_url"])
        except (DownloadError, OSError) as e:
            errors.append(f"download of {zname} from {source} failed: {e}")
            continue
        zip_listing.append(zname)
        notes.append(f"downloaded {zname} ({(got + 1023) // 1024} KB) from {source}")
    return notes, errors, pending


# ---------------------------------------------------------------------------
# Extraction
# ---------------------------------------------------------------------------

def file_crc(path):
    crc = 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            crc = zlib.crc32(chunk, crc)
    return crc & 0xFFFFFFFF


def is_current(path, size, crc):
    try:
        return os.path.getsize(path) == size and file_crc(path) == crc
    except OSError:
        return False


def entry_rank(info, wanted_name):
    """Lower is better: the root of the zip before clone subfolders, and the
    expected name before any other file with the same contents."""
    name = info.filename.replace("\\", "/")
    in_subfolder = "/" in name
    other_name = name.rsplit("/", 1)[-1].lower() != wanted_name.lower()
    return (in_subfolder, other_name)


def to_uf2(data, address, family):
    """data as a UF2 image at address with this family ID, as picotool's
    "uf2 convert -t bin" writes it: 256 bytes in each 512-byte block, the last
    one padded with zeros."""
    n = (len(data) + 255) // 256
    out = bytearray()
    for i in range(n):
        out += struct.pack("<8I", 0x0A324655, 0x9E5D5157, 0x00002000,
                           (address + 256 * i) & 0xFFFFFFFF, 256, i, n, family)
        out += data[256 * i:256 * (i + 1)].ljust(476, b"\0")
        out += struct.pack("<I", 0x0AB16F30)
    return bytes(out)


class MemoryArchive:
    """Files held in memory, opened as if they were a zip holding them, so
    that install_core treats them like the entries of a zip."""

    def __init__(self, label):
        self.label = label
        self.entries = []

    def add(self, name, data):
        info = zipfile.ZipInfo(name)
        info.file_size = len(data)
        info.CRC = zlib.crc32(data) & 0xFFFFFFFF
        self.entries.append((info, data))

    def infolist(self):
        return [info for info, _ in self.entries]

    def read(self, info):
        return next(data for i, data in self.entries if i is info)

    def close(self):
        pass


class PlainFile(MemoryArchive):
    """A downloaded game-data file that is not a zip, held as if it were a
    zip holding just itself. With a (address, family) for uf2, it holds the
    file as a UF2 image instead. A BIOS file with an offset is held as well,
    under its own name, as that part of the download."""

    def __init__(self, path, uf2=None, files=()):
        with open(path, "rb") as f:
            data = f.read()
        if uf2:
            data = to_uf2(data, *uf2)
        super().__init__(os.path.basename(path))
        self.add(self.label, data)
        for f in files:
            if f["_offset"] is not None:
                self.add(f["_parts"][-1], data[f["_offset"]:f["_offset"] + f["_size"]])


def join_parts(f, archives, res):
    """The BIOS file f, made of the entries that its "parts" name, joined in
    that order, as an archive holding just that file; None when a part is not
    there or cannot be read, which is added to res.errors."""
    data = b""
    for part in f["_join"]:
        found = [(zf, i) for zf in archives for i in zf.infolist()
                 if not i.is_dir() and
                 i.filename.replace("\\", "/").rsplit("/", 1)[-1].lower() == part.lower()]
        if not found:
            res.errors.append(f"{archives[0].label} holds no {part}")
            return None
        zf, info = min(found, key=lambda c: entry_rank(c[1], part))
        try:
            data += zf.read(info)
        except (OSError, zipfile.BadZipFile, NotImplementedError, zlib.error) as e:
            res.errors.append(f"{zf.label}:{info.filename}: {e}")
            return None
    joined = MemoryArchive(archives[0].label)
    joined.add(f["_parts"][-1], data)
    return joined


def uf2_note(name, address, dry_run):
    return f"{'would convert' if dry_run else 'converted'} {name} to a UF2 image at 0x{address:08X}"


def open_archives(core, path, res, dry_run):
    """The archives to search in the file at path: the zip itself, or the
    file alone when game data is not a zip or is to become a UF2 image, plus
    the zips inside it that the core names in "archives". Problems are added
    to res.errors, and a conversion to res.notes."""
    name = os.path.basename(path)
    if core["_url"] and (core["_uf2"] or core["_sliced"] or not zipfile.is_zipfile(path)):
        try:
            plain = PlainFile(path, core["_uf2"], core["files"])
        except OSError as e:
            res.errors.append(f"cannot open {name}: {e}")
            return []
        if core["_uf2"]:
            res.notes.append(uf2_note(name, core["_uf2"][0], dry_run))
        return [plain]
    try:
        zf = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as e:
        res.errors.append(f"cannot open {name}: {e}")
        return []
    zf.label = name
    archives = [zf]
    for inner in core["_archives"]:
        found = [i for i in zf.infolist()
                 if i.filename.replace("\\", "/").rsplit("/", 1)[-1].lower() == inner.lower()]
        if not found:
            res.errors.append(f"{name} holds no {inner}")
            continue
        label = f"{name}:{found[0].filename}"
        try:
            inner_zf = zipfile.ZipFile(io.BytesIO(zf.read(found[0])))
        except (OSError, zipfile.BadZipFile, NotImplementedError, zlib.error) as e:
            res.errors.append(f"cannot open {label}: {e}")
            continue
        inner_zf.label = label
        archives.append(inner_zf)
    return archives


class Result:
    def __init__(self):
        self.written = 0
        self.current = 0
        self.missing = []
        self.errors = []
        self.notes = []
        self.skipped = None   # reason, when no zip was found

    @property
    def complete(self):
        return self.skipped is None and not self.missing and not self.errors


def install_core(core, zip_dir, zip_listing, sd_root, dry_run):
    res = Result()

    zip_paths = []
    for zname in core["zips"]:
        for entry in zip_listing:
            if entry.lower() == zname.lower():
                zip_paths.append(os.path.join(zip_dir, entry))
                break
    if not zip_paths:
        res.skipped = " or ".join(core["zips"]) + " not found"
        return res, []

    archives = []
    try:
        for p in zip_paths:
            archives += open_archives(core, p, res, dry_run)
        opened = list(archives)
        for f in core["files"]:
            if f["_join"] and opened:
                joined = join_parts(f, opened, res)
                if joined:
                    archives.append(joined)

        index = {}
        by_name = {}
        for zf in archives:
            for info in zf.infolist():
                if not info.is_dir():
                    index.setdefault((info.file_size, info.CRC), []).append((zf, info))
                    base = info.filename.replace("\\", "/").rsplit("/", 1)[-1].lower()
                    by_name.setdefault(base, []).append((zf, info))

        dest = os.path.join(sd_root, *core["_dest_parts"])
        for f in core["files"]:
            target = os.path.join(dest, *f["_parts"])
            base = f["_parts"][-1]
            if is_current(target, f["_size"], f["_crc"]):
                res.current += 1
                continue

            candidates = index.get((f["_size"], f["_crc"]))
            if not candidates:
                same_name = by_name.get(base.lower())
                if core["_uf2"] and archives:
                    res.errors.append(f"the UF2 image of {archives[0].label} is not the expected "
                                      "file (damaged, or from a different version)")
                elif same_name:
                    zf, info = min(same_name, key=lambda c: entry_rank(c[1], base))
                    res.errors.append(f"{zf.label}:{info.filename} is not the "
                                      "expected file (damaged, or from a different version of the set)")
                else:
                    res.missing.append(f["name"])
                continue
            zf, info = min(candidates, key=lambda c: entry_rank(c[1], base))
            source = f"{zf.label}:{info.filename}"

            if dry_run:
                res.written += 1
                continue
            try:
                data = zf.read(info)    # zipfile checks the CRC itself
            except (OSError, zipfile.BadZipFile, NotImplementedError, zlib.error) as e:
                res.errors.append(f"{source}: {e}")
                continue
            if len(data) != f["_size"] or (zlib.crc32(data) & 0xFFFFFFFF) != f["_crc"]:
                res.errors.append(f"{source}: contents do not match the size and CRC")
                continue
            try:
                os.makedirs(os.path.dirname(target), exist_ok=True)
                tmp = target + ".tmp"
                with open(tmp, "wb") as out:
                    out.write(data)
                os.replace(tmp, target)
            except OSError as e:
                res.errors.append(f"cannot write {target}: {e}")
                continue
            res.written += 1
    finally:
        for zf in archives:
            zf.close()

    return res, [os.path.basename(p) for p in zip_paths]


def list_cores(cores, databases, sets_from):
    print(f"{'ID':<12} {'Name':<14} {'Zip':<14} {'SD card folder':<26} Set")
    for c in cores:
        print(f"{c['id']:<12} {c['name']:<14} {', '.join(c['zips']):<14} "
              f"{c['_dest_shown']:<26} {c.get('set', '')}, {len(c['files'])} files")
    print()
    if sets_from:
        print(f"Sets from:    {sets_from}")
    if databases:
        print("Databases:")
        for d in databases:
            print(f"  {d}")
    else:
        print("Databases: none configured, so missing zips are not downloaded")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Extract MAME arcade ROM zips into the folders on the SD card "
                    "where the applications look for them, downloading missing zips "
                    "from the configured databases first, and install the game data "
                    "of the ported games.")
    ap.add_argument("--zips", metavar="DIR",
                    help="folder with MAME zips you already have, e.g. phoenix.zip; "
                         "downloaded zips and game data are kept here as well (default: "
                         "download into a temporary folder that is removed afterwards)")
    ap.add_argument("--sd", metavar="DIR",
                    help="root of the SD card, e.g. E:\\ or /media/user/SDCARD; "
                         "any existing folder will do")
    ap.add_argument("--core", metavar="ID", action="append",
                    help="install only this core; repeat or separate with commas; "
                         "bios selects every BIOS file (default: every core in the "
                         "configuration)")
    ap.add_argument("--db", metavar="URL|FILE", action="append", default=[],
                    help="download database to use in addition to those in the "
                         "configuration; repeat or separate with commas")
    ap.add_argument("--no-download", action="store_true",
                    help="use only the files already in the zip folder")
    ap.add_argument("--config", metavar="FILE", default=DEFAULT_CONFIG,
                    help="configuration file (default: updateAll.json next to this "
                         "script); when it has a config_url, the configuration is read "
                         "from there, and from this file only if that fails")
    ap.add_argument("--dry-run", action="store_true",
                    help="show what would be downloaded and written without doing it")
    ap.add_argument("--list", action="store_true",
                    help="list the configured cores and databases and exit")
    args = ap.parse_args(argv)

    try:
        cores, db_sources, config_url = load_config(args.config)
    except ConfigError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    # The configuration at config_url replaces the local one; the local one
    # is kept when that cannot be read.
    sets_from = None
    if config_url:
        try:
            cores, db_sources, sets_from = fetch_config(
                config_url, os.path.dirname(os.path.abspath(args.config)))
        except ConfigError as e:
            sets_from = f"{os.path.abspath(args.config)} ({e})"
    extra = [d.strip() for arg in args.db for d in arg.split(",") if d.strip()]
    db_sources += [d if _is_url(d) else os.path.abspath(os.path.expanduser(d)) for d in extra]

    if args.list:
        list_cores(cores, db_sources, sets_from)
        return 0

    if not args.sd:
        ap.error("--sd is required (or use --list)")
    if not os.path.isdir(args.sd):
        print(f"ERROR: SD card root not found: {args.sd}", file=sys.stderr)
        return 1
    # Game data has its own URL, so it can be downloaded without a database.
    use_databases = bool(db_sources) and not args.no_download
    url_downloads = not args.no_download
    may_download = use_databases or (url_downloads and any(c["_url"] for c in cores))
    if not args.zips and not may_download:
        print("ERROR: nothing to install from: give a zip folder with --zips, "
              "or configure a database to download from", file=sys.stderr)
        return 1
    if args.zips and not os.path.isdir(args.zips) and not may_download:
        print(f"ERROR: zip folder not found: {args.zips}", file=sys.stderr)
        return 1

    requested = []
    if args.core:
        requested = [cid.strip().lower() for arg in args.core for cid in arg.split(",") if cid.strip()]
        known = {c["id"].lower() for c in cores}
        if any(c["_bios"] for c in cores):
            known.add("bios")       # every BIOS file
        unknown = [cid for cid in requested if cid not in known]
        if unknown:
            print(f"ERROR: unknown core: {', '.join(unknown)}. Configured: "
                  f"{', '.join(c['id'] for c in cores)}", file=sys.stderr)
            return 1
        cores = [c for c in cores
                 if c["id"].lower() in requested or (c["_bios"] and "bios" in requested)]

    if sets_from:
        print(f"Sets from:    {sets_from}")
    print(f"Zips from:    {os.path.abspath(args.zips) if args.zips else 'downloads only'}")
    print(f"SD card:      {os.path.abspath(args.sd)}")
    if use_databases:
        print(f"Databases:    {len(db_sources)}, used for zips that are not in the zip folder")
    if args.dry_run:
        print("Dry run:      nothing is downloaded or written")
    print()

    # Without --zips, downloads go to a temporary folder that is removed again.
    temp_dir = None if args.zips else tempfile.mkdtemp(prefix="updateAll-")
    try:
        return run(args, cores, requested, args.zips or temp_dir,
                   Databases(db_sources if use_databases else []), url_downloads)
    finally:
        if temp_dir:
            shutil.rmtree(temp_dir, ignore_errors=True)


def files_to_install(core, sd_root):
    dest = os.path.join(sd_root, *core["_dest_parts"])
    return [f for f in core["files"]
            if not is_current(os.path.join(dest, *f["_parts"]), f["_size"], f["_crc"])]


def run(args, cores, requested, zip_dir, databases, url_downloads):
    zip_listing = []
    if os.path.isdir(zip_dir):
        zip_listing = [e for e in os.listdir(zip_dir) if os.path.isfile(os.path.join(zip_dir, e))]

    complete = skipped = failed = to_download = 0
    for core in cores:
        # A set that is complete on the card needs no zip, so nothing is
        # opened or downloaded for it.
        if not files_to_install(core, args.sd):
            complete += 1
            print(f"{core['name']}: {core['_dest_shown']}")
            print(f"  {len(core['files'])} up to date")
            continue

        notes, dl_errors, pending = fetch_missing_zips(core, zip_dir, zip_listing, databases,
                                                       args.dry_run, url_downloads)
        res, used = install_core(core, zip_dir, zip_listing, args.sd, args.dry_run)
        res.errors[:0] = dl_errors

        if res.skipped and not pending and not dl_errors:
            skipped += 1
            where = ", and not in any database" if databases.loaded and not core["_url"] else ""
            print(f"{core['name']}: skipped, {res.skipped}{where}")
            continue

        print(f"{core['name']}: {', '.join(used or core['zips'])} -> {core['_dest_shown']}")
        for note in notes + res.notes:
            print(f"  {note}")
        if res.skipped:
            for err in res.errors:
                print(f"  ERROR: {err}")
            if pending:
                to_download += 1
            else:
                failed += 1
            continue

        parts = []
        if res.written:
            parts.append(f"{res.written} {'to write' if args.dry_run else 'written'}")
        if res.current:
            parts.append(f"{res.current} up to date")
        if parts:
            print(f"  {', '.join(parts)}")
        if res.missing:
            print(f"  INCOMPLETE: {len(res.missing)} of {len(core['files'])} files not in the zip: "
                  f"{', '.join(res.missing)}")
        for err in res.errors:
            print(f"  ERROR: {err}")
        if res.complete:
            complete += 1
        else:
            failed += 1

    print()
    summary = f"{complete} complete, {failed} incomplete, {skipped} skipped"
    if to_download:
        summary += f", {to_download} to download"
    print(summary)

    requested_skipped = bool(requested) and skipped > 0
    ok = ((complete or to_download) and not failed and not requested_skipped
          and not databases.errors)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
