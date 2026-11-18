# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""Hold card-image.yml to tools/card_inputs.py, the one definition of what the
RG40XX V card image is built from.

The workflow cannot compute its own `paths:` filters, so they are the one
list that has to be written out in YAML. This check is what keeps that copy
from drifting, and keeps a second list from growing anywhere else:

- both `paths:` filters (pull_request and push) are exactly TRIGGER_PATHS;
- the workflow's BUILDROOT is the release build.sh builds with, or a
  stored base would be unpacked beside a Buildroot build.sh cannot find;
- the base is looked up and stored by the key os-base.sh prints, and the
  download cache is keyed on it too, never on a hashFiles() list of
  host/rg40xxv paths, which would be a second definition of the OS;
- os-base.sh's key and settle ask card_inputs.py rather than listing files;
- every rule in card_inputs.RULES still matches a file, so the table cannot
  keep a rule for a file that has gone and look as if it covered something.

    python tools/check_card_inputs.py
"""

import fnmatch
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import card_inputs  # noqa: E402

ROOT = card_inputs.ROOT
WORKFLOW = os.path.join(ROOT, ".github", "workflows", "card-image.yml")
OS_BASE = os.path.join(card_inputs.CARD, "os-base.sh")


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def paths_filters(text):
    """{event: [patterns]} for every `paths:` list under `on:`."""
    on = re.search(r"^on:\n(.*?)(?=^\S)", text, re.M | re.S)
    if not on:
        return {}
    found, event = {}, None
    lines = on.group(1).splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        head = re.match(r"^  (\w+):", line)
        if head:
            event = head.group(1)
        if re.match(r"^    paths:\s*$", line):
            items = []
            i += 1
            while i < len(lines) and re.match(r"^      - ", lines[i]):
                items.append(lines[i].split("- ", 1)[1].strip().strip('"\''))
                i += 1
            found[event] = items
            continue
        i += 1
    return found


def main():
    problems = []
    text = read(WORKFLOW)

    filters = paths_filters(text)
    for event in ("pull_request", "push"):
        got = filters.get(event)
        if got is None:
            problems.append("card-image.yml has no `paths:` filter on %s" % event)
        elif tuple(got) != card_inputs.TRIGGER_PATHS:
            problems.append(
                "card-image.yml's %s paths are not tools/card_inputs.py's "
                "TRIGGER_PATHS.\n      workflow:  %s\n      expected:  %s"
                % (event, ", ".join(got), ", ".join(card_inputs.TRIGGER_PATHS)))

    match = re.search(r"^\s+BUILDROOT:\s*(\S+)\s*$", text, re.M)
    wanted = card_inputs.buildroot_version()
    if not match:
        problems.append("card-image.yml sets no BUILDROOT")
    elif match.group(1) != wanted:
        problems.append("card-image.yml fetches %s but build.sh builds with %s"
                        % (match.group(1), wanted))

    if "os-base.sh key" not in text:
        problems.append("card-image.yml does not name the base by "
                        "`os-base.sh key`")
    for call in re.findall(r"hashFiles\(([^)]*)\)", text):
        if "host/rg40xxv" in call:
            problems.append("card-image.yml hashes host/rg40xxv paths itself "
                            "(hashFiles(%s)); key on `os-base.sh key`, which "
                            "is the one definition" % call)

    base = read(OS_BASE)
    if base.count('python3 "$CARD_INPUTS"') < 2 or "find . -type f" in base:
        problems.append("host/rg40xxv/os-base.sh must take its key and its "
                        "list of OS inputs from tools/card_inputs.py")

    names = card_inputs.files()
    for pattern, kind in card_inputs.RULES:
        if not any(fnmatch.fnmatchcase(rel, pattern) for rel in names):
            problems.append("tools/card_inputs.py rule %r (%s) matches no file "
                            "under host/rg40xxv/" % (pattern, kind))

    if problems:
        print("check_card_inputs: %d problem(s)" % len(problems))
        for problem in problems:
            print("  - " + problem)
        return 1
    counts = {}
    for rel in names:
        counts[card_inputs.kind_of(rel)] = counts.get(card_inputs.kind_of(rel), 0) + 1
    print("check_card_inputs: OK -- key %s; %s"
          % (card_inputs.key(), ", ".join("%d %s" % (counts.get(k, 0), k)
                                         for k in card_inputs.KINDS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
