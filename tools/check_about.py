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
import urllib.error
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


def api(path):
    """GET a GitHub API path: the decoded JSON, False on a 404, None if no
    token or gh login is available to ask with."""
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        req = urllib.request.Request(
            "https://api.github.com/" + path,
            headers={"Authorization": "Bearer " + token,
                     "Accept": "application/vnd.github+json"})
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return False
            raise
    if shutil.which("gh"):
        out = subprocess.run(["gh", "api", path], capture_output=True, text=True)
        if out.returncode != 0:
            if "404" in out.stderr or "Not Found" in out.stdout + out.stderr:
                return False
            return None
        return json.loads(out.stdout)
    return None


def live_about():
    """The repository's About box as GitHub has it, or None if unreadable."""
    data = api("repos/" + REPO)
    if not data:
        return None
    return {"description": data.get("description") or "",
            "homepage": data.get("homepage") or "",
            "topics": sorted(data.get("topics") or [])}


def card_problems():
    """Every published release has its RG40XX V card.

    5.14.0 was published with no card, while the site promised one, because
    the card was a second tag somebody had to remember. release.yml now makes
    it; this is the check that it did. The site links to card-rg40xxv-v<the
    version on main>, so a release with no card is a dead link on the page.
    """
    version = re.search(r'BRAINO_VERSION\s+"([^"]+)"',
                        read(os.path.join("include", "AppVersion.h"))).group(1)
    if "-" in version:
        version = None  # a snapshot: check the newest published release instead
    latest = api("repos/%s/releases/latest" % REPO)
    if latest is None:
        return None
    wanted = set()
    if latest:
        wanted.add(latest["tag_name"].lstrip("v"))
    if version and api("repos/%s/releases/tags/v%s" % (REPO, version)):
        wanted.add(version)
    out = []
    for v in sorted(wanted):
        if not api("repos/%s/releases/tags/card-rg40xxv-v%s" % (REPO, v)):
            out.append("release v%s is published but card-rg40xxv-v%s is not -- "
                       "the RG40XX V has no card for it and the site's Install "
                       "tab links to nothing; start card-image.yml on that tag "
                       "(host/rg40xxv/README.md, \"In CI\")" % (v, v))
    return out


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

    try:
        cards = card_problems()
    except Exception as e:  # network trouble is not a pass
        cards = ["could not read the releases: %s" % e]
    if cards is None:
        print("check_about: no token or gh login -- card releases not checked")
    else:
        problems.extend(cards)

    if problems:
        for p in problems:
            print("check_about: " + p)
        return 1
    print("check_about: clean (%d platform(s) named everywhere)" % len(platforms()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
