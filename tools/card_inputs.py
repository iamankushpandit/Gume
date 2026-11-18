# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""What the RG40XX V card image is built from, and which part of it is the OS.

The card image (host/rg40xxv/) is two layers with very different costs:

- the OS -- a cross toolchain, glibc, the kernel, U-Boot, busybox and every
  library -- which takes about an hour to build from source, and
- Braino and the few files every build applies afresh, which take minutes.

CI keeps the first layer between runs as an "OS base" (host/rg40xxv/os-base.sh)
and builds only the second on top of it. That is only correct while the base
was built from the same OS inputs as the commit being built, so the base is
named after a hash of exactly those inputs: the key this module prints. A
change to any OS input changes the key, finds no base, and builds everything;
a change to anything else finds the base and takes minutes.

This file is the ONE place that says which is which. os-base.sh asks it for
the key and for the files to date before an unpacked base; card-image.yml asks
os-base.sh; tools/check_card_inputs.py holds the workflow's trigger paths and
its Buildroot version to what is written here. Nothing else may keep a list.

Every file under host/rg40xxv/ is one of four kinds, by the first rule in
RULES that matches it:

  os           Its contents go into the key. The default, so a file nobody
               has classified forces a full build rather than being missed.
  overlay      The root filesystem overlay. Buildroot copies it into the
               target on every build, so a change to what a file SAYS is
               picked up on a base -- but a file REMOVED from the overlay
               would survive in the base's target/, and a file added may need
               an OS change beside it. So the overlay's NAMES go into the key
               and its contents do not: editing an init script takes minutes,
               adding or removing one is a full build.
  every-build  Read afresh by every build and writing only into images/,
               which post-image regenerates: Braino's own package (its
               version is the commit, so every commit is a new build
               directory), post-image.sh, the boot menu and the card layout.
  not-built    Not part of the image at all.

post-build.sh is deliberately `os`: it DELETES from the target (firmware
blobs, init scripts), and a base's target/ has already lost whatever it
deleted, so a post-build.sh that stopped deleting something would build a
different image on a base than from source.

Outside host/rg40xxv/ the image also reads the whole of Braino's source tree
(package/braino copies the repository) and host/consoles/ (post-build.sh
installs them, clearing the directory first). None of that is OS: it is what
TRIGGER_PATHS is for -- the paths whose change makes the card worth
rebuilding at all.

    python3 tools/card_inputs.py key            # the OS base's name
    python3 tools/card_inputs.py list [KIND]    # files of one kind (default os)
    python3 tools/card_inputs.py explain        # every file and its kind
    python3 tools/card_inputs.py needs PATH...  # "full" or "base" for a change
"""

import argparse
import fnmatch
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARD = os.path.join(ROOT, "host", "rg40xxv")
CARD_PREFIX = "host/rg40xxv/"

OS = "os"
OVERLAY = "overlay"
EVERY_BUILD = "every-build"
NOT_BUILT = "not-built"
KINDS = (OS, OVERLAY, EVERY_BUILD, NOT_BUILT)

# Bump to force every run to build in full once, for a reason the key cannot
# see -- a Buildroot download that changed under the same name, say.
BASE_EPOCH = 2

# First match wins; patterns are fnmatch, relative to host/rg40xxv/, and `*`
# crosses directories. Anything no rule names is OS.
RULES = (
    ("README.md", NOT_BUILT),
    ("write-card.ps1", NOT_BUILT),
    ("package/braino/*", EVERY_BUILD),
    ("board/overlay/*", OVERLAY),
    ("board/post-image.sh", EVERY_BUILD),
    ("board/extlinux.conf", EVERY_BUILD),
    ("board/genimage.cfg", EVERY_BUILD),
    ("*", OS),
)

# What card-image.yml's `paths:` filters must say, for both pull requests and
# pushes, in this order. Everything the image is built from, and nothing else:
# a change to the README or to an ESP32 board does not start an image build.
#   host/**                  the card's own tree, and Braino's PC sources
#   src/**, include/**       Braino
#   tools/braino-badge.svg   the product mark host/CMakeLists.txt embeds
#   tools/card_inputs.py     this file: which runs are full is decided here
#   LICENSE, NOTICE.md       packed into the image's legal-info
# and not the Markdown under host/, nor the muOS packer, which the image
# never reads.
TRIGGER_PATHS = (
    "host/**",
    "src/**",
    "include/**",
    "tools/braino-badge.svg",
    "tools/card_inputs.py",
    "LICENSE",
    "NOTICE.md",
    ".github/workflows/card-image.yml",
    "!host/**/*.md",
    "!host/muos/**",
)


def kind_of(rel):
    """The kind of one path relative to host/rg40xxv/."""
    rel = rel.replace("\\", "/")
    for pattern, kind in RULES:
        if fnmatch.fnmatchcase(rel, pattern):
            return kind
    return OS


def files():
    """Every file under host/rg40xxv/, relative and sorted byte-wise.

    The filesystem rather than git: build.sh runs in WSL against a Windows
    worktree whose .git git cannot follow. CI's checkout is clean, so the two
    agree unless a desk has stray files in the card tree -- which then, as
    they would in a build, count.
    """
    found = []
    for top, dirs, names in os.walk(CARD):
        dirs[:] = [d for d in dirs if d != "__pycache__"]
        for name in names:
            path = os.path.join(top, name)
            found.append(os.path.relpath(path, CARD).replace(os.sep, "/"))
    return sorted(found, key=lambda rel: rel.encode("utf-8"))


def buildroot_version():
    """The Buildroot release build.sh builds with, from build.sh itself."""
    with open(os.path.join(CARD, "build.sh"), encoding="utf-8") as handle:
        match = re.search(r"/(buildroot-[0-9.]+)\}", handle.read())
    if not match:
        sys.exit("card_inputs: build.sh names no buildroot-<version> directory")
    return match.group(1)


def key():
    """The OS base's name: 16 hex digits of a hash of every OS input.

    build.sh is an OS input, so the Buildroot release it names is in here too.
    The rules are hashed as well, so moving a file between kinds is a full
    build rather than a base that was cut on different terms.
    """
    digest = hashlib.sha256()
    digest.update(("epoch %d\nrules %r\n" % (BASE_EPOCH, RULES)).encode())
    for rel in files():
        kind = kind_of(rel)
        if kind == OS:
            with open(os.path.join(CARD, rel), "rb") as handle:
                content = hashlib.sha256(handle.read()).hexdigest()
            digest.update(("os %s %s\n" % (content, rel)).encode())
        elif kind == OVERLAY:
            digest.update(("overlay %s\n" % rel).encode())
    return digest.hexdigest()[:16]


def needs(paths):
    """'full' if any of these repository paths is an OS input, else 'base'.

    Adding or removing an overlay file is an OS change; editing one is not.
    Which overlay files a change adds or removes cannot be told from a list of
    paths alone, so a path under the overlay that does not exist on disk any
    more counts as removed, and one that does is assumed edited.
    """
    for path in paths:
        path = path.replace("\\", "/")
        if not path.startswith(CARD_PREFIX):
            continue
        rel = path[len(CARD_PREFIX):]
        kind = kind_of(rel)
        if kind == OS:
            return "full"
        if kind == OVERLAY and not os.path.exists(os.path.join(CARD, rel)):
            return "full"
    return "base"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("key", help="print the OS base's name")
    listing = sub.add_parser("list", help="print the files of one kind")
    listing.add_argument("kind", nargs="?", default=OS, choices=KINDS)
    sub.add_parser("explain", help="print every file with its kind")
    need = sub.add_parser("needs", help="would a change to these paths need "
                                        "a full build? reads stdin if none")
    need.add_argument("paths", nargs="*")
    args = parser.parse_args()

    if args.command == "key":
        print(key())
    elif args.command == "list":
        for rel in files():
            if kind_of(rel) == args.kind:
                print(rel)
    elif args.command == "explain":
        for rel in files():
            print("%-12s %s" % (kind_of(rel), rel))
    elif args.command == "needs":
        paths = args.paths or [line.strip() for line in sys.stdin if line.strip()]
        print(needs(paths))


if __name__ == "__main__":
    main()
