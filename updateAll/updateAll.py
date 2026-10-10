#!/usr/bin/env python3
"""
updateAll - install arcade ROM sets on the SD card.

Takes MAME ROM zips from one folder, the way MiSTer keeps them in /games/mame,
and extracts each set into the folder on the SD card where its application
looks for it. The sets, their zips and their folders are defined in
updateAll.json next to this script; a new core is added there, not here.

A zip that is not in the folder is downloaded into it first, from the
databases listed in updateAll.json or given with --db; without --zips, into a
temporary folder that is removed afterwards. A database uses the format of the
MiSTer Downloader: a JSON file, optionally zipped, whose "files" map gives each
zip a "url" (or a "base_files_url" to prefix), a "size" and an MD5 "hash". No
database is configured by default. A set that is already complete on the card
needs no zip, so nothing is downloaded for it.

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
import io
import json
import os
import re
import shutil
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


class ConfigError(Exception):
    pass


class DownloadError(Exception):
    pass


def _bad_name(name):
    return (not name or name in (".", "..") or
            any(ch in name for ch in "/\\:*?\"<>|"))


def _is_url(src):
    return re.match(r"^https?://", src, re.I) is not None


def load_config(path):
    """Returns (cores, databases); relative database paths are made relative
    to the configuration file."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            cfg = json.load(f)
    except (OSError, ValueError) as e:
        raise ConfigError(f"cannot read {path}: {e}")

    cores = cfg.get("cores") if isinstance(cfg, dict) else None
    if not isinstance(cores, list) or not cores:
        raise ConfigError(f"{path}: no cores defined")

    ids = set()
    for core in cores:
        cid = core.get("id", "?")
        for key in ("id", "name", "zips", "destination", "files"):
            if key not in core:
                raise ConfigError(f"{path}: core '{cid}' has no '{key}'")
        if cid.lower() in ids:
            raise ConfigError(f"{path}: core '{cid}' is defined twice")
        ids.add(cid.lower())

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
            if _bad_name(f.get("name", "")):
                raise ConfigError(f"{path}: core '{cid}' has an invalid file name "
                                  f"'{f.get('name', '')}'")
            try:
                f["_crc"] = int(f["crc"], 16)
                f["_size"] = int(f["size"])
            except (KeyError, TypeError, ValueError):
                raise ConfigError(f"{path}: core '{cid}', file '{f['name']}' needs "
                                  "a numeric 'size' and a hexadecimal 'crc'")

    databases = cfg.get("databases", [])
    if not isinstance(databases, list) or not all(isinstance(d, str) for d in databases):
        raise ConfigError(f"{path}: 'databases' must be a list of URLs or paths")
    config_dir = os.path.dirname(os.path.abspath(path))
    databases = [d if _is_url(d) else os.path.join(config_dir, os.path.expanduser(d))
                 for d in databases]
    return cores, databases


# ---------------------------------------------------------------------------
# Databases and downloads
# ---------------------------------------------------------------------------

def _describe(e):
    if isinstance(e, urllib.error.HTTPError):
        return f"HTTP {e.code} {e.reason}"
    if isinstance(e, urllib.error.URLError):
        return str(e.reason)
    return str(e)


def _open_url(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    return urllib.request.urlopen(req, timeout=TIMEOUT)


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


def download(url, dest, size=None, md5=None):
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
            if not zipfile.is_zipfile(tmp):
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


def fetch_missing_zips(core, zip_dir, zip_listing, databases, dry_run):
    """Downloads the core's zips that are not in zip_dir. Returns (notes,
    errors, pending); pending is true when a dry run would have downloaded."""
    notes, errors, pending = [], [], False
    have = {e.lower() for e in zip_listing}
    for zname in core["zips"]:
        if zname.lower() in have:
            continue
        hit = databases.find(zname)
        if not hit:
            continue
        db, url, size, md5 = hit
        if dry_run:
            notes.append(f"would download {zname} from {db.id}")
            pending = True
            continue
        try:
            os.makedirs(zip_dir, exist_ok=True)
            got = download(url, os.path.join(zip_dir, zname), size, md5)
        except (DownloadError, OSError) as e:
            errors.append(f"download of {zname} from {db.id} failed: {e}")
            continue
        zip_listing.append(zname)
        notes.append(f"downloaded {zname} ({(got + 1023) // 1024} KB) from {db.id}")
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


class Result:
    def __init__(self):
        self.written = 0
        self.current = 0
        self.missing = []
        self.errors = []
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
            try:
                archives.append(zipfile.ZipFile(p))
            except (OSError, zipfile.BadZipFile) as e:
                res.errors.append(f"cannot open {os.path.basename(p)}: {e}")

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
            target = os.path.join(dest, f["name"])
            if is_current(target, f["_size"], f["_crc"]):
                res.current += 1
                continue

            candidates = index.get((f["_size"], f["_crc"]))
            if not candidates:
                same_name = by_name.get(f["name"].lower())
                if same_name:
                    zf, info = min(same_name, key=lambda c: entry_rank(c[1], f["name"]))
                    res.errors.append(f"{os.path.basename(zf.filename)}:{info.filename} is not the "
                                      "expected file (damaged, or from a different version of the set)")
                else:
                    res.missing.append(f["name"])
                continue
            zf, info = min(candidates, key=lambda c: entry_rank(c[1], f["name"]))
            source = f"{os.path.basename(zf.filename)}:{info.filename}"

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
                os.makedirs(dest, exist_ok=True)
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


def list_cores(cores, databases):
    print(f"{'ID':<12} {'Name':<14} {'Zip':<14} {'SD card folder':<26} Set")
    for c in cores:
        print(f"{c['id']:<12} {c['name']:<14} {', '.join(c['zips']):<14} "
              f"{c['_dest_shown']:<26} {c.get('set', '')}, {len(c['files'])} files")
    print()
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
                    "from the configured databases first.")
    ap.add_argument("--zips", metavar="DIR",
                    help="folder with MAME zips you already have, e.g. phoenix.zip; "
                         "downloaded zips are kept here as well (default: download "
                         "into a temporary folder that is removed afterwards)")
    ap.add_argument("--sd", metavar="DIR",
                    help="root of the SD card, e.g. E:\\ or /media/user/SDCARD; "
                         "any existing folder will do")
    ap.add_argument("--core", metavar="ID", action="append",
                    help="install only this core; repeat or separate with commas "
                         "(default: every core in the configuration)")
    ap.add_argument("--db", metavar="URL|FILE", action="append", default=[],
                    help="download database to use in addition to those in the "
                         "configuration; repeat or separate with commas")
    ap.add_argument("--no-download", action="store_true",
                    help="use only the zips already in the zip folder")
    ap.add_argument("--config", metavar="FILE", default=DEFAULT_CONFIG,
                    help="configuration file (default: updateAll.json next to this script)")
    ap.add_argument("--dry-run", action="store_true",
                    help="show what would be downloaded and written without doing it")
    ap.add_argument("--list", action="store_true",
                    help="list the configured cores and databases and exit")
    args = ap.parse_args(argv)

    try:
        cores, db_sources = load_config(args.config)
    except ConfigError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    extra = [d.strip() for arg in args.db for d in arg.split(",") if d.strip()]
    db_sources += [d if _is_url(d) else os.path.abspath(os.path.expanduser(d)) for d in extra]

    if args.list:
        list_cores(cores, db_sources)
        return 0

    if not args.sd:
        ap.error("--sd is required (or use --list)")
    if not os.path.isdir(args.sd):
        print(f"ERROR: SD card root not found: {args.sd}", file=sys.stderr)
        return 1
    may_download = bool(db_sources) and not args.no_download
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
        unknown = [cid for cid in requested if cid not in known]
        if unknown:
            print(f"ERROR: unknown core: {', '.join(unknown)}. Configured: "
                  f"{', '.join(c['id'] for c in cores)}", file=sys.stderr)
            return 1
        cores = [c for c in cores if c["id"].lower() in requested]

    print(f"Zips from:    {os.path.abspath(args.zips) if args.zips else 'downloads only'}")
    print(f"SD card:      {os.path.abspath(args.sd)}")
    if may_download:
        print(f"Databases:    {len(db_sources)}, used for zips that are not in the zip folder")
    if args.dry_run:
        print("Dry run:      nothing is downloaded or written")
    print()

    # Without --zips, downloads go to a temporary folder that is removed again.
    temp_dir = None if args.zips else tempfile.mkdtemp(prefix="updateAll-")
    try:
        return run(args, cores, requested, args.zips or temp_dir,
                   Databases(db_sources if may_download else []))
    finally:
        if temp_dir:
            shutil.rmtree(temp_dir, ignore_errors=True)


def files_to_install(core, sd_root):
    dest = os.path.join(sd_root, *core["_dest_parts"])
    return [f for f in core["files"]
            if not is_current(os.path.join(dest, f["name"]), f["_size"], f["_crc"])]


def run(args, cores, requested, zip_dir, databases):
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

        notes, dl_errors, pending = fetch_missing_zips(core, zip_dir, zip_listing,
                                                       databases, args.dry_run)
        res, used = install_core(core, zip_dir, zip_listing, args.sd, args.dry_run)
        res.errors[:0] = dl_errors

        if res.skipped and not pending and not dl_errors:
            skipped += 1
            where = ", and not in any database" if databases.loaded else ""
            print(f"{core['name']}: skipped, {res.skipped}{where}")
            continue

        print(f"{core['name']}: {', '.join(used or core['zips'])} -> {core['_dest_shown']}")
        for note in notes:
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
