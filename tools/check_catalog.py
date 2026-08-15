#!/usr/bin/env python3
"""Check the playable app registry still lines up internally.

`APP_REGISTRY[]` (src/engine/AppRegistry.cpp) binds metadata functions to
concrete game instances. Each playable game declares its own `AppMetadata` once
in its own `.cpp`, including launcher order, icon and default visibility.

It is still possible to drift: a merge can duplicate an index, leave registry
order mismatched with metadata order, point the wrong icon at a game, expose a
playable game to the privileged host API, register a system app without its
capabilities, or leave `PLAYABLE_APP_COUNT` out of sync with the actual
registry entries. This check keeps that from shipping silently.

    python tools/check_catalog.py

Exit 0 = aligned, 1 = something has drifted.

What this can and cannot know
-----------------------------
There is no derivable rule mapping a catalog id to a launcher icon name -- the
pairing is still a human decision ("slide" uses `LauncherIcon::SlidingPuzzle`).
So the check normalises both sides and accepts an exact match or a common
prefix, which covers every pair added since the project started naming them
consistently. The handful of older pairs that do not match are listed in
EXCEPTIONS below, explicitly, one line each. That is the point: a *new*
mismatch is a bug, an *old* one is a documented fact, and the two are told
apart by this table rather than by whoever is reading the diff.
"""

import os
import re
import sys

from app_registry_parser import playable_apps, system_apps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Catalog id -> LauncherIcon enumerator, for pairs whose names genuinely differ.
# Every entry here is a deliberate historical naming choice, not a bug. Adding
# a line is a decision; do not add one to silence a real misalignment.
EXCEPTIONS = {
    "multiply":   "Multiplication",
    "slide":      "SlidingPuzzle",
    "shapearith": "ObjectAdd",
    "fingers":    "FingerCount",
    "calendar":   "Sequence",
}


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as handle:
        return handle.read()


def normalise(name):
    return re.sub(r"[^a-z0-9]", "", name.lower())


def pair_ok(game_id, kind):
    if EXCEPTIONS.get(game_id) == kind:
        return True
    a, b = normalise(game_id), normalise(kind)
    return a == b or a.startswith(b) or b.startswith(a)


def check(problems):
    registry = read("src", "engine", "AppRegistry.cpp")
    registry_h = read("src", "engine", "AppRegistry.h")
    apps = playable_apps()
    systems = system_apps()

    declared = re.search(r"PLAYABLE_APP_COUNT\s*=\s*(\d+)", registry_h)
    if not declared:
        problems.append("AppRegistry.h: PLAYABLE_APP_COUNT not found")
        return
    declared = int(declared.group(1))

    if not apps:
        problems.append("AppRegistry.cpp: could not parse any playable app entries")
        return
    system_ids = [app.id for app in systems]

    # 1. Lengths.
    if len(apps) != declared:
        problems.append(
            "APP_REGISTRY has %d playable entries but PLAYABLE_APP_COUNT is %d"
            % (len(apps), declared))

    # 2. Duplicates. Two tiles pointing at one game is the classic merge result.
    registry_indices = [app.index for app in apps]
    for label, seq in (("AppMetadata id", [app.id for app in apps]),
                       ("AppMetadata launcher index", registry_indices)):
        seen = set()
        for i, item in enumerate(seq):
            if item in seen:
                problems.append("%s '%s' appears twice (index %d)" % (label, item, i))
            seen.add(item)
    seen = set()
    for i, item in enumerate(system_ids):
        if item in seen:
            problems.append("system app id '%s' appears twice (index %d)" % (item, i))
        seen.add(item)

    # 3. The alignment itself, index by index.
    for i, app in enumerate(apps):
        if app.index != i:
            problems.append(
                "index %d: app '%s' declares launcherIndex %d -- "
                "the registry order or metadata index has drifted" % (i, app.id, app.index))
        if not pair_ok(app.id, app.icon):
            problems.append(
                "index %d: app '%s' declares LauncherIcon::%s -- "
                "if that pairing is intended, add it to EXCEPTIONS in "
                "tools/check_catalog.py; otherwise the metadata has drifted and "
                "this tile launches the wrong game" % (i, app.id, app.icon))
        if not app.default_visible:
            problems.append(
                "index %d: app '%s' is hidden by default; make that an explicit "
                "documented product decision before shipping" % (i, app.id))

    # 4. APP_REGISTRY count should match its declared total.
    total = re.search(r"APP_REGISTRY_COUNT\s*=\s*PLAYABLE_APP_COUNT\s*\+\s*SYSTEM_APP_COUNT",
                      registry_h)
    if total:
        system_count = re.search(r"SYSTEM_APP_COUNT\s*=\s*(\d+)", registry_h)
        expected_total = declared + int(system_count.group(1))
        actual_total = len(apps) + len(system_ids)
        if actual_total != expected_total:
            problems.append(
                "APP_REGISTRY declares %d total entries but AppRegistry.cpp has %d"
                % (expected_total, actual_total))

    # 5. Score metadata should either match the app id or be intentionally absent.
    known = {app.id for app in apps}
    for app in apps:
        if app.score is None:
            continue
        if app.score.game_id not in known:
            problems.append(
                "score metadata for '%s' records game id '%s', which is not a playable app id"
                % (app.id, app.score.game_id))

    # 6. Playable games should not receive the privileged GameHost surface.
    for app in apps:
        header = app.source_file[:-4] + ".h"
        try:
            header_text = read(header)
        except FileNotFoundError:
            problems.append("%s: metadata source has no matching header" % app.source_file)
            continue
        if ": public AppGame" not in header_text:
            problems.append(
                "%s: playable app '%s' must derive from AppGame, not Game, "
                "so it receives AppContext instead of the full GameHost"
                % (header, app.id))

    # 7. System apps are privileged by declaration. An empty capability set is
    # usually a forgotten permission review rather than an intentional state.
    for app in systems:
        if "APP_CAP_" not in app.capabilities or app.capabilities == "APP_CAP_NONE":
            problems.append(
                "system app '%s' has no explicit capability declaration" % app.id)


def main():
    problems = []
    check(problems)
    if problems:
        print("Catalog or registry has drifted:\n")
        for problem in problems:
            print("  - %s" % problem)
        print("\n%d problem(s). Re-verify APP_REGISTRY[] and the per-game "
              "AppMetadata declarations before doing anything else."
              % len(problems))
        return 1
    print("Catalog check: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
