#!/usr/bin/env python3
"""
Runs updateAll.py and ua_host (the core of updateAll.exe) on the same cases
and checks that they print the same, exit with the same status and leave the
same files behind. Downloads come from a local HTTP server.

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
]
CASES += [(f"bad-config-{n}", f"--config bad_{n}.json --zips {{zips}} --sd {{card}}", None)
          for n in ("syntax", "nocores", "missing_key", "twice", "dest", "dest_empty", "nozips",
                    "zipname", "filename", "crc", "size", "databases")]

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
                        os.path.join(fx, "zips"), "--sd", full], cwd=fx, env=env,
                       stdout=subprocess.DEVNULL)

        failures = 0
        cases = [c for c in CASES if not args.k or args.k in c[0]]
        for name, argline, card in cases:
            results = []
            for tool in ([sys.executable, SCRIPT], [HOST]):
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
                start = time.time()
                p = subprocess.run(tool + argv, cwd=fx, env=env, capture_output=True, text=True)
                results.append({"rc": p.returncode, "out": normalise(p.stdout),
                                "err": normalise(p.stderr), "tree": tree(run_dir),
                                "time": time.time() - start})
            py, c = results
            diffs = []
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
        print(f"\n{len(cases) - failures} of {len(cases)} cases agree")
        return 1 if failures else 0
    finally:
        server.terminate()
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
