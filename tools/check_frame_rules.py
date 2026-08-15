#!/usr/bin/env python3
"""Check the memory and frame-budget rules that CLAUDE.md states in prose.

Three rules, none of which the compiler can enforce:

  1. No raw owning allocation -- no `new`, `delete`, `malloc`, `free`. There is
     not one in this firmware and there must not be; the classic leak is
     impossible by construction only for as long as that stays true.
  2. No Arduino `String` in anything that runs per frame. Every concatenation
     allocates, and the heap here cannot be compacted, so churn fragments it
     and something fails to allocate hours later.
  3. No `delay()` anywhere a render path can reach. The whole frame budget is
     20ms; battery sensing once slept 10ms per call and a top bar calls two
     battery getters.

    python tools/check_frame_rules.py
    python tools/check_frame_rules.py --update-baseline

A ratchet, not a wall
---------------------
The tree already contains violations -- AboutGame builds its text with
`String`, CinnamonGame draws `String("Score ") + score_`, ScoresGame's Mine tab
predates the rule. Failing on all of them would mean the check is disabled on
day one, and CLAUDE.md separately says not to opportunistically refactor files
you are not otherwise changing.

So this records a per-file baseline count and fails only when a count goes
**up**. Existing debt is visible and frozen; new debt is refused. When you
genuinely reduce a count, re-run with --update-baseline to lower the ratchet so
it can never climb back.
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, "tools", "frame_rules_baseline.json")

# Directories whose code runs on the render/update path.
SCAN_DIRS = [("src", "games"), ("src", "ui"), ("src", "hal"), ("src", "engine")]

RULES = [
    # (label, compiled pattern)
    ("heap",   re.compile(r"\b(?:new\s+[A-Za-z_]|delete\s|malloc\s*\(|calloc\s*\(|realloc\s*\(|free\s*\()")),
    ("String", re.compile(r"\bString\b")),
    ("delay",  re.compile(r"(?<![A-Za-z_])delay\s*\(")),
]

# Lines that are comments or strings-about-strings would otherwise inflate the
# count and make the ratchet meaningless.
COMMENT = re.compile(r"^\s*(?://|\*|/\*)")


def source_files():
    for parts in SCAN_DIRS:
        directory = os.path.join(ROOT, *parts)
        if not os.path.isdir(directory):
            continue
        for name in sorted(os.listdir(directory)):
            if name.endswith((".cpp", ".h")):
                yield "/".join(parts) + "/" + name, os.path.join(directory, name)


def count_violations(path):
    """Return {rule: count} and a list of (rule, lineno, text) for reporting."""
    counts = {label: 0 for label, _ in RULES}
    hits = []
    with open(path, encoding="utf-8") as handle:
        for lineno, line in enumerate(handle, 1):
            if COMMENT.match(line):
                continue
            code = line.split("//", 1)[0]
            for label, pattern in RULES:
                if pattern.search(code):
                    counts[label] += 1
                    hits.append((label, lineno, line.rstrip()))
    return counts, hits


def scan():
    result = {}
    detail = {}
    for rel, path in source_files():
        counts, hits = count_violations(path)
        total = sum(counts.values())
        if total:
            result[rel] = counts
            detail[rel] = hits
    return result, detail


def load_baseline():
    if not os.path.exists(BASELINE):
        return None
    with open(BASELINE, encoding="utf-8") as handle:
        return json.load(handle)


def write_baseline(current):
    payload = {
        "_comment": [
            "Per-file counts of the CLAUDE.md memory/frame-rule patterns.",
            "This is a RATCHET: check_frame_rules.py fails when a count rises.",
            "Existing entries are pre-existing debt, deliberately frozen rather",
            "than fixed, because CLAUDE.md says not to opportunistically",
            "refactor files you are not otherwise changing.",
            "Lower a number only by actually removing violations, then re-run",
            "python tools/check_frame_rules.py --update-baseline",
        ],
        "files": current,
    }
    with open(BASELINE, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")


def main():
    update = "--update-baseline" in sys.argv
    current, detail = scan()

    if update:
        write_baseline(current)
        total = sum(sum(c.values()) for c in current.values())
        print("Baseline written: %d file(s), %d violation(s) frozen."
              % (len(current), total))
        return 0

    baseline = load_baseline()
    if baseline is None:
        print("No baseline yet. Create one with:\n"
              "    python tools/check_frame_rules.py --update-baseline")
        return 1
    allowed = baseline.get("files", {})

    problems = []
    for rel, counts in sorted(current.items()):
        was = allowed.get(rel, {})
        for label, count in sorted(counts.items()):
            before = was.get(label, 0)
            if count > before:
                offenders = [h for h in detail[rel] if h[0] == label][-(count - before):]
                where = ", ".join("line %d" % h[1] for h in offenders)
                problems.append(
                    "%s: %d '%s' violation(s), baseline allows %d (%s)"
                    % (rel, count, label, before, where))

    if problems:
        print("New memory / frame-budget violations:\n")
        for problem in problems:
            print("  - %s" % problem)
        print("\n%d problem(s). See the memory rule and the responsiveness rule "
              "in CLAUDE.md.\n"
              "Build text with snprintf into a stack buffer, keep screens as\n"
              "fixed members, and never delay() where a render path reaches."
              % len(problems))
        return 1

    # Report debt that has been paid off, so the ratchet gets tightened.
    loosened = []
    for rel, was in sorted(allowed.items()):
        now = current.get(rel, {})
        for label, before in sorted(was.items()):
            if now.get(label, 0) < before:
                loosened.append("%s '%s': %d -> %d"
                                % (rel, label, before, now.get(label, 0)))
    if loosened:
        print("Frame rules: clean, and some debt was paid off:")
        for item in loosened:
            print("  - %s" % item)
        print("\nRe-run with --update-baseline to lower the ratchet.")
        return 0

    print("Frame rules: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
