#!/usr/bin/env python3
"""Check that the docs still describe the code.

Three rules in CLAUDE.md say to keep README.md, CLAUDE.md and the About app in
sync with the code. Rules were not enough -- the game count, the version, the
source tree listing and the flash figures each went stale anyway, because
"remember to update the docs" is a request for vigilance and vigilance is
exactly what runs out at the end of a long change.

This checks the parts a machine can check. Run it before committing:

    python tools/check_docs.py

Exit code 0 = clean, 1 = something has drifted. It deliberately does not try to
check prose; it checks the facts that have actually gone stale before.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as handle:
        return handle.read()


def fail(problems, message):
    problems.append(message)


def check_version(problems):
    """README's stated release must match the firmware constant."""
    header = read("include", "AppVersion.h")
    match = re.search(r'GOODTIME_KIDS_VERSION\s+"([^"]+)"', header)
    if not match:
        fail(problems, "AppVersion.h: GOODTIME_KIDS_VERSION not found")
        return
    version = match.group(1)

    readme = read("README.md")
    stated = re.search(r"Current release: \*\*([^*]+)\*\*", readme)
    if not stated:
        fail(problems, "README.md: no 'Current release: **x.y.z**' line")
    elif stated.group(1) != version:
        fail(problems, "README.md says release %s, AppVersion.h says %s"
             % (stated.group(1), version))

    changelog = read("CHANGELOG.md")
    if not re.search(r"^## %s\b" % re.escape(version), changelog, re.M):
        fail(problems, "CHANGELOG.md has no '## %s' section" % version)


def check_game_count(problems):
    """The headline game count is the fact that has gone stale most often."""
    catalog = read("src", "engine", "GameCatalog.h")
    match = re.search(r"GAME_CATALOG_COUNT\s*=\s*(\d+)", catalog)
    if not match:
        fail(problems, "GameCatalog.h: GAME_CATALOG_COUNT not found")
        return
    count = match.group(1)

    readme = read("README.md")
    stated = re.search(r"\|\s*Games\s*\|\s*(\d+)\s*\|", readme)
    if not stated:
        fail(problems, "README.md: no '| Games | N |' row")
    elif stated.group(1) != count:
        fail(problems, "README.md says %s games, GAME_CATALOG_COUNT is %s"
             % (stated.group(1), count))

    if not re.search(r"\b%s-game\b" % count, readme):
        fail(problems, "README.md opening line does not say '%s-game'" % count)


def check_source_tree(problems):
    """Every source file should appear in README's 'Layout of the code' block.

    Games are listed as a directory rather than file by file, so they are
    exempt; everything else is small enough to enumerate and is the part a
    reader uses to orient themselves.
    """
    readme = read("README.md")
    block = re.search(r"## Layout of the code\s*\n+```(.*?)```", readme, re.S)
    if not block:
        fail(problems, "README.md: no 'Layout of the code' code block")
        return
    listed = block.group(1)

    for folder in ("", "engine", "hal", "ui"):
        directory = os.path.join(ROOT, "src", folder)
        for name in sorted(os.listdir(directory)):
            if not name.endswith(".cpp"):
                continue
            if name not in listed:
                where = "src/%s/%s" % (folder, name) if folder else "src/%s" % name
                fail(problems, "README.md layout block does not mention %s" % where)


def check_size_agreement(problems):
    """README and CLAUDE.md must quote the same build figures as each other.

    Neither can be derived here without running a build, but they going out of
    step with one another is the failure that actually happens: one gets
    updated after a `pio run` and the other does not.
    """
    readme = read("README.md")
    claude = read("CLAUDE.md")

    def numbers(text, label):
        found = re.findall(r"([\d,]{7,})\s*/\s*3,145,728", text)
        if not found:
            fail(problems, "%s: no flash figure of the form 'N / 3,145,728'" % label)
        return set(found)

    readme_flash = numbers(readme, "README.md")
    claude_flash = numbers(claude, "CLAUDE.md")
    if readme_flash and claude_flash and readme_flash != claude_flash:
        fail(problems, "flash figures disagree: README %s vs CLAUDE.md %s"
             % (sorted(readme_flash), sorted(claude_flash)))


def check_about_is_derived(problems):
    """About must read its facts from the firmware, not restate them.

    It fell six games behind once by keeping its own list. The rule is in
    CLAUDE.md; this catches the specific regression of hardcoding again.
    """
    about = read("src", "games", "AboutGame.cpp")
    for symbol in ("GAME_CATALOG", "GOODTIME_KIDS_VERSION", "BOARD_NAME",
                   "BleBeacon::active"):
        if symbol not in about:
            fail(problems, "AboutGame.cpp no longer reads %s -- About is "
                           "supposed to derive facts, not restate them" % symbol)


def check_credits_match_artwork(problems):
    """Don't credit artwork that is no longer compiled in.

    The Countries game went, and with it the djaiss/mapsicon country outlines
    -- but README kept a licence warning saying the build could not be sold
    commercially, and About kept crediting mapsicon. A credit that outlives the
    asset is not harmless: this one misrepresented what the owner was allowed
    to do with their own project.

    `mnf_map()` is the only way country outlines can reach the screen. If
    nothing calls it, mapsicon must not be credited anywhere.
    """
    sources = []
    for folder in ("games", "ui"):
        directory = os.path.join(ROOT, "src", folder)
        for name in sorted(os.listdir(directory)):
            if name.endswith((".cpp", ".h")):
                sources.append(read("src", folder, name))
    uses_country_outlines = any(re.search(r"mnf_map\s*\(", text) for text in sources)

    for doc in ("README.md", os.path.join("src", "games", "AboutGame.cpp")):
        text = read(doc)
        credits_mapsicon = re.search(r"mapsicon", text, re.I) is not None
        if credits_mapsicon and not uses_country_outlines:
            if doc == "README.md" and "no mapsicon data" in text:
                continue   # explaining its removal is fine; crediting it is not
            fail(problems, "%s credits mapsicon but nothing calls mnf_map() -- "
                           "the country outlines are no longer compiled in" % doc)


def check_screens(problems):
    """Screenshots must exist, be referenced, and not outlive their feature.

    docs/screens/ kept countries-outline.png and countries-continent.png long
    after the Countries game was deleted, and had nothing for the four games
    added since. A mock-up of a screen that no longer exists is a worse lie
    than a missing one.
    """
    screens_dir = os.path.join(ROOT, "docs", "screens")
    on_disk = {n[:-4] for n in os.listdir(screens_dir) if n.endswith(".png")}

    generator = read("tools", "gen_screens.py")
    generated = set(re.findall(r'\(\s*"([a-z0-9-]+)"\s*,\s*\w+\s*,', generator))

    readme = read("README.md")
    referenced = set(re.findall(r"docs/screens/([a-z0-9-]+)\.png", readme))

    for name in sorted(generated - on_disk):
        fail(problems, "gen_screens.py builds '%s' but docs/screens/%s.png is "
                       "missing -- run python tools/gen_screens.py" % (name, name))
    for name in sorted(on_disk - generated):
        fail(problems, "docs/screens/%s.png has no generator entry -- it is "
                       "left over from a removed screen" % name)
    for name in sorted(referenced - on_disk):
        fail(problems, "README.md references docs/screens/%s.png, which does "
                       "not exist" % name)

    # Every playable game should be picturable. Catalog ids mostly match the
    # screen names; list the deliberate exceptions rather than skipping the check.
    alias = {"shapecolor": "shapes", "sort": "sorting", "fingers": "fingers-count",
             "flags": "flags-country", "states": "states", "stateflags": "stateflags",
             "statemaps": "statemaps", "tictactoe": "tictactoe"}
    catalog = read("src", "engine", "GameCatalog.cpp")
    ids = re.findall(r'^\s*\{\s*"([a-z0-9]+)"', catalog, re.M)
    for game_id in ids:
        name = alias.get(game_id, game_id)
        if name not in on_disk:
            fail(problems, "game '%s' has no screenshot (expected "
                           "docs/screens/%s.png)" % (game_id, name))


def main():
    problems = []
    check_version(problems)
    check_game_count(problems)
    check_source_tree(problems)
    check_size_agreement(problems)
    check_about_is_derived(problems)
    check_credits_match_artwork(problems)
    check_screens(problems)

    if problems:
        print("Docs are out of sync with the code:\n")
        for problem in problems:
            print("  - %s" % problem)
        print("\n%d problem(s). See the standing rules at the top of CLAUDE.md."
              % len(problems))
        return 1

    print("Docs check: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
