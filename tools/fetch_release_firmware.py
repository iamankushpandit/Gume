#!/usr/bin/env python3
"""Fetch published release binaries so the installer can offer older versions.

    python tools/fetch_release_firmware.py                 # last few releases
    python tools/fetch_release_firmware.py --keep 8
    python tools/fetch_release_firmware.py --tags v5.4.0 v5.3.0

Writes into `site/_releases/` (not committed):

    <version>/<board>/<env>/{bootloader,partitions,boot_app0,firmware}.bin
    index.json      what was fetched, which gen_site.py reads

Why the page needs this at all
------------------------------
The installer offered exactly one firmware: whatever `main` last built. When
5.5.0 shipped a defect that made the two 2.8-inch boards untouchable, every
visitor to the page was handed that build and nothing else -- the maintainer's
own words were "I am locked out of the previous releases from web flasher". A
page that can only ever install the newest thing has no way back from a bad
release, which is precisely the moment somebody needs one.

Why the binaries are copied rather than linked
----------------------------------------------
The obvious implementation is a manifest pointing straight at
`github.com/<owner>/<repo>/releases/download/<tag>/<asset>`. It does not work.
esp-web-tools reads the parts with `fetch()` from the Pages origin, and GitHub's
release asset host sends **no `Access-Control-Allow-Origin` header** at either
hop -- measured, not assumed. The browser blocks the read, and it blocks it
*after* the user has connected their board and the flash has begun, which is the
worst place to discover it.

So the assets are downloaded here and served from the Pages origin, where
same-origin rules make the question go away. The important property is kept:
**nothing is rebuilt.** The bytes served for 5.4.0 are the bytes that were
published as 5.4.0, so an old version cannot quietly become a new build of old
source, and a tag that no longer compiles on today's toolchain is still
installable.

What it deliberately does not do
--------------------------------
It does not invent a version. A release missing any of the four parts for a
board/env is skipped for that combination rather than half-published -- an older
release predates the boards added after it, and "this board did not exist yet"
is the honest answer, not a manifest with a hole in it. Draft and prerelease
tags are ignored, and so is any tag whose assets fail the size sanity check.
"""

import argparse
import json
import os
import re
import shutil
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_site import VARIANTS  # noqa: E402  -- the offered environments, once

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Only the environments the picker actually offers. Read from gen_site rather
# than restated, for the reason everything else on this page is derived: a
# second list is a list that drifts. It also keeps the download honest -- the
# diagnostic builds are on every release and nobody can install them from the
# page, so fetching them would be ~40 MB of Pages artifact nobody can reach.
OFFERED_ENVS = frozenset(entry["env"] for entry in VARIANTS)
CACHE = os.path.join(ROOT, "site", "_releases")

# The four parts, and the smallest plausible size for each. A truncated or
# error-page download is otherwise indistinguishable from a real one until the
# flash fails on somebody's desk.
PARTS = (
    ("bootloader.bin", 1024),
    ("partitions.bin", 512),
    ("boot_app0.bin", 512),
    ("firmware.bin", 512 * 1024),
)

# pack_release.py names every asset braino-<version>-<board>-<env>-<part>.bin,
# and none of those four fields contains a hyphen -- board and env ids use
# underscores (esp32_2432s028_st7789), and the version is plain semver. So the
# split is unambiguous. `-merged.bin` is not matched on purpose: it is the
# convenience image for a terminal flash at 0x0, and esp-web-tools wants the
# four parts at their own offsets.
ASSET_RE = re.compile(
    r"^braino-(?P<version>[^-]+)-(?P<board>[^-]+)-(?P<env>[^-]+)"
    r"-(?P<part>bootloader|partitions|boot_app0|firmware)\.bin$")

SEMVER_RE = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)$")


def die(message):
    sys.stderr.write("fetch_release_firmware: %s\n" % message)
    sys.exit(1)


def api(url):
    request = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "User-Agent": "gume-fetch-release-firmware",
    })
    # CI has a token and gets 1000 requests an hour; a local run without one
    # gets 60, which is still plenty for a handful of releases.
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        request.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def download(url, dest):
    request = urllib.request.Request(url, headers={
        "Accept": "application/octet-stream",
        "User-Agent": "gume-fetch-release-firmware",
    })
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        request.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(request, timeout=300) as response:
        data = response.read()
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as handle:
        handle.write(data)
    return len(data)


def releases(repo, keep, wanted_tags):
    """Published releases, newest first, tags that look like versions only."""
    found = []
    for page in range(1, 6):
        batch = api("https://api.github.com/repos/%s/releases?per_page=100&page=%d"
                    % (repo, page))
        if not batch:
            break
        found.extend(batch)
        if len(batch) < 100:
            break

    out = []
    for release in found:
        tag = release.get("tag_name") or ""
        if release.get("draft") or release.get("prerelease"):
            continue
        if not SEMVER_RE.match(tag):
            continue
        if wanted_tags and tag not in wanted_tags:
            continue
        out.append(release)

    out.sort(key=lambda r: [int(n) for n in SEMVER_RE.match(r["tag_name"]).groups()],
             reverse=True)
    return out if wanted_tags else out[:keep]


def group_assets(release):
    """{version: {board: {env: {part: url}}}} for one release."""
    tree = {}
    for asset in release.get("assets", []):
        match = ASSET_RE.match(asset["name"])
        if not match:
            continue
        board = match.group("board")
        env = match.group("env")
        tree.setdefault(board, {}).setdefault(env, {})[match.group("part") + ".bin"] = (
            asset["browser_download_url"], asset.get("size", 0))
    return tree


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default="iamankushpandit/Gume")
    parser.add_argument("--keep", type=int, default=6,
                        help="how many recent releases to offer (default 6)")
    parser.add_argument("--tags", nargs="*", default=None,
                        help="fetch only these tags")
    parser.add_argument("--out", default=CACHE)
    parser.add_argument("--clean", action="store_true",
                        help="discard the cache first")
    args = parser.parse_args()

    if args.clean and os.path.isdir(args.out):
        shutil.rmtree(args.out)

    try:
        found = releases(args.repo, args.keep, set(args.tags or []))
    except urllib.error.HTTPError as error:
        # A rate limit or an outage must not fail the whole Pages build: the
        # current firmware is generated from the tree and does not depend on
        # any of this. Older versions are a recovery path, not the product.
        sys.stderr.write("fetch_release_firmware: GitHub API said %s -- "
                         "continuing with no older versions.\n" % error)
        found = []

    index = {"repo": args.repo, "versions": []}
    for release in found:
        version = release["tag_name"].lstrip("v")
        tree = group_assets(release)
        entry = {"version": version, "tag": release["tag_name"], "boards": {}}

        for board, envs in sorted(tree.items()):
            for env, parts in sorted(envs.items()):
                if env not in OFFERED_ENVS:
                    continue
                missing = [name for name, _ in PARTS if name not in parts]
                if missing:
                    # An older release predates boards added after it. That is
                    # a fact about history, not a fault -- skip quietly.
                    continue

                ok = True
                for name, floor in PARTS:
                    url, size = parts[name]
                    dest = os.path.join(args.out, version, board, env, name)
                    if os.path.exists(dest) and os.path.getsize(dest) >= floor:
                        continue
                    try:
                        got = download(url, dest)
                    except (urllib.error.HTTPError, urllib.error.URLError) as error:
                        sys.stderr.write("  %s %s/%s %s: %s\n"
                                         % (version, board, env, name, error))
                        ok = False
                        break
                    if got < floor:
                        sys.stderr.write("  %s %s/%s %s: only %d bytes -- "
                                         "refusing to publish it\n"
                                         % (version, board, env, name, got))
                        ok = False
                        break
                if ok:
                    entry["boards"].setdefault(board, []).append(env)

        if entry["boards"]:
            index["versions"].append(entry)
            print("  %-8s %s" % (version, ", ".join(
                "%s:%d" % (b, len(e)) for b, e in sorted(entry["boards"].items()))))

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "index.json"), "w", encoding="utf-8") as handle:
        json.dump(index, handle, indent=2)
        handle.write("\n")

    print("Cached %d older release(s) in %s." % (len(index["versions"]), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
