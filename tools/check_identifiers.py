#!/usr/bin/env python3
"""Refuse to let a hardware or personal identifier into this repository.

WHY THIS FILE EXISTS. `tools/board_registry.json` mapped six boards' MAC
addresses to the exact firmware each was running, and it was tracked for three
commits and shipped inside two release tarballs before anybody noticed. That
is not a stale-docs problem, it is not recoverable, and no later commit undoes
it: a MAC is burned into eFuse, so it cannot be changed, and once published it
is public for the life of the hardware. Anyone with the file knew which
specific boards existed, where to look for them on a network, and what was
running on them.

Vigilance did not catch it. A check does.

WHAT COUNTS AS AN IDENTIFIER HERE. Anything that points at one physical device,
one network, or one person, rather than at a model or a design:

  - MAC addresses, in any separator style
  - Wi-Fi SSIDs and passwords, and any other credential
  - serial numbers, eFuse ids, chip ids read off a specific unit
  - public IP addresses, and hostnames of somebody's own machine
  - a player's name, profile name or score

Placeholders are fine and are what documentation should use. The allowlist
below is deliberately short: an entry is a promise that the string names
nothing real.

WHAT THIS CANNOT CATCH, so do not treat a clean run as permission. An SSID is
just a word; a hostname looks like a domain; a child's name looks like any
other name. This checker finds the shapes a machine can recognise, and the
rule in CLAUDE.md covers the rest.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Placeholder MACs, and the documentation prefixes reserved for examples.
ALLOWED_MACS = {
    "aa:bb:cc:00:00:01", "aa:bb:cc:00:00:02", "aa:bb:cc:dd:ee:ff",
    "00:00:00:00:00:00", "ff:ff:ff:ff:ff:ff", "de:ad:be:ef:00:00",
    "01:23:45:67:89:ab", "12:34:56:78:9a:bc",
}

# Files that necessarily contain the PATTERN in order to describe or test it.
PATTERN_OWNERS = {
    "tools/check_identifiers.py",
}

MAC_RE = re.compile(r"\b(?:[0-9a-fA-F]{2}[:-]){5}[0-9a-fA-F]{2}\b")

# A public IPv4. Private ranges, loopback and 0.0.0.0 are not identifying:
# every home network uses them and they point at nothing from outside.
IPV4_RE = re.compile(r"\b(?:\d{1,3}\.){3}\d{1,3}\b")
PRIVATE_IPV4 = re.compile(
    r"^(?:10\.|127\.|169\.254\.|192\.168\.|0\.|255\.|22[4-9]\.|23\d\.|"
    r"172\.(?:1[6-9]|2\d|3[01])\.|192\.0\.2\.|198\.51\.100\.|203\.0\.113\.)")

TEXT_SUFFIXES = {
    ".c", ".cpp", ".h", ".hpp", ".ini", ".json", ".md", ".py", ".txt",
    ".yml", ".yaml", ".csv", ".html", ".js", ".css", ".cfg", ".toml", ".sh",
}


def tracked_files():
    out = subprocess.run(["git", "ls-files"], cwd=ROOT,
                         stdout=subprocess.PIPE, check=True)
    return out.stdout.decode("utf-8", "replace").splitlines()


def looks_like_version(text, start, end):
    """`1.2.3.4` in prose is a version, not an address."""
    before = text[max(0, start - 1):start]
    after = text[end:end + 1]
    return before.isdigit() or after.isdigit()


def main():
    problems = []
    for rel in tracked_files():
        if rel in PATTERN_OWNERS:
            continue
        if os.path.splitext(rel)[1].lower() not in TEXT_SUFFIXES:
            continue
        path = os.path.join(ROOT, rel)
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        except OSError:
            continue

        for line_no, line in enumerate(text.splitlines(), 1):
            for match in MAC_RE.finditer(line):
                mac = match.group(0).lower().replace("-", ":")
                if mac in ALLOWED_MACS:
                    continue
                problems.append(
                    "%s:%d: looks like a MAC address (%s).\n"
                    "    A MAC is burned into eFuse and cannot be changed, so "
                    "publishing one is permanent.\n"
                    "    Keep it on the machine that owns the hardware. Use "
                    "Board::deviceId() to identify a\n"
                    "    board instead -- the firmware generates it, it means "
                    "nothing off the device, and a\n"
                    "    factory reset reissues it. If this really is a "
                    "placeholder, add it to ALLOWED_MACS."
                    % (rel, line_no, match.group(0)))

            for match in IPV4_RE.finditer(line):
                addr = match.group(0)
                if PRIVATE_IPV4.match(addr) or looks_like_version(
                        line, match.start(), match.end()):
                    continue
                if any(part == "" or int(part) > 255
                       for part in addr.split(".")):
                    continue
                problems.append(
                    "%s:%d: looks like a public IP address (%s). If it "
                    "identifies somebody's own\n"
                    "    network or machine, it does not belong here."
                    % (rel, line_no, addr))

    if problems:
        sys.stderr.write("Identifier check FAILED:\n\n")
        for problem in problems:
            sys.stderr.write("  " + problem + "\n\n")
        sys.stderr.write(
            "See CLAUDE.md, 'No identifiers in this repository'. This is not a "
            "style rule:\n"
            "the last time one of these shipped it reached two releases and "
            "cannot be recalled.\n")
        return 1

    print("Identifier check: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
