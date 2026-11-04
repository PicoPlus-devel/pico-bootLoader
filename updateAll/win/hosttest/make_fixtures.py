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
# Configurations read from a config_url
# ---------------------------------------------------------------------------

# The served configuration has other sets and a database the local one lacks,
# and a config_url of its own that must not be followed.
remote_cfg = serve("cfg_remote.json", json.dumps(
    {"config_url": f"{URL}/no-such-cfg.json", "databases": [f"{URL}/db.json.zip"],
     "cores": remote}, indent=2).encode())
for name, url in (("fetch", remote_cfg), ("fetch404", f"{URL}/no-such-cfg.json"),
                  ("fetchbroken", f"{URL}/broken.json"), ("fetchnocores", f"{URL}/nofiles.json"),
                  ("fetchoff", "")):
    with open(os.path.join(OUT, f"cfg_{name}.json"), "w") as f:
        json.dump({"config_url": url, "cores": local}, f, indent=2)

# ---------------------------------------------------------------------------
# Game data: one URL per entry, a zip or a plain file
# ---------------------------------------------------------------------------


def data_entry(did, url, dest, files, archives=None, uf2=None):
    entry = {"id": did, "name": did.capitalize() + " data", "set": f"test data {did}",
             "url": url, "destination": dest,
             "files": [{"name": k, "size": len(v), "crc": crc(v)} for k, v in files.items()]}
    if archives is not None:
        entry["archives"] = archives
    if uf2 is not None:
        entry["uf2"] = uf2
    return entry


def uf2_image(data, address, family):
    """The UF2 file picotool's "uf2 convert -t bin -o <address> --family <family>"
    writes (checked against picotool with doom1.whx): 512-byte blocks of 256 data
    bytes, zero-filled."""
    blocks = -(-len(data) // 256)
    image = bytearray(512 * blocks)
    for n in range(blocks):
        struct.pack_into("<IIIIIIII", image, 512 * n, 0x0A324655, 0x9E5D5157, 0x2000,
                         address + 256 * n, 256, n, blocks, family)
        image[512 * n + 32:512 * n + 32 + 256] = data[256 * n:256 * n + 256].ljust(256, b"\0")
        struct.pack_into("<I", image, 512 * n + 508, 0x0AB16F30)
    return bytes(image)


def sfx_bytes(entries):
    """A self-extracting archive: an exe stub, then a zip whose offsets count
    from the start of the file, as in the Duke Nukem 3D shareware package."""
    import io
    buf = io.BytesIO()
    buf.write(b"MZ" + blob(3000))
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, data in entries:
            zf.writestr(name, data)
    data = buf.getvalue()
    eocd = data.rfind(b"PK\x05\x06")
    cd_size, cd_offset = struct.unpack("<II", data[eocd + 12:eocd + 20])
    assert cd_offset == eocd - cd_size, "offsets must be absolute"
    return data


game = []

# A plain file, kept in the zip folder under its URL's (unquoted) name and
# installed under another one.
rho = {"rho.uf2": blob(3000)}
serve("data/rho file.uf2", rho["rho.uf2"])
game.append(data_entry("rho", f"{URL}/data/rho%20file.uf2", "/roms/RHO", rho))

# A zip whose files go into subfolders of the destination; one file of the zip
# is not wanted. The URL has a query.
sigma = {"base/s1": blob(2000), "base/sub/s2": blob(2001), "readme.txt": b"sigma readme"}
data = zip_bytes([("roms/sigma/base/s1", sigma["base/s1"]),
                  ("roms/sigma/base/sub/s2", sigma["base/sub/s2"]),
                  ("roms/sigma/readme.txt", sigma["readme.txt"]),
                  ("unwanted.txt", b"not installed")])
game.append(data_entry("sigma", serve("data/sigma.zip", data) + "?download=1", "/roms/SIGMA",
                       sigma))
make_zip("zips_data/sigma.zip", [(f"roms/sigma/{k}", v) for k, v in sigma.items()])
write("zips_data/rho file.uf2", rho["rho.uf2"])

# A zip holding a self-extracting archive that holds the data; the licence is
# in both.
tau = {"TAU.GRP": blob(5000), "LICENSE.TXT": b"tau licence"}
inner = sfx_bytes(list(tau.items()))
data = zip_bytes([("LICENSE.TXT", tau["LICENSE.TXT"]), ("INSTALL.EXE", blob(500)),
                  ("TAU.SHR", inner)])
game.append(data_entry("tau", serve("data/tau13.zip", data), "/roms/TAU", tau, ["tau.shr"]))

# A plain file installed as a UF2 image, its last block only partly used.
omega_raw = blob(1000)
serve("data/omega.whx", omega_raw)
game.append(data_entry("omega", f"{URL}/data/omega.whx", "/roms/OMEGA",
                       {"omega.uf2": uf2_image(omega_raw, 0x10200000, 0xE48BFF58)},
                       uf2={"address": "0x10200000", "family": "e48bff58"}))
write("zips_data/omega.whx", omega_raw)

# The same file, but the image the configuration expects is at another address.
serve("data/omegabad.whx", omega_raw)
game.append(data_entry("omegabad", f"{URL}/data/omegabad.whx", "/roms/OMEGABAD",
                       {"omega.uf2": uf2_image(omega_raw, 0x10400000, 0xE48BFF58)},
                       uf2={"address": "0x10200000", "family": "e48bff58"}))

# Not found; a plain file with other contents; a zip without the named
# archive; and a named archive that is not a zip.
game.append(data_entry("upsilon", f"{URL}/data/missing/upsilon.uf2", "/roms/UPSILON",
                       {"upsilon.uf2": blob(100)}))
phi = {"phi.bin": blob(100)}
serve("data/phi.bin", blob(100))
game.append(data_entry("phi", f"{URL}/data/phi.bin", "/roms/PHI", phi))
chi = {"CHI.GRP": blob(100)}
serve("data/chi.zip", zip_bytes([("CHI.GRP.txt", b"x")]))
game.append(data_entry("chi", f"{URL}/data/chi.zip", "/roms/CHI", chi, ["CHI.SHR"]))
psi = {"PSI.GRP": blob(100)}
serve("data/psi.zip", zip_bytes([("sub/PSI.SHR", b"MZ not a zip")]))
game.append(data_entry("psi", f"{URL}/data/psi.zip", "/roms/PSI", psi, ["psi.shr"]))

with open(os.path.join(OUT, "cfg_data.json"), "w") as f:
    json.dump({"databases": [f"{URL}/db.json.zip"], "cores": [remote[0]], "data": game}, f,
              indent=2)

# A card on which part of sigma is already right and one file is wrong.
write("card_data_partial/roms/SIGMA/base/s1", sigma["base/s1"])
write("card_data_partial/roms/SIGMA/readme.txt", b"edited")

# ---------------------------------------------------------------------------
# BIOS files: game data in form, some built from their download
# ---------------------------------------------------------------------------


def bios_entry(bid, url, dest, files):
    """files: name -> (contents, extra keys of that file)."""
    entry = data_entry(bid, url, dest, {k: v for k, (v, _) in files.items()})
    entry["name"] = "Test BIOS"
    for f in entry["files"]:
        f.update(files[f["name"]][1])
    return entry


bios = []

# A plain file, installed under another name. Its name has brackets, as the
# Sega CD BIOSes of the MiSTer BIOS database: PowerShell 5.1 takes them for a
# wildcard pattern where a path is not literal.
b_plain = blob(1024)
serve("bios/[BIOS] o2.bin", b_plain)
bios.append(bios_entry("b_plain", f"{URL}/bios/%5BBIOS%5D%20o2.bin", "/bios",
                       {"o2rom.bin": (b_plain, {})}))
write("zips_bios/[BIOS] o2.bin", b_plain)

# A zip of several files, the wanted one among others.
b_zip = {"cd_us.md": blob(2048), "cd_eu.md": blob(2048)}
bios.append(bios_entry("b_zip", serve("bios/MegaCD.zip", zip_bytes(
    [("Other.md", blob(2048))] + [(f"bios/{k}", v) for k, v in b_zip.items()])),
    "/bios", {k: (v, {}) for k, v in b_zip.items()}))

# One file, two parts of it cut out, as the TI-99/4A console ROM and GROM
# come from MiSTer's boot0.rom; the offsets with and without "0x".
boot = blob(3000)
bios.append(bios_entry("b_slice", serve("bios/boot0.rom", boot), "/bios",
                       {"rom.bin": (boot[0x200:0x600], {"offset": "0x200"}),
                        "grom.bin": (boot[0x800:0xA00], {"offset": "800"})}))
write("zips_bios/boot0.rom", boot)

# A part with another CRC; a part that runs past the end of the file; and one
# that starts past it.
boot1 = blob(2000)
bios.append(bios_entry("b_slicebad", serve("bios/boot1.rom", boot1), "/bios/bad",
                       {"x.bin": (blob(256), {"offset": "0x10"}),
                        "y.bin": (blob(200), {"offset": "0x76C"}),
                        "z.bin": (blob(16), {"offset": "0x10000"})}))

# Two entries of a zip joined, as the TI disk controller ROM from MAME's
# ti99_fdc: the root before a subfolder, a part only in a subfolder, and
# names in another case.
fdc = (blob(400), blob(500))
bios.append(bios_entry("b_join", serve("bios/fdc.zip", zip_bytes(
    [("sub/fdc.u26", blob(400)), ("FDC.U26", fdc[0]), ("dsr/fdc.u27", fdc[1]),
     ("readme.txt", b"fdc")])), "/bios",
    {"disk.bin": (fdc[0] + fdc[1], {"parts": ["fdc.u26", "FDC.u27"]})}))
make_zip("zips_bios/fdc.zip", [("fdc.u26", fdc[0]), ("fdc.u27", fdc[1])])

# A part missing; the parts in the wrong order; and a part that is damaged.
bios.append(bios_entry("b_joinmissing", serve("bios/fdc2.zip", zip_bytes(
    [("fdc.u26", fdc[0])])), "/bios/missing",
    {"disk.bin": (fdc[0] + fdc[1], {"parts": ["fdc.u26", "fdc.u27"]})}))
bios.append(bios_entry("b_joinbad", serve("bios/fdc3.zip", zip_bytes(
    [("fdc.u26", fdc[0]), ("fdc.u27", fdc[1])])), "/bios/bad",
    {"disk.bin": (fdc[0] + fdc[1], {"parts": ["fdc.u27", "fdc.u26"]})}))
damaged = make_zip("srv/bios/fdc4.zip", [("fdc.u26", fdc[0]), ("fdc.u27", fdc[1])],
                   zipfile.ZIP_STORED)
corrupt_entry(damaged, "fdc.u27")
bios.append(bios_entry("b_joindamaged", f"{URL}/bios/fdc4.zip", "/bios/damaged",
                       {"disk.bin": (fdc[0] + fdc[1], {"parts": ["fdc.u26", "fdc.u27"]})}))

# "offset" and "parts" only mean something in "bios": in "data" they are
# ignored, as older copies of updateAll ignore them everywhere.
gdata = data_entry("g_offset", serve("bios/g.bin", boot), "/roms/G", {"g.bin": boot})
gdata["files"][0]["offset"] = "0x200"

with open(os.path.join(OUT, "cfg_bios.json"), "w") as f:
    json.dump({"databases": [f"{URL}/db.json.zip"], "cores": [remote[0]], "data": [gdata],
               "bios": bios}, f, indent=2)

# A card with one part of b_slice on it, and the other wrong.
write("card_bios_partial/bios/rom.bin", boot[0x200:0x600])
write("card_bios_partial/bios/grom.bin", b"edited")

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
bad("config_url", {"config_url": "ftp://x/updateAll.json", "cores": [good]})
bad("config_url_type", {"config_url": ["http://x"], "cores": [good]})

good_data = data_entry("d", "http://x/d.zip", "/roms/D", {"base/d1": b"d"})
bad("data_type", {"cores": [good], "data": {"d": good_data}})
bad("data_nourl", {"cores": [good], "data": [{k: v for k, v in good_data.items() if k != "url"}]})
bad("data_url", {"cores": [good], "data": [dict(good_data, url="ftp://x/d.zip")]})
bad("data_url_name", {"cores": [good], "data": [dict(good_data, url="http://x/dir/")]})
bad("data_name", {"cores": [good], "data": [dict(good_data, files=[{"name": "a/../b", "size": 1,
                                                                    "crc": "0"}])]})
bad("data_archives", {"cores": [good], "data": [dict(good_data, archives=["a/b.zip"])]})
bad("data_dup_id", {"cores": [good], "data": [dict(good_data, id="OK")]})
bad("data_slash_in_core", {"cores": [dict(good, files=[{"name": "a/b", "size": 1, "crc": "0"}])]})
bad("data_uf2", {"cores": [good], "data": [dict(good_data, uf2={"address": "xyz", "family": "0"})]})
bad("data_uf2_type", {"cores": [good], "data": [dict(good_data, uf2="0x10200000")]})
bad("data_uf2_nofamily", {"cores": [good], "data": [dict(good_data, uf2={"address": "0x10200000"})]})
bad("data_uf2_big", {"cores": [good], "data": [dict(good_data, uf2={"address": "100000000",
                                                                    "family": "0"})]})

good_bios = data_entry("b", "http://x/b.bin", "/bios", {"b.bin": b"b"})


def bios_file(**keys):
    return dict(good_bios, files=[dict(good_bios["files"][0], **keys)])


bad("bios_type", {"cores": [good], "bios": {"b": good_bios}})
bad("bios_reserved", {"cores": [dict(good, id="Bios")]})
bad("bios_reserved_data", {"cores": [good], "data": [dict(good_data, id="bios")]})
bad("bios_dup_id", {"cores": [good], "data": [good_data], "bios": [dict(good_bios, id="D")]})
bad("bios_offset", {"cores": [good], "bios": [bios_file(offset="xyz")]})
bad("bios_offset_type", {"cores": [good], "bios": [bios_file(offset=16)]})
bad("bios_offset_null", {"cores": [good], "bios": [bios_file(offset=None)]})
bad("bios_parts_type", {"cores": [good], "bios": [bios_file(parts="a.u1")]})
bad("bios_parts_empty", {"cores": [good], "bios": [bios_file(parts=[])]})
bad("bios_parts_name", {"cores": [good], "bios": [bios_file(parts=["a.u1", "sub/b.u2"])]})
bad("bios_parts_item", {"cores": [good], "bios": [bios_file(parts=["a.u1", 2])]})
bad("bios_offset_parts", {"cores": [good], "bios": [dict(good_bios, files=[
    {"name": "x", "size": 1, "crc": "0", "offset": "0"},
    {"name": "y", "size": 1, "crc": "0", "parts": ["a"]}])]})
bad("bios_offset_uf2", {"cores": [good], "bios": [dict(bios_file(offset="0"),
                                                      uf2={"address": "0", "family": "0"})]})
bad("bios_parts_uf2", {"cores": [good], "bios": [dict(bios_file(parts=["a"]),
                                                     uf2={"address": "0", "family": "0"})]})
bad("bios_slash", {"cores": [good], "bios": [bios_file(name="sub/../b.bin")]})
bad("sdcard_type", {"cores": [good], "sdcard": "http://x"})
bad("sdcard_name", {"cores": [good], "sdcard": {"releases_url": "http://x", "asset": "a.zip"}})
bad("sdcard_url", {"cores": [good], "sdcard": {"name": "A", "releases_url": "ftp://x", "asset": "a.zip"}})
bad("sdcard_asset", {"cores": [good], "sdcard": {"name": "A", "releases_url": "http://x", "asset": "a/b.zip"}})
bad("sdcard_asset_zip", {"cores": [good], "sdcard": {"name": "A", "releases_url": "http://x", "asset": "a.tar"}})
bad("sdcard_reserved", {"cores": [dict(good, id="SDCard")]})
bad("sdcard_reserved_data", {"cores": [good], "data": [dict(good_data, id="sdcard")]})


# ---------------------------------------------------------------------------
# The SD-card archive
# ---------------------------------------------------------------------------

ASSET = "pico-bootLoader_sdcard.zip"
REL_SAME = blob(900)        # on most cards as it is in the archive
REL_NEW = blob(1200)
REL_LIST = b"piconesPlus;nes;NES\npicoNew;new;New\n"
REL_VERSIONS = b"piconesPlus;pico-infonesPlus;v0.55;abc\n"
REL_JSON = b'{"config_url": "http://x/new.json", "cores": []}\n'
REL_FILES = [("emu/8/new.uf2", REL_NEW), ("emu/8/same.uf2", REL_SAME),
             ("emu/emulators.txt", REL_LIST), ("emu/versions.txt", REL_VERSIONS),
             ("updateAll/updateAll.json", REL_JSON), ("updateAll/updateAll.py", b"# new\n")]
# Folder entries, as zip -r writes them, and a marker that must not be installed.
REL_ZIP = zip_bytes([("emu/", b""), ("emu/8/", b""), ("updateAll/", b""),
                     ("emu/release.txt", b"v0.0\n")] + REL_FILES)


def release(scenario, tag, data=REL_ZIP, name=ASSET, url=None, size=None, **extra):
    """One release as the GitHub API lists it, with its archive on the server."""
    if url is None:
        url = serve(f"rel/{scenario}/{tag}/{name}", data)
    asset = {"name": name, "state": "uploaded", "size": len(data) if size is None else size,
             "browser_download_url": url}
    other = {"name": "board.uf2", "size": 1, "browser_download_url": f"{URL}/rel/board.uf2"}
    return dict({"tag_name": tag, "draft": False, "prerelease": False,
                 "html_url": f"{URL}/rel/tag/{tag}", "assets": [other, asset]}, **extra)


def rel_cfg(scenario, releases, asset=ASSET, url=None, program=None):
    """cfg_rel_<scenario>.json, with one more set, and its answer on the server.
    The server ignores the query, as it must for GitHub's per_page."""
    if url is None:
        text = releases if isinstance(releases, str) else json.dumps(releases)
        url = serve(f"api/{scenario}.json", text.encode()) + "?per_page=10"
    sdcard = {"name": "SD card files", "releases_url": url, "asset": asset}
    if program is not None:
        sdcard["program_asset"] = program
    write(f"cfg_rel_{scenario}.json", json.dumps({
        "cores": local[:1], "databases": [], "sdcard": sdcard}).encode())


def rel_card(name, marker=b"v0.9\n", files=(), edited=b"edited list\n"):
    if marker is not None:
        write(f"{name}/emu/release.txt", marker)
    write(f"{name}/emu/emulators.txt", edited)
    write(f"{name}/emu/8/same.uf2", REL_SAME)
    write(f"{name}/emu/8/own.uf2", b"the user's own")
    write(f"{name}/boot.txt", b"GUI=1\n")
    for path, data in files:
        write(f"{name}/{path}", data)


rel_cfg("main", [release("main", "v0.9"), release("main", "v0.10"),
                 release("main", "v0.11-alpha")])
rel_cfg("patch", [release("patch", "v0.10"), release("patch", "v0.10.1")])
rel_cfg("gone", [release("gone", "v0.10", url=f"{URL}/rel/gone/nothing-here.zip")])
rel_cfg("filter", [
    release("filter", "v0.20", draft=True), release("filter", "v0.19", prerelease=True),
    5, "not a release", {"tag_name": "v0.30"}, release("filter", "v1"), release("filter", "0.18"),
    release("filter", "v0.17-alpha"), release("filter", "v0.16.1.2"), release("filter", "V0.15"),
    release("filter", "v0.14", size="big"), release("filter", "v0.13", url="ftp://x/a.zip"),
    release("filter", "v0.12", html_url=None),
    dict(release("filter", "v0.11"), assets=[{"name": ASSET, "state": "new", "size": 5,
                                              "browser_download_url": f"{URL}/x.zip"}]),
    release("filter", "v0.9"), release("filter", "v0.10"), release("filter", "v0.8")])
rel_cfg("fwonly", [dict(release("fwonly", "v0.12"), assets=[]), release("fwonly", "v0.10")])
rel_cfg("check404", None, url=f"{URL}/api/no-such-answer.json")
rel_cfg("broken", "{not json")
rel_cfg("notlist", '{"message": "API rate limit exceeded"}')
rel_cfg("noasset", [dict(release("noasset", "v0.10"), assets=[])])

for scenario, name in (("slip", "../evil.txt"), ("abs", "/abs.txt"), ("back", "emu\\evil.txt"),
                       ("drive", "c:evil.txt"), ("ascii", "emu/café.txt"), ("dots", "emu/./x")):
    rel_cfg(scenario, [release(scenario, "v0.10", zip_bytes(REL_FILES + [(name, b"evil")]))])

rel_cfg("badsize", [release("badsize", "v0.10", size=len(REL_ZIP) + 1)])
rel_cfg("notzip", [release("notzip", "v0.10", b"this is no zip at all")])
rel_cfg("assetgone", [release("assetgone", "v0.10", url=f"{URL}/rel/nothing-here.zip")])

# A damaged entry: the others are installed, and the marker must not be.
rel_damaged = make_zip("rel_damaged.zip", REL_FILES, zipfile.ZIP_STORED)
corrupt_entry(rel_damaged, "emu/8/new.uf2")
with open(rel_damaged, "rb") as f:
    rel_cfg("damaged", [release("damaged", "v0.10", f.read())])

# Kept in the zip folder: nothing to download, so the URL need not work. And one
# of the wrong size, from a release that was cut again, which is downloaded again.
make_zip("zips/kept_sdcard-v0.10.zip", REL_FILES)
with open(os.path.join(OUT, "zips/kept_sdcard-v0.10.zip"), "rb") as f:
    rel_cfg("kept", [release("kept", "v0.10", f.read(), name="kept_sdcard.zip",
                             url=f"{URL}/rel/nothing-here.zip")], asset="kept_sdcard.zip")
make_zip("zips/stale_sdcard-v0.10.zip", [("emu/8/old.uf2", b"from the first cut")])
rel_cfg("stale", [release("stale", "v0.10", name="stale_sdcard.zip")], asset="stale_sdcard.zip")

# The key arrives with the configuration at config_url.
rel_remote = serve("cfg_rel_remote.json", json.dumps({
    "cores": local[:1], "databases": [],
    "sdcard": {"name": "SD card files", "asset": ASSET,
               "releases_url": serve("api/remote.json", json.dumps(
                   [release("remote", "v0.10")]).encode())}}).encode())
write("cfg_rel_fetch.json", json.dumps({"config_url": rel_remote, "cores": local[:1]}).encode())

rel_card("card_rel_v09")
rel_card("card_rel_v08", b"v0.8\n")
rel_card("card_rel_v010", b"v0.10\n")
rel_card("card_rel_crlf", b"  v0.9 \r\nanything else\n")
rel_card("card_rel_garbage", b"not a tag\n")
rel_card("card_rel_future", b"v0.99\n")
rel_card("card_rel_empty_marker", b"")
# Everything the archive holds is on it already, but nothing says so.
rel_card("card_rel_nomarker", None, REL_FILES, REL_LIST)
rel_card("card_rel_bak", files=[("emu/emulators.txt.bak", b"edits saved earlier\n")])
rel_card("card_rel_bak_same", files=[("emu/emulators.txt.bak", b"edited list\n")])
# updateAll.json without a config_url is the user's own choice.
rel_card("card_rel_json_own", files=[("updateAll/updateAll.json", b'{"databases": [], "cores": []}\n')])
rel_card("card_rel_json_bad", files=[("updateAll/updateAll.json", b'{"edited": \n')])
rel_card("card_rel_json_online", files=[("updateAll/updateAll.json",
                                         b'{"config_url": "http://x/old.json", "cores": []}\n')])


# ---------------------------------------------------------------------------
# updateAll.exe finding and downloading a newer copy of itself
# ---------------------------------------------------------------------------

PROGRAM = "updateAll-*.exe"
FAKE_BODY = blob(300)


def fake_exe(title):
    """As much of a program as the check looks at: its header and its window title."""
    return b"MZ" + FAKE_BODY + title.encode("utf-16-le") + b"\0\0" + FAKE_BODY


write("fake_exe_v0.10.bin", fake_exe("updateAll v0.10"))
write("fake_exe_v1.5.bin", fake_exe("updateAll v1.5"))


def with_program(scenario, tag, data=None, archive=False, url=None, size=None, version=None, name=None, **extra):
    """A release holding the program, and the archive when asked for. The program's
    version is the release's tag unless version says otherwise."""
    version = version or tag
    name = name or f"updateAll-{version}.exe"
    data = fake_exe(f"updateAll {version}") if data is None else data
    if url is None:
        url = serve(f"rel/{scenario}/{tag}/{name}", data)
    r = release(scenario, tag, **extra)
    exe = {"name": name, "state": "uploaded", "size": len(data) if size is None else size,
           "browser_download_url": url}
    r["assets"] = (r["assets"] if archive else r["assets"][:1]) + [exe]
    return r


# Selection: the program may be older than the newest archive, or the only asset.
rel_cfg("prog_older", [release("prog_older", "v0.10"),
                       with_program("prog_older", "v0.9", archive=True)], program=PROGRAM)
rel_cfg("prog_alone", [with_program("prog_alone", "v0.10")], program=PROGRAM)
rel_cfg("prog_filter", [
    with_program("prog_filter", "v0.20", draft=True), with_program("prog_filter", "v0.19", prerelease=True),
    with_program("prog_filter", "v0.18-alpha"), with_program("prog_filter", "v0.16", size="big"),
    dict(with_program("prog_filter", "v0.15"), assets=[
        {"name": "updateAll-v0.15.exe", "state": "new", "size": 5, "browser_download_url": f"{URL}/x.exe"}]),
    with_program("prog_filter", "v0.9"), with_program("prog_filter", "v0.12"),
    with_program("prog_filter", "v0.8")], program=PROGRAM)
rel_cfg("prog_none", [release("prog_none", "v0.10")], program=PROGRAM)
rel_cfg("prog_nokey", [with_program("prog_nokey", "v0.10", archive=True)])
rel_cfg("prog_404", None, url=f"{URL}/api/no-such-answer.json", program=PROGRAM)
rel_cfg("prog_badname", [with_program("prog_badname", "v0.10", archive=True)], program="a/*.exe")
rel_cfg("prog_notexe", [with_program("prog_notexe", "v0.10", archive=True)], program="updateAll-*.zip")
rel_cfg("prog_nostar", [with_program("prog_nostar", "v0.10", archive=True)], program="updateAll.exe")
rel_cfg("prog_twostars", [with_program("prog_twostars", "v0.10", archive=True)], program="updateAll-*-*.exe")
# The version is the program's own, whatever the tag of the release holding it.
rel_cfg("prog_separate", [with_program("prog_separate", "v0.10", version="v1.5", archive=True)], program=PROGRAM)
rel_cfg("prog_highest", [with_program("prog_highest", "v0.12", version="v1.1"),
                         with_program("prog_highest", "v0.11", version="v1.2"),
                         with_program("prog_highest", "v0.10", version="v1.0")], program=PROGRAM)
rel_cfg("prog_oldname", [with_program("prog_oldname", "v0.10", name="updateAll.exe")], program=PROGRAM)
rel_cfg("prog_badversion", [
    with_program("prog_badversion", "v0.14", name="updateAll-1.5.exe"),
    with_program("prog_badversion", "v0.13", name="updateAll-v1.5-dirty.exe"),
    with_program("prog_badversion", "v0.12", name="updateAll-.exe"),
    with_program("prog_badversion", "v0.11", name="updateAll-v1.exe"),
    with_program("prog_badversion", "v0.10", name="updateall-v1.5.exe")], program=PROGRAM)
write("cfg_rel_prog_badtype.json", json.dumps({
    "cores": local[:1], "databases": [],
    "sdcard": {"name": "SD card files", "asset": ASSET, "program_asset": 5,
               "releases_url": serve("api/prog_badtype.json", json.dumps(
                   [with_program("prog_badtype", "v0.10", archive=True)]).encode())}}).encode())
rel_cfg("prog_key", [with_program("prog_key", "v0.10", archive=True)], program=PROGRAM)

# Download.
rel_cfg("prog_ok", [with_program("prog_ok", "v0.10", archive=True)], program=PROGRAM)
rel_cfg("prog_size", [with_program("prog_size", "v0.10", size=len(fake_exe("updateAll v0.10")) + 1)],
        program=PROGRAM)
rel_cfg("prog_mz", [with_program("prog_mz", "v0.10", b"PE" + fake_exe("updateAll v0.10"))], program=PROGRAM)
rel_cfg("prog_title", [with_program("prog_title", "v0.10", fake_exe("updateAll v0.9"))], program=PROGRAM)
rel_cfg("prog_longer", [with_program("prog_longer", "v0.10", fake_exe("updateAll v0.10.1"))], program=PROGRAM)
rel_cfg("prog_gone", [with_program("prog_gone", "v0.10", url=f"{URL}/rel/nothing-here.exe")], program=PROGRAM)
rel_cfg("prog_sep_ok", [with_program("prog_sep_ok", "v0.10", version="v1.5")], program=PROGRAM)
rel_cfg("prog_sep_title", [with_program("prog_sep_title", "v0.10", fake_exe("updateAll v0.10"), version="v1.5")],
        program=PROGRAM)
