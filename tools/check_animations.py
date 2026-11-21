#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""No animation in this repository may show the number 13, in its timing too.

The generators refuse a drawn string containing it. What nobody checked was
the timing: on 2026-11-20, 42 published GIFs held a frame for 1300 or 130 ms,
some because PIL merges identical frames and adds their durations. This reads
every tracked GIF back and fails on a hold, a frame count or a loop length
(in ms or whole seconds) containing the number. tools/gif_timing.py is the
fix the generators use; retime an existing file with its prepare().
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gif_timing  # noqa: E402


def main():
    files = subprocess.run(["git", "ls-files", "*.gif"], cwd=ROOT,
                           capture_output=True, text=True, check=True).stdout.split()
    problems = []
    for rel in files:
        try:
            gif_timing.check(os.path.join(ROOT, rel))
        except SystemExit as e:
            problems.append(str(e))
    for p in problems:
        print("check_animations: " + p)
    if problems:
        return 1
    print("check_animations: clean (%d GIFs)" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
