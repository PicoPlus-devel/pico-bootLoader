#!/usr/bin/env python3
"""
Checks the version of the updateAll program in updateAll/win/VERSION against the
highest version that a release already holds, and says whether this release
attaches the program:

    no release holds the program                  attach it
    VERSION is higher                             attach it
    VERSION is the same, program unchanged        attach it again
    VERSION is the same, program changed          stop: VERSION was not raised
    VERSION is lower                              stop

Users are offered an update only when a release holds a higher version, so a
change without a new version would never reach them.

Every release attaches the program, so that a new user finds it on the latest
release. A copy of the same version does no harm: it is not offered to anyone.
When the list of releases or the sources of that release cannot be read, this says
so and attaches the program as well.

Usage: updateall_version.py <tag being released>

The release workflow runs this before it builds, and build_emulators.sh -z -r
before it builds anything. In the workflow, the outcome goes to GITHUB_OUTPUT as
attach=true|false and version=<version>.
"""

import json
import os
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROGRAM = "updateAll/win"
VERSION_FORM = r"v[0-9]{1,9}(?:\.[0-9]{1,9}){1,2}"
ASSET = re.compile(rf"updateAll-({VERSION_FORM})\.exe")


def key(version):
    numbers = [int(n) for n in version[1:].split(".")]
    return numbers + [0] * (3 - len(numbers))


def report(kind, text):
    """A line in the log, which the workflow's page also shows as a warning or error."""
    if os.environ.get("GITHUB_ACTIONS"):
        print(f"::{kind}::{text}")
    else:
        print(f"{kind.upper()}: {text}", file=sys.stderr)


def repository():
    repo = os.environ.get("GITHUB_REPOSITORY")
    if repo:
        return repo
    config = json.loads((ROOT / "updateAll/updateAll.json").read_text(encoding="utf-8"))
    return re.search(r"/repos/([^/]+/[^/]+)/releases", config["sdcard"]["releases_url"])[1]


def read_releases(repo):
    req = urllib.request.Request(f"https://api.github.com/repos/{repo}/releases?per_page=100",
                                 headers={"User-Agent": "pico-bootLoader release workflow"})
    if os.environ.get("GITHUB_TOKEN"):
        req.add_header("Authorization", f"Bearer {os.environ['GITHUB_TOKEN']}")
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def newest_copy(releases, tag):
    """The highest version of the program on a release, and that release's tag, or
    None. This tag does not count, as a release can be cut again; a draft, a
    prerelease and a dry run do not either, as they are offered to nobody."""
    best = None
    for rel in releases:
        name = rel["tag_name"]
        if rel["draft"] or rel["prerelease"] or name == tag or not re.fullmatch(VERSION_FORM, name):
            continue
        for asset in rel["assets"]:
            found = ASSET.fullmatch(asset["name"])
            if found and (best is None or key(found[1]) > key(best[0])):
                best = (found[1], name)
    return best


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)


def program_changed(base):
    """Whether the program being built differs from that of a release tag, or None
    when this cannot be told. Compares with the files on disk, which are built."""
    ref = f"refs/tags/{base}"
    if git("rev-parse", "-q", "--verify", ref + "^{commit}").returncode:
        # --depth only where the clone is shallow already: it would make a full one so.
        shallow = git("rev-parse", "--is-shallow-repository").stdout.strip() == "true"
        depth = ["--depth=1"] if shallow else []
        if git("fetch", "-q", *depth, "origin", f"{ref}:{ref}").returncode:
            return None
    diff = git("diff", "--quiet", ref, "--", PROGRAM,
               f":(exclude){PROGRAM}/hosttest", f":(exclude){PROGRAM}/VERSION")
    return {0: False, 1: True}.get(diff.returncode)


def finish(version, attach):
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as f:
            f.write(f"attach={'true' if attach else 'false'}\nversion={version}\n")
    return 0


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    tag = sys.argv[1]
    version = (ROOT / PROGRAM / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(VERSION_FORM, version):
        report("error", f"{PROGRAM}/VERSION must hold a version such as v1.0 or v1.0.1, not '{version}'")
        return 1

    try:
        newest = newest_copy(read_releases(repository()), tag)
    except (OSError, ValueError, KeyError, TypeError) as e:
        report("warning", f"cannot read the list of releases ({e}): attaching updateAll {version}")
        return finish(version, True)

    if newest is None:
        print(f"updateAll {version}: no release holds the program yet, so this one attaches it")
        return finish(version, True)
    held, held_by = newest
    if key(version) > key(held):
        print(f"updateAll {version}: higher than {held} on release {held_by}, so this release attaches it")
        return finish(version, True)
    if key(version) < key(held):
        report("error", f"{PROGRAM}/VERSION is {version}, lower than {held} on release {held_by}")
        return 1
    changed = program_changed(held_by)
    if changed is None:
        report("warning", f"cannot compare {PROGRAM} with release {held_by}: attaching updateAll {version}")
        return finish(version, True)
    if changed:
        report("error", f"{PROGRAM} changed since release {held_by}, which holds updateAll {held}, "
                        f"but {PROGRAM}/VERSION is still {version}. Raise it, and commit that.")
        return 1
    print(f"updateAll {version}: unchanged since release {held_by}; this release attaches it again, "
          "which offers no copy an update")
    return finish(version, True)


if __name__ == "__main__":
    sys.exit(main())
