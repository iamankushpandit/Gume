# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""Frame timing for every animation the site serves, with no 13 in it.

The maintainer's rule is that no animation shows the number 13, and that
covers its timing as well as its text: how long a frame is held, how many
frames there are, and how long one loop takes. The generators already refuse
a drawn string containing it; nothing checked the timing, and 42 published
GIFs held a frame for 1300 or 130 ms. Some of those holds were never written
by anybody: PIL merges identical consecutive frames when it saves a GIF and
adds their durations, so two 67 ms frames became one 130 ms hold.

So the merging happens here, before PIL sees the frames, where the result can
be checked: identical neighbours are folded into one frame, every hold is
stepped over the number, the frame count likewise, then the loop length by
adjusting the last hold. `check()` reads the written file back and refuses it
if anything slipped through. GIF stores time in hundredths of a second, so
every hold is rounded to 10 ms first -- that is what the file will say.
"""

from PIL import ImageChops

FORBIDDEN = "13"


def _bad(n):
    return FORBIDDEN in str(int(n))


def _clean(ms):
    """The nearest 10 ms multiple to ms whose value avoids the number."""
    ms = max(20, int(round(ms / 10.0)) * 10)
    for step in (0, 10, -10, 20, -20, 30, -30, 40, -40, 50, -50, 100, -100):
        cand = ms + step
        if cand >= 20 and not _bad(cand):
            return cand
    raise ValueError("no clean hold near %d ms" % ms)


def _same(a, b):
    if a.size != b.size or a.mode != b.mode:
        return False
    return ImageChops.difference(a.convert("RGB"), b.convert("RGB")).getbbox() is None


def prepare(frames, durations):
    """Return (frames, durations) safe to hand to PIL's GIF writer."""
    out_f, out_d = [], []
    for f, d in zip(frames, durations):
        if out_f and _same(out_f[-1], f):
            out_d[-1] += d
        else:
            out_f.append(f)
            out_d.append(d)
    # The frame count: fold the shortest frame into its predecessor until the
    # count is clean. A frame of a fraction of a second is all that is lost.
    while _bad(len(out_f)) and len(out_f) > 2:
        i = min(range(1, len(out_d)), key=lambda k: out_d[k])
        out_d[i - 1] += out_d[i]
        del out_f[i], out_d[i]
    out_d = [_clean(d) for d in out_d]
    # The loop length, in ms and in whole seconds, settled on the last hold.
    for step in range(0, 400, 10):
        for sign in (1, -1):
            last = out_d[-1] + sign * step
            total = sum(out_d[:-1]) + last
            if last >= 20 and not _bad(last) and not _bad(total) \
                    and not _bad(round(total / 1000.0)):
                out_d[-1] = last
                return out_f, out_d
    raise ValueError("could not find a loop length free of %s" % FORBIDDEN)


def check(path):
    """Read a written GIF back and refuse it if its timing shows the number."""
    from PIL import Image
    im = Image.open(path)
    holds = []
    try:
        while True:
            holds.append(im.info.get("duration", 0))
            im.seek(im.tell() + 1)
    except EOFError:
        pass
    total = sum(holds)
    problems = []
    if _bad(len(holds)):
        problems.append("%d frames" % len(holds))
    bad_holds = sorted({h for h in holds if _bad(h)})
    if bad_holds:
        problems.append("holds of %s ms" % bad_holds)
    if _bad(total) or _bad(round(total / 1000.0)):
        problems.append("a %d ms loop" % total)
    if problems:
        raise SystemExit("%s: timing shows %s -- %s"
                         % (path, FORBIDDEN, ", ".join(problems)))
