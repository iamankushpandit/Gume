# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""Build and run every host test in test/host/, and say which failed.

Each test states how it is built, in its own header comment, as the line a
person would paste:

    //   g++ -std=c++17 -O2 -Wall -Wextra -I src test/host/course_test.cpp -o course_test && ./course_test

That line is the specification, and this script runs it -- the compiler, the
flags and the sources exactly as written, with only the output path moved into
a scratch directory -- so the command a reader is told to use and the one CI
uses cannot come to disagree. There is no list of tests here either: every
test/host/*_test.cpp is a test, and one with no such line FAILS rather than
being skipped, because a test nobody builds is one that is already broken and
has not been told yet (the same reasoning as tools/envs.py).

Every test is built and run, whatever an earlier one did, and its full output
is printed as it runs. The summary at the end names each test once with its
result; the exit status is 1 if any test failed to build or to pass.

    python tools/run_host_tests.py                # all of them
    python tools/run_host_tests.py go_rules_test  # the ones named
    python tools/run_host_tests.py --list         # the commands, not run

Run it from anywhere; it works from the repository root. It needs g++ (or the
compiler named in $CXX, which replaces the leading `g++`), so on Windows run it
in WSL.
"""

from __future__ import annotations

import argparse
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEST_DIR = ROOT / "test" / "host"

# The build line: a `//` or ` *` comment, then `g++ ...`, then `&& ./whatever` (ignored).
BUILD_LINE = re.compile(r"^\s*(?://|\*)\s*(g\+\+\s.*)$")


def build_command(test: Path) -> list[str] | None:
    """The documented compile command for `test`, or None if it states none."""
    rel = test.relative_to(ROOT).as_posix()
    for line in test.read_text(encoding="utf-8").splitlines():
        m = BUILD_LINE.match(line)
        if not m:
            continue
        cmd = m.group(1).split("&&")[0].strip()
        args = shlex.split(cmd)
        if rel in args:
            return args
    return None


def with_output(args: list[str], out: Path) -> list[str]:
    """`args` with its -o pointed at `out` (added if it had none)."""
    args = list(args)
    if "-o" in args:
        i = args.index("-o")
        args[i + 1] = str(out)
    else:
        args += ["-o", str(out)]
    cxx = os.environ.get("CXX")
    if cxx:
        args[0:1] = shlex.split(cxx)
    return args


def banner(text: str) -> None:
    print(f"\n==== {text}", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("names", nargs="*", help="test names (file stem) to run; default all")
    ap.add_argument("--list", action="store_true", help="print each test's build command and stop")
    opts = ap.parse_args()

    tests = sorted(TEST_DIR.glob("*_test.cpp"))
    if opts.names:
        wanted = set(opts.names)
        unknown = wanted - {t.stem for t in tests}
        if unknown:
            print(f"no such host test: {', '.join(sorted(unknown))}", file=sys.stderr)
            return 2
        tests = [t for t in tests if t.stem in wanted]
    if not tests:
        print(f"no tests found in {TEST_DIR}", file=sys.stderr)
        return 2

    if opts.list:
        for t in tests:
            cmd = build_command(t)
            print(f"{t.stem}: {shlex.join(cmd) if cmd else 'NO BUILD LINE'}")
        return 0

    results: list[tuple[str, str, float]] = []
    with tempfile.TemporaryDirectory(prefix="braino-host-tests-") as tmp:
        for t in tests:
            name = t.stem
            start = time.monotonic()
            cmd = build_command(t)
            if cmd is None:
                banner(f"{name}: FAILED -- its header states no `g++ ... {t.name} ...` build line")
                results.append((name, "no build line", 0.0))
                continue
            exe = Path(tmp) / name
            cmd = with_output(cmd, exe)
            banner(f"{name}: building")
            print("$ " + shlex.join(cmd), flush=True)
            built = subprocess.run(cmd, cwd=ROOT)
            if built.returncode != 0:
                results.append((name, f"build failed ({built.returncode})", time.monotonic() - start))
                banner(f"{name}: BUILD FAILED, exit {built.returncode}")
                continue
            banner(f"{name}: running")
            ran = subprocess.run([str(exe)], cwd=ROOT)
            took = time.monotonic() - start
            if ran.returncode == 0:
                results.append((name, "passed", took))
                banner(f"{name}: passed in {took:.1f}s")
            else:
                results.append((name, f"FAILED (exit {ran.returncode})", took))
                banner(f"{name}: FAILED, exit {ran.returncode}")

    failed = [r for r in results if r[1] != "passed"]
    width = max(len(r[0]) for r in results)
    banner(f"host tests: {len(results) - len(failed)} passed, {len(failed)} failed, of {len(results)}")
    for name, result, took in results:
        print(f"  {name:<{width}}  {result:<22}  {took:6.1f}s")
    if failed and os.environ.get("GITHUB_ACTIONS") == "true":
        for name, result, _ in failed:
            print(f"::error title=Host test {name}::{name}: {result}")
    sys.stdout.flush()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
