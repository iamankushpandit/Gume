#!/usr/bin/env python3
"""Check the catalog and app registry still line up.

`GAME_CATALOG[]` (src/engine/GameCatalog.cpp) is the single source of truth for
playable-game metadata, and `APP_REGISTRY[]` (src/engine/AppRegistry.cpp)
binds each catalog slot to its concrete game instance and launcher icon. The
registry is much safer than the old `CATALOG_KINDS[]` + `launchKind()` pair
because every catalog binding now names its source slot explicitly
(`catalogApp(17, ...)`) and the system apps are in the same table.

It is still possible to drift: a merge can append a new `GAME_CATALOG` entry
and a new `catalogApp()` line in a different order, or point the wrong catalog
index at a game instance. This check keeps that from shipping silently.

`ScoreCatalog.cpp` is keyed off the same ids, so it is checked too.

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
    catalog_h = read("src", "engine", "GameCatalog.h")
    catalog = read("src", "engine", "GameCatalog.cpp")
    registry = read("src", "engine", "AppRegistry.cpp")
    scores = read("src", "engine", "ScoreCatalog.cpp")

    declared = re.search(r"GAME_CATALOG_COUNT\s*=\s*(\d+)", catalog_h)
    if not declared:
        problems.append("GameCatalog.h: GAME_CATALOG_COUNT not found")
        return
    declared = int(declared.group(1))

    ids = re.findall(r'^\s*\{\s*"([a-z0-9]+)"', catalog, re.M)
    if not ids:
        problems.append("GameCatalog.cpp: could not parse any entries")
        return

    bound = re.findall(r"catalogApp\((\d+),\s*LauncherIcon::(\w+)", registry)
    if not bound:
        problems.append("AppRegistry.cpp: no catalogApp() entries found")
        return
    system_ids = re.findall(r'systemApp\("([a-z0-9]+)"', registry)

    # 1. Lengths.
    if len(ids) != declared:
        problems.append(
            "GAME_CATALOG has %d entries but GAME_CATALOG_COUNT is %d"
            % (len(ids), declared))
    if len(bound) != declared:
        problems.append(
            "APP_REGISTRY has %d catalogApp() entries but GAME_CATALOG_COUNT is %d"
            % (len(bound), declared))

    # 2. Duplicates. Two tiles pointing at one game is the classic merge result.
    icon_names = [icon for _, icon in bound]
    registry_indices = [int(index) for index, _ in bound]
    for label, seq in (("GAME_CATALOG id", ids), ("APP_REGISTRY catalog index", registry_indices)):
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
    for i, (game_id, binding) in enumerate(zip(ids, bound)):
        registry_index, icon = int(binding[0]), binding[1]
        if registry_index != i:
            problems.append(
                "index %d: GAME_CATALOG '%s' is bound as catalogApp(%d, ...) -- "
                "the registry order or index has drifted" % (i, game_id, registry_index))
        if not pair_ok(game_id, icon):
            problems.append(
                "index %d: GAME_CATALOG '%s' is paired with LauncherIcon::%s -- "
                "if that pairing is intended, add it to EXCEPTIONS in "
                "tools/check_catalog.py; otherwise the registry has drifted and "
                "this tile launches the wrong game" % (i, game_id, icon))

    # 4. APP_REGISTRY count should match its declared total.
    total = re.search(r"APP_REGISTRY_COUNT\s*=\s*GAME_CATALOG_COUNT\s*\+\s*(\d+)",
                      read("src", "engine", "AppRegistry.h"))
    if total:
        expected_total = declared + int(total.group(1))
        actual_total = len(bound) + len(system_ids)
        if actual_total != expected_total:
            problems.append(
                "APP_REGISTRY declares %d total entries but AppRegistry.cpp has %d"
                % (expected_total, actual_total))

    # 5. ScoreCatalog is keyed off the same ids.
    score_ids = re.findall(r'^\s*\{\s*"([a-z0-9]+)"', scores, re.M)
    known = set(ids)
    for score_id in score_ids:
        if score_id not in known:
            problems.append(
                "ScoreCatalog.cpp records '%s', which is not a GAME_CATALOG id "
                "-- its scores would never be shown" % score_id)


def main():
    problems = []
    check(problems)
    if problems:
        print("Catalog or registry has drifted:\n")
        for problem in problems:
            print("  - %s" % problem)
        print("\n%d problem(s). Re-verify GAME_CATALOG[], APP_REGISTRY[] and "
              "ScoreCatalog entry-for-entry before doing anything else."
              % len(problems))
        return 1
    print("Catalog check: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
