#!/usr/bin/env python3
"""Check the index-coupled catalog arrays still line up.

`GAME_CATALOG[]` (src/engine/GameCatalog.cpp) and `CATALOG_KINDS[]`
(src/main.cpp) are parallel arrays coupled **by index**, in two different
files, with nothing in the build enforcing agreement. CLAUDE.md says so three
separate times, because it is the one failure here that is completely silent:
a merge that appends both entries in a different order, or a "keep both
changes" conflict resolution, still compiles, still links, and the only symptom
is a launcher tile opening the wrong game.

`ScoreCatalog.cpp` is keyed off the same ids, so it is checked too.

    python tools/check_catalog.py

Exit 0 = aligned, 1 = something has drifted.

What this can and cannot know
-----------------------------
There is no derivable rule mapping a catalog id to an EntryKind name -- the
pairing is a human decision ("slide" launches `SlidingPuzzle`). So the check
normalises both sides and accepts an exact match or a common prefix, which
covers every pair added since the project started naming them consistently.
The handful of older pairs that do not match are listed in EXCEPTIONS below,
explicitly, one line each. That is the point: a *new* mismatch is a bug, an
*old* one is a documented fact, and the two are told apart by this table
rather than by whoever is reading the diff.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Catalog id -> EntryKind enumerator, for pairs whose names genuinely differ.
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
    main = read("src", "main.cpp")
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

    block = re.search(r"CATALOG_KINDS\[GAME_CATALOG_COUNT\]\s*=\s*\{(.*?)\};",
                      main, re.S)
    if not block:
        problems.append("main.cpp: CATALOG_KINDS[] not found")
        return
    kinds = re.findall(r"EntryKind::(\w+)", block.group(1))

    # 1. Lengths. A short CATALOG_KINDS zero-fills the tail rather than failing
    #    to compile, so the last tiles would launch whatever kind is 0.
    if len(ids) != declared:
        problems.append(
            "GAME_CATALOG has %d entries but GAME_CATALOG_COUNT is %d"
            % (len(ids), declared))
    if len(kinds) != declared:
        problems.append(
            "CATALOG_KINDS has %d entries but GAME_CATALOG_COUNT is %d"
            % (len(kinds), declared))

    # 2. Duplicates. Two tiles pointing at one game is the classic merge result.
    for label, seq in (("GAME_CATALOG id", ids), ("CATALOG_KINDS entry", kinds)):
        seen = set()
        for i, item in enumerate(seq):
            if item in seen:
                problems.append("%s '%s' appears twice (index %d)" % (label, item, i))
            seen.add(item)

    # 3. The alignment itself, index by index.
    for i, (game_id, kind) in enumerate(zip(ids, kinds)):
        if not pair_ok(game_id, kind):
            problems.append(
                "index %d: GAME_CATALOG '%s' is paired with EntryKind::%s -- "
                "if that pairing is intended, add it to EXCEPTIONS in "
                "tools/check_catalog.py; otherwise the arrays have drifted and "
                "this tile launches the wrong game" % (i, game_id, kind))

    # 4. Every kind used must be declared, and must be wired in launchKind().
    enum_block = re.search(r"enum class EntryKind\s*:\s*uint8_t\s*\{(.*?)\};",
                           main, re.S)
    if enum_block:
        declared_kinds = set(re.findall(r"^\s*(\w+),", enum_block.group(1), re.M))
        for kind in kinds:
            if kind not in declared_kinds:
                problems.append(
                    "CATALOG_KINDS uses EntryKind::%s, which is not in the enum"
                    % kind)
    launched = set(re.findall(r"case EntryKind::(\w+):", main))
    for kind in kinds:
        if kind not in launched:
            problems.append(
                "EntryKind::%s has no case in launchKind() -- its tile would "
                "fall through and open nothing" % kind)

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
        print("Catalog arrays have drifted:\n")
        for problem in problems:
            print("  - %s" % problem)
        print("\n%d problem(s). Re-verify GAME_CATALOG[], CATALOG_KINDS[] and "
              "ScoreCatalog entry-for-entry before doing anything else."
              % len(problems))
        return 1
    print("Catalog check: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
