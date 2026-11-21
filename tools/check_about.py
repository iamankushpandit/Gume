#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""Hold everything a visitor reads first to the platforms this tree supports.

WHY THIS FILE EXISTS. 5.14.0 was the first release for three kinds of device:
ESP32 boards, the Attaky Cyber Deck Plus and the Anbernic RG40XX V. It shipped
with the old ESP32-only website, a README that opened on one ESP32 board, and a
GitHub description and topics that named only the CYD. Every other check was
green, because every other check compares the docs with each other or with the
firmware. Nothing compared them with which platforms exist.

This check does that. Each platform is derived from the tree, not from a list:

  ESP32 boards          any `custom_env_kind = product` environment
  Attaky Cyber Deck Plus  include/boards/attaky-core.h
  Anbernic RG40XX V     host/rg40xxv/

Every platform that exists must be named in:

  - the README's opening, before its first "## " section after the intro table;
  - site/index.template.html;
  - .github/about.json, the repository's About box as data: its description,
    website and topics.

Topics also follow the wording rule: no word for an age group.

The live About box is a GitHub setting, not a file, so it drifts silently.
With a token in GH_TOKEN or GITHUB_TOKEN, or a logged-in `gh`, this check reads
it from the API and fails if it differs from .github/about.json, printing the
`gh repo edit` command that fixes it. Without either, it says so and skips that
part. CI always has the token.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = "iamankushpandit/Gume"

# The same rule check_docs.py enforces on prose: players, young players or
# the administrator, never an age group.
AGE_WORDS = {"kids", "kid", "children", "child", "adults", "adult",
             "toddlers", "teens", "preschool"}


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def platforms():
    """(name, [spellings any one of which counts]) for each platform present."""
    found = []
    ini = read("platformio.ini")
    if re.search(r"^\s*custom_env_kind\s*=\s*product\b", ini, re.M):
        found.append(("ESP32 boards", ["ESP32"]))
    if os.path.exists(os.path.join(ROOT, "include", "boards", "attaky-core.h")):
        found.append(("Attaky Cyber Deck Plus", ["Attaky"]))
    if os.path.isdir(os.path.join(ROOT, "host", "rg40xxv")):
        found.append(("Anbernic RG40XX V", ["RG40XX V", "RG40XXV"]))
    return found


def readme_opening():
    """The README up to the first section after the device table: what a
    visitor sees before scrolling."""
    text = read("README.md")
    heads = [m.start() for m in re.finditer(r"^## ", text, re.M)]
    # The opening ends at the second "## " heading: the first is the device
    # table itself ("Pick your device"), which belongs to the opening.
    end = heads[1] if len(heads) > 1 else len(text)
    return text[:end]


def live_about():
    """The repository's About box as GitHub has it, or None if unreadable."""
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        req = urllib.request.Request(
            "https://api.github.com/repos/" + REPO,
            headers={"Authorization": "Bearer " + token,
                     "Accept": "application/vnd.github+json"})
        with urllib.request.urlopen(req, timeout=30) as r:
            data = json.load(r)
    elif shutil.which("gh"):
        out = subprocess.run(["gh", "api", "repos/" + REPO],
                             capture_output=True, text=True)
        if out.returncode != 0:
            return None
        data = json.loads(out.stdout)
    else:
        return None
    return {"description": data.get("description") or "",
            "homepage": data.get("homepage") or "",
            "topics": sorted(data.get("topics") or [])}


def main():
    problems = []
    about = json.loads(read(os.path.join(".github", "about.json")))
    about["topics"] = sorted(about["topics"])
    opening = readme_opening()
    site = read(os.path.join("site", "index.template.html"))

    for name, spellings in platforms():
        for where, text in (("the README's opening", opening),
                            ("site/index.template.html", site),
                            (".github/about.json description", about["description"])):
            if not any(s in text for s in spellings):
                problems.append("%s does not name %s, which this tree supports"
                                % (where, name))

    for t in about["topics"]:
        if t.lower() in AGE_WORDS:
            problems.append(".github/about.json topic %r names an age group; "
                            "the wording rule says players or young players" % t)

    try:
        live = live_about()
    except Exception as e:  # network trouble is not a pass
        problems.append("could not read the live About box: %s" % e)
        live = None
    if live is None:
        print("check_about: no token or gh login -- live About box not compared")
    else:
        for key in ("description", "homepage", "topics"):
            if live[key] != about[key]:
                problems.append("live repository %s differs from .github/about.json:\n"
                                "    live: %r\n    file: %r" % (key, live[key], about[key]))
        if any(live[k] != about[k] for k in ("description", "homepage", "topics")):
            problems.append(
                "fix the live box from the file with:\n    gh repo edit %s "
                "--description %s --homepage %s %s\n    (and --remove-topic for any "
                "live topic not in the file)" % (
                    REPO, json.dumps(about["description"]), about["homepage"],
                    " ".join("--add-topic " + t for t in about["topics"])))

    if problems:
        for p in problems:
            print("check_about: " + p)
        return 1
    print("check_about: clean (%d platform(s) named everywhere)" % len(platforms()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
