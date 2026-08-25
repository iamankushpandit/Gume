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

import contextlib
import io
import os
import re
import sys

import elf_size
import gen_site
from app_registry_parser import playable_apps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as handle:
        return handle.read()


def fail(problems, message):
    problems.append(message)


def check_version(problems):
    """README's stated release must match the firmware constant."""
    header = read("include", "AppVersion.h")
    match = re.search(r'BRAINO_VERSION\s+"([^"]+)"', header)
    if not match:
        fail(problems, "AppVersion.h: BRAINO_VERSION not found")
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
    count = str(len(playable_apps()))

    readme = read("README.md")
    stated = re.search(r"\|\s*Games\s*\|\s*(\d+)\s*\|", readme)
    if not stated:
        fail(problems, "README.md: no '| Games | N |' row")
    elif stated.group(1) != count:
        fail(problems, "README.md says %s games, playable app count is %s"
             % (stated.group(1), count))

    if not re.search(r"\b%s-game\b" % count, readme):
        fail(problems, "README.md opening line does not say '%s-game'" % count)

    # CONTRIBUTING.md is the first file a would-be contributor reads, and it
    # was never checked by anything -- it claimed 30 games while the registry
    # had 31.
    contributing = read("CONTRIBUTING.md")
    stated = re.search(r"(\d+) built-in games", contributing)
    if not stated:
        fail(problems, "CONTRIBUTING.md: no 'N built-in games' phrase")
    elif stated.group(1) != count:
        fail(problems, "CONTRIBUTING.md says %s built-in games, playable app "
                       "count is %s" % (stated.group(1), count))

    registry = read("src", "engine", "AppRegistry.h")
    declared = re.search(r"SYSTEM_APP_COUNT\s*=\s*(\d+)", registry)
    stated = re.search(r"built-in games,\s+(\d+)\s+system apps", contributing)
    if declared and stated and stated.group(1) != declared.group(1):
        fail(problems, "CONTRIBUTING.md says %s system apps, AppRegistry.h "
                       "says %s" % (stated.group(1), declared.group(1)))


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
    """README and CLAUDE.md must quote the same build figures -- as each other,
    and as the last actual build.

    They going out of step with one another is one failure: one gets updated
    after a `pio run` and the other does not. But agreeing with each other was
    never sufficient, and trusting it cost 404 bytes of drift -- both documents
    said 2,347,169 while the firmware had grown to 2,347,573, and the check
    stayed green the whole time because it only compared them to each other.

    So when there is a build to read, the documented figures are checked
    against the ELF the compiler actually produced. On a tree that has never
    been built there is nothing to compare against and only the agreement check
    runs, which keeps a fresh checkout (and CI before its build step) clean.
    """
    readme = read("README.md")
    claude = read("CLAUDE.md")

    def numbers(text, label, capacity):
        found = re.findall(r"([\d,]{6,})\s*/\s*" + re.escape(capacity), text)
        if not found:
            fail(problems, "%s: no figure of the form 'N / %s'" % (label, capacity))
        return set(found)

    readme_flash = numbers(readme, "README.md", "3,145,728")
    claude_flash = numbers(claude, "CLAUDE.md", "3,145,728")
    if readme_flash and claude_flash and readme_flash != claude_flash:
        fail(problems, "flash figures disagree: README %s vs CLAUDE.md %s"
             % (sorted(readme_flash), sorted(claude_flash)))

    figures = elf_size.build_figures(elf_size.default_elf(ROOT))
    if figures is None:
        return                      # never built here; nothing to check against
    flash, ram = figures

    readme_ram = numbers(readme, "README.md", "327,680")
    claude_ram = numbers(claude, "CLAUDE.md", "327,680")

    for label, stated, real, what in (
            ("README.md", readme_flash, flash, "flash"),
            ("CLAUDE.md", claude_flash, flash, "flash"),
            ("README.md", readme_ram, ram, "RAM"),
            ("CLAUDE.md", claude_ram, ram, "RAM")):
        for value in sorted(stated):
            if int(value.replace(",", "")) != real:
                fail(problems, "%s says %s bytes of %s, the build in .pio "
                               "reports %s -- re-read the size line from your "
                               "own `pio run`"
                     % (label, value, what, format(real, ",")))


def check_about_is_derived(problems):
    """About must read its facts from the firmware, not restate them.

    It fell six games behind once by keeping its own list. The rule is in
    CLAUDE.md; this catches the specific regression of hardcoding again.
    """
    about = read("src", "games", "AboutGame.cpp")
    for symbol in ("playableAppAt", "playableAppCount", "BRAINO_VERSION",
                   "BRAINO_PRODUCT_NAME", "BRAINO_COPYRIGHT",
                   "BOARD_NAME", "BleBeacon::active"):
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


def check_games_are_documented(problems):
    """Every catalogued game must appear in README by name.

    Step 7 of the "Adding a game or an app" checklist. A game that launches
    correctly and is invisible in every document describing the product is not
    finished -- US States, State Flags, State Maps and Trace all shipped that
    way, listed in the catalog and absent from the README.
    """
    titles = [app.title for app in playable_apps()]
    if not titles:
        fail(problems, "AppRegistry metadata: could not parse any game titles")
        return

    readme = read("README.md")
    for title in titles:
        if title not in readme:
            fail(problems, "game '%s' is in the playable app registry but never named in "
                           "README.md" % title)


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
    for app in playable_apps():
        game_id = app.id
        name = alias.get(game_id, game_id)
        if name not in on_disk:
            fail(problems, "game '%s' has no screenshot (expected "
                           "docs/screens/%s.png)" % (game_id, name))


def check_site(problems):
    """The Pages site must stay derived, and its firmware list must be real.

    `tools/gen_site.py` fills the landing page from AppVersion.h, AppRegistry
    and README, for the same reason About derives its list. Two ways that can
    rot: someone types a fact into the template that the generator already
    supplies, or someone adds a PlatformIO environment to the page that the
    Pages workflow never builds -- which produces a picker entry whose manifest
    404s halfway through erasing somebody's board.

    The third way, and the one that actually broke the deploy: a new game adds
    a still to docs/screens/ and wires it into gen_screens.py and the README,
    but nobody adds it to gen_site.py's PLAYABLE_STILLS. gen_site.py refuses to
    generate an orphaned still, but it only says so when the site is generated
    -- which happens in CI, on main, after the push. Every documented local
    check passed and Pages failed twice anyway. So the still contract is
    enforced here too, by calling gen_site's own validator rather than
    reimplementing it, so the two cannot drift apart.
    """
    template = read("site", "index.template.html")
    generator = read("tools", "gen_site.py")

    for placeholder in sorted(set(re.findall(r'\("(\{\{[A-Z_]+\}\})"', generator))):
        if placeholder not in template:
            fail(problems, "gen_site.py substitutes %s but site/"
                           "index.template.html no longer contains it" % placeholder)

    if re.search(r"\d+\.\d+\.\d+", template):
        fail(problems, "site/index.template.html hardcodes a version number -- "
                       "it is supposed to use the {{VERSION}} placeholder")

    envs = re.findall(r'"env":\s*"(\w+)"', generator)
    ini = read("platformio.ini")
    workflow = read(".github", "workflows", "pages.yml")
    for env in envs:
        if not re.search(r"^\[env:%s\]" % re.escape(env), ini, re.M):
            fail(problems, "gen_site.py offers firmware '%s', which is not a "
                           "platformio.ini environment" % env)
        if not re.search(r"\b%s\b" % re.escape(env), workflow):
            fail(problems, "gen_site.py offers firmware '%s', but the Pages "
                           "workflow never builds it -- its manifest would "
                           "point at binaries that do not exist" % env)

    check_site_screens(problems)


def check_site_screens(problems):
    """Every docs/screens still must appear on the site, and vice versa.

    gen_site.validate_site_screens() is the authority; it reports through
    die(), which writes to stderr and raises SystemExit. Left alone that would
    kill check_docs.py mid-run and hide every later problem, so capture both
    and turn them into ordinary problem strings.
    """
    captured = io.StringIO()
    try:
        with contextlib.redirect_stderr(captured):
            gen_site.validate_site_screens(playable_apps())
    except SystemExit:
        message = captured.getvalue().strip() or "gen_site.py rejected the screen set"
        for line in message.splitlines():
            fail(problems, line.replace("gen_site.py: ", "", 1) +
                 " -- fix tools/gen_site.py, or the Pages build fails on main")


def check_badges(problems):
    """The README badges must not become the next stale fact.

    Shields.io badges are images with the value baked into the URL, so a
    version, a game count or a flash percentage typed into one drifts exactly
    the way the README's headline game count and flash figure already did --
    and a wrong number is more believable in a badge, because it looks
    generated. These three are checked against the same sources the rest of
    this file uses.
    """
    readme = read("README.md")

    match = re.search(r"BRAINO_VERSION\s+\"([^\"]+)\"", read("include", "AppVersion.h"))
    badge = re.search(r"img\.shields\.io/badge/version-([\d.]+)-", readme)
    if match and badge and badge.group(1) != match.group(1):
        fail(problems, "README version badge says %s, AppVersion.h says %s"
             % (badge.group(1), match.group(1)))

    count = len(playable_apps())
    badge = re.search(r"img\.shields\.io/badge/games-(\d+)-", readme)
    if badge and int(badge.group(1)) != count:
        fail(problems, "README games badge says %s, the registry has %d playable apps"
             % (badge.group(1), count))

    badge = re.search(r"img\.shields\.io/badge/flash-([\d.]+)%25", readme)
    if not badge:
        return
    figures = elf_size.build_figures(elf_size.default_elf(ROOT))
    if figures is None:
        stated = re.search(r"([\d,]{6,})\s*/\s*3,145,728", readme)
        if not stated:
            return
        flash = int(stated.group(1).replace(",", ""))
    else:
        flash = figures[0]
    real = round(100.0 * flash / elf_size.FLASH_CAPACITY, 1)
    if abs(float(badge.group(1)) - real) > 0.05:
        fail(problems, "README flash badge says %s%%, the real figure is %.1f%%"
             % (badge.group(1), real))


def main():
    problems = []
    check_version(problems)
    check_game_count(problems)
    check_source_tree(problems)
    check_size_agreement(problems)
    check_about_is_derived(problems)
    check_credits_match_artwork(problems)
    check_games_are_documented(problems)
    check_screens(problems)
    check_site(problems)
    check_badges(problems)

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
