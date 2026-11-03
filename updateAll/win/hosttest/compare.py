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
                    "bios_offset_parts", "bios_offset_uf2", "bios_parts_uf2", "bios_slash")]

# ua_host only, stopped after this many calls of on_bytes: (name, arguments, calls,
# the state the file it stopped on went through). Python has no way to cancel.
CANCEL_CASES = [
    ("cancel-download", "--config cfg_db.json --zips {zips} --sd {card} --core theta", 1, "Downloading"),
    ("cancel-install", "--config cfg_local.json --zips {zips} --sd {card} --core alpha", 1, "Installing"),
]
# Run without the hooks as well; they must not change anything.
NO_HOOK_CASES = ("local", "db-http", "data-uf2-local", "bios-http")

# The states of ua_core.h.
DOWNLOADING, BUSY, DONE, NOTE, FAILED, CANCELLED = range(6)
REPORT_HEADERS = ("Sets from:", "Zips from:", "SD card:", "Databases:", "Dry run:", "ERROR:")

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

        def run_case(tool, argline, card, extra_env=None):
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
            start = time.time()
            p = subprocess.run(tool + argv, cwd=fx, env=dict(env, **(extra_env or {})),
                               capture_output=True, text=True)
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
            failures += bool(problems)
            print(f"{'FAIL' if problems else 'ok':4} {name:22} exit {c['rc']}")
            for p in problems:
                print("     " + p)
            if args.v or problems:
                for r in rows:
                    print(f"     {r['set']}/{r['file']}: {r['details']} {r['states']}")

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
