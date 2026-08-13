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


def main():
    problems = []
    check_version(problems)
    check_game_count(problems)
    check_source_tree(problems)
    check_size_agreement(problems)
    check_about_is_derived(problems)

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
