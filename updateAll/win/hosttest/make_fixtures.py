#!/usr/bin/env python3
"""
Writes the fixtures compare.py runs updateAll.py and ua_host against:
configurations with made-up sets of random files (no real ROMs), their zips,
download databases, and the files the test HTTP server serves.

Usage: make_fixtures.py <output dir> <http port>
"""

import hashlib
import json
import os
import random
import struct
import sys
import zipfile
import zlib

OUT, PORT = sys.argv[1], int(sys.argv[2])
URL = f"http://127.0.0.1:{PORT}"
random.seed(20261008)


def blob(n):
    return bytes(random.getrandbits(8) for _ in range(n))


def crc(data):
    return "%08x" % (zlib.crc32(data) & 0xFFFFFFFF)


def write(path, data):
    path = os.path.join(OUT, path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    return path


def make_zip(path, entries, method=zipfile.ZIP_DEFLATED):
    path = os.path.join(OUT, path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with zipfile.ZipFile(path, "w", method) as zf:
        for name, data in entries:
            zf.writestr(name, data)
    return path


def corrupt_entry(path, name):
    """Flips a byte of a stored entry's data; the directory keeps its CRC."""
    with zipfile.ZipFile(path) as zf:
        info = zf.getinfo(name)
    with open(path, "r+b") as f:
        f.seek(info.header_offset + 26)
        name_len, extra_len = struct.unpack("<HH", f.read(4))
        f.seek(info.header_offset + 30 + name_len + extra_len)
        b = f.read(1)
        f.seek(-1, 1)
        f.write(bytes([b[0] ^ 0xFF]))


def files_of(prefix, n, size=1024):
    return {f"{prefix}{i}": blob(size + i) for i in range(1, n + 1)}


def core(cid, zips, dest, files, name=None):
    return {
        "id": cid, "name": name or cid.capitalize(), "set": f"test set {cid}",
        "zips": zips, "destination": dest,
        "files": [{"name": k, "size": len(v), "crc": crc(v)} for k, v in files.items()],
    }


local = []      # sets installed from zips in the zip folder

# Flat zip, stored on disk under an upper-case name.
alpha = files_of("a", 4)
make_zip("zips/ALPHA.ZIP", list(alpha.items()))
local.append(core("alpha", ["alpha.zip"], "/roms/ALPHA", alpha))

# Merged zip: a renamed file at the root, a clone subfolder with a file of
# its own, a duplicate of a root file and a variant under a root name.
beta = files_of("b", 5)
make_zip("zips/beta.zip", [
    ("b1", beta["b1"]), ("b2", beta["b2"]), ("b3", beta["b3"]), ("b4.renamed", beta["b4"]),
    ("betaj/", b""), ("betaj/b5", beta["b5"]), ("betaj/b2", beta["b2"]), ("betaj/b1", blob(1025)),
])
local.append(core("beta", ["beta.zip"], "\\roms\\arcade\\BETA\\", beta))

# Split set: the clone zip first, the parent second.
gamma = files_of("g", 3)
make_zip("zips/gammac.zip", [("g3", gamma["g3"])])
make_zip("zips/gamma.zip", [("g1", gamma["g1"]), ("g2", gamma["g2"])])
local.append(core("gamma", ["gammac.zip", "gamma.zip"], "roms/arcade/GAMMA", gamma))

# A file the zip does not have.
delta = files_of("d", 3)
make_zip("zips/delta.zip", [("d1", delta["d1"]), ("d2", delta["d2"])])
local.append(core("delta", ["delta.zip"], "/roms/DELTA", delta))

# A file of the expected name with other contents.
eps = files_of("e", 2)
make_zip("zips/eps.zip", [("e1", eps["e1"]), ("e2", blob(len(eps["e2"])))])
local.append(core("eps", ["eps.zip"], "/roms/EPS", eps))

# A damaged entry: its data no longer matches the CRC in the directory.
zeta = files_of("z", 2)
corrupt_entry(make_zip("zips/zeta.zip", list(zeta.items()), zipfile.ZIP_STORED), "z1")
local.append(core("zeta", ["zeta.zip"], "/roms/ZETA", zeta))

# A zip that is not there at all, and one that is not a zip.
local.append(core("eta", ["eta.zip"], "/roms/ETA", files_of("h", 2)))
write("zips/xi.zip", b"this is not a zip file")
local.append(core("xi", ["xi.zip"], "/roms/XI", files_of("x", 2)))

with open(os.path.join(OUT, "cfg_local.json"), "w") as f:
    json.dump({"cores": local}, f, indent=2)

# A card on which part of alpha is already right and one file is wrong.
write("card_partial/roms/ALPHA/a1", alpha["a1"])
write("card_partial/roms/ALPHA/a2", alpha["a2"])
write("card_partial/roms/ALPHA/a3", blob(10))
write("card_partial/roms/ALPHA/other.txt", b"left alone")

# ---------------------------------------------------------------------------
# Download databases
# ---------------------------------------------------------------------------

remote = []
db_files = {}


def serve(path, data):
    write(os.path.join("srv", path), data)
    return f"{URL}/{path}"


def zip_bytes(entries):
    import io
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, data in entries:
            zf.writestr(name, data)
    return buf.getvalue()


def md5(data):
    return hashlib.md5(data).hexdigest()


# Downloaded and installed.
theta = files_of("t", 3)
data = zip_bytes(list(theta.items()))
db_files["games/mame/theta.zip"] = {"url": serve("files/theta.zip", data),
                                    "size": len(data), "hash": md5(data).upper()}
remote.append(core("theta", ["theta.zip"], "/roms/THETA", theta))

# Wrong MD5, wrong size, not found, and a file that is not a zip.
iota = files_of("i", 1)
data = zip_bytes(list(iota.items()))
db_files["games/mame/iota.zip"] = {"url": serve("files/iota.zip", data), "size": len(data),
                                   "hash": "0" * 32}
remote.append(core("iota", ["iota.zip"], "/roms/IOTA", iota))

lam = files_of("l", 1)
data = zip_bytes(list(lam.items()))
db_files["games/mame/lam.zip"] = {"url": serve("files/lam.zip", data), "size": len(data) + 1}
remote.append(core("lam", ["lam.zip"], "/roms/LAM", lam))

kappa = files_of("k", 1)
db_files["games/mame/kappa.zip"] = {"url": f"{URL}/files/missing/kappa.zip"}
remote.append(core("kappa", ["kappa.zip"], "/roms/KAPPA", kappa))

nu = files_of("n", 1)
data = b"not a zip, although size and MD5 match"
db_files["games/mame/nu.zip"] = {"url": serve("files/nu.zip", data), "size": len(data),
                                 "hash": md5(data)}
remote.append(core("nu", ["nu.zip"], "/roms/NU", nu))

# Listed under hbmame first, with other contents, and under mame, both
# without a url, so that base_files_url and quoting are used.
mu = files_of("m", 2)
wrong = zip_bytes([("m1", blob(len(mu["m1"]))), ("m2", mu["m2"])])
right = zip_bytes(list(mu.items()))
serve("files/games/hbmame/mu set.zip", wrong)
serve("files/games/mame/mu set.zip", right)
db_files["games/hbmame/mu set.zip"] = {"size": len(wrong), "hash": md5(wrong)}
db_files["|games/mame/mu set.zip"] = {"size": len(right), "hash": md5(right)}
remote.append(core("mu", ["mu set.zip"], "/roms/arcade/MU", mu))

# In the database, but already in the zip folder, so not downloaded.
omicron = files_of("o", 2)
make_zip("zips/omicron.zip", list(omicron.items()))
db_files["games/mame/omicron.zip"] = {"url": f"{URL}/files/missing/omicron.zip"}
remote.append(core("omicron", ["omicron.zip"], "/roms/OMICRON", omicron))

# In no database.
remote.append(core("pi", ["pi.zip"], "/roms/PI", files_of("p", 1)))

db = {"db_id": "test_db", "base_files_url": f"{URL}/files/", "files": db_files}
db_text = json.dumps(db, indent=1).encode()
serve("db.json", db_text)
serve("db.json.zip", zip_bytes([("db.json", db_text)]))
serve("nofiles.json", json.dumps({"db_id": "x"}).encode())
serve("broken.json", b"{ this is not json")
write("db_local.json", json.dumps({"files": db_files}).encode())   # no db_id: the path is shown
write("db_zipped.json.zip", zip_bytes([("readme.txt", b"x"), ("sub/db.json", db_text)]))
write("db_nojson.zip", zip_bytes([("readme.txt", b"x")]))

with open(os.path.join(OUT, "cfg_db.json"), "w") as f:
    json.dump({"databases": [f"{URL}/db.json.zip"], "cores": remote}, f, indent=2)
with open(os.path.join(OUT, "cfg_dbfile.json"), "w") as f:
    json.dump({"databases": ["db_local.json"], "cores": remote}, f, indent=2)
with open(os.path.join(OUT, "cfg_nodb.json"), "w") as f:
    json.dump({"cores": remote}, f, indent=2)

# ---------------------------------------------------------------------------
# Invalid configurations
# ---------------------------------------------------------------------------

good = core("ok", ["ok.zip"], "/roms/OK", files_of("q", 1))


def bad(name, cfg):
    with open(os.path.join(OUT, f"bad_{name}.json"), "w") as f:
        f.write(cfg if isinstance(cfg, str) else json.dumps(cfg))


bad("syntax", '{"cores": [ }')
bad("nocores", {"cores": []})
bad("missing_key", {"cores": [{k: v for k, v in good.items() if k != "files"}]})
bad("twice", {"cores": [good, dict(good, id="OK")]})
bad("dest", {"cores": [dict(good, destination="/roms/../x")]})
bad("dest_empty", {"cores": [dict(good, destination="//")]})
bad("nozips", {"cores": [dict(good, zips=[])]})
bad("zipname", {"cores": [dict(good, zips=["a/b.zip"])]})
bad("filename", {"cores": [dict(good, files=[{"name": "x:y", "size": 1, "crc": "0"}])]})
bad("crc", {"cores": [dict(good, files=[{"name": "x", "size": 1, "crc": "xyz"}])]})
bad("size", {"cores": [dict(good, files=[{"name": "x", "size": "big", "crc": "0"}])]})
bad("databases", {"databases": "http://x", "cores": [good]})
