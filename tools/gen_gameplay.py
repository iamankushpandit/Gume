#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""Animate the game mock-ups: a few seconds of each game being played.

    python tools/gen_gameplay.py [name ...]

Writes docs/gameplay/<still>.gif, one per playable game plus the launcher,
named after the still it animates (docs/screens/<still>.png). The site shows
the GIF on the game's card and on the Home wall, and serves the PNG instead
to a reader whose system asks for reduced motion.

Every frame is drawn by tools/gen_screens.py, by the same functions that draw
the stills -- the state each one shows is a parameter whose default is the
still -- so an animation cannot show a layout the stills do not. That also
means it inherits gen_screens' caveat: these are mock-ups, and a screen that
looks right here is not evidence about the panel.

A finger is drawn where the player touches. In a quiz the touch lands on the
button that turns green, found by watching which button gen_screens draws in
the success colour, so no coordinate is typed twice.

Look at every GIF after regenerating. Nothing else will tell you one came out
wrong.
"""

import random
import sys
from collections import deque
from contextlib import contextmanager
from pathlib import Path

from PIL import Image, ImageDraw

import gen_screens as gs

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "gameplay"
SCALE = 2                       # the stills are published at twice the panel

WAIT, AFTER = 1000, 1300        # ms on a question, ms on its answer

# The maintainer asked that no animation show the number 13, so every string
# drawn while a sequence is built is recorded and write() refuses one that
# contains it. A score, a counter or a total that would pass through it is
# chosen to step over it instead.
FORBIDDEN = ("13",)
_DRAWN = []
_text = ImageDraw.ImageDraw.text


def _recording_text(self, xy, text, *args, **kwargs):
    _DRAWN.append(str(text))
    return _text(self, xy, text, *args, **kwargs)


ImageDraw.ImageDraw.text = _recording_text


# ---------------------------------------------------------------- the finger

def finger(im, xy, state="press"):
    """A fingertip over the panel: approaching, pressing, or just lifted."""
    if xy is None:
        return im
    x, y = xy
    ov = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    if state == "hover":
        x, y = x + 5, y + 7
        d.ellipse([x - 11, y - 11, x + 11, y + 11], fill=(255, 255, 255, 70),
                  outline=(255, 255, 255, 170), width=2)
    elif state == "press":
        d.ellipse([x - 15, y - 15, x + 15, y + 15], outline=(255, 255, 255, 110), width=2)
        d.ellipse([x - 9, y - 9, x + 9, y + 9], fill=(255, 255, 255, 130),
                  outline=(255, 255, 255, 235), width=2)
    else:                       # lift: the ring left behind
        d.ellipse([x - 19, y - 19, x + 19, y + 19], outline=(255, 255, 255, 80), width=2)
    return Image.alpha_composite(im.convert("RGBA"), ov).convert("RGB")


def tap(before, after, xy, wait=WAIT, after_ms=AFTER):
    """Show `before`, touch at xy, show `after`."""
    return [(before, wait), (finger(before, xy, "hover"), 160),
            (finger(before, xy, "press"), 200), (finger(after, xy, "lift"), 140),
            (after, after_ms)]


def centre(r):
    x, y, w, h = r
    return (x + w // 2, y + h // 2)


@contextmanager
def unanswered(fill=None, ink=None):
    """Draw a screen as it is before the answer: every button gen_screens
    paints in the success colour is painted neutral instead, and its rect is
    kept so the finger knows where the right answer is."""
    real, hits = gs.button, []

    def neutral(d, r, label, fill_=None, tc=None, f=None):
        if fill_ == gs.SUCCESS:
            hits.append(r)
            fill_ = gs.PANEL if fill is None else fill
            tc = gs.TEXT if ink is None else ink
        real(d, r, label, fill_, tc, gs.F2 if f is None else f)

    gs.button = neutral
    try:
        yield hits
    finally:
        gs.button = real


def quiz(fn, fill=None, ink=None, after=None):
    """A question, a touch on the right answer, the answer shown."""
    with unanswered(fill, ink) as hits:
        before = fn()
    return tap(before, (after or fn)(), centre(hits[-1]))


# ---------------------------------------------------------------- the games

def launcher_wide():
    home = gs.launcher_wide()
    with unanswered() as hits:
        flag = gs.flags_country()
    return ([(home, 1400)] + tap(home, flag, (82, 127), wait=0, after_ms=WAIT)[1:]
            + tap(flag, gs.flags_country(), centre(hits[-1]), wait=0))


def flags_country():
    return (quiz(gs.flags_country)
            + tap(gs.flags_capital(), gs.flags_capital(True), (81, 198)))


def math():
    words = lambda: gs.math_words("Nia has 20 shells and gives 8 away. How many are left?",
                                  ("12", "11", "28", "14"), "12")
    return quiz(gs.math_game, gs.BLUE, gs.WHITE) + quiz(words, gs.BLUE, gs.WHITE)


def space():
    return quiz(gs.space, gs.BLUE, gs.WHITE, after=gs.space_answered)


def roman():
    return tap(gs.roman(False), gs.roman(), (84, 163))


def signs():
    """The course in four touches: open lesson 3, it teaches J, Next teaches
    K, and the lesson's quiz asks about a hand from it."""
    lessons, j, k = gs.signs_lessons(), gs.signs_teach("J"), gs.signs_teach("K")
    tile = (164, 60, 70, 58)                 # lesson 3, from signs_lessons()
    header = "Lesson 3 quiz: 1 of 6"
    ask = gs.signs_quiz("L", "ILKJ", None, header)
    right = gs.signs_quiz("L", "ILKJ", "L", header)
    return ([(lessons, 1400)] + tap(lessons, j, centre(tile), wait=0)[1:]
            + tap(j, k, centre(gs._sign_key(3)), wait=0)[1:]
            + [(ask, WAIT)] + tap(ask, right, centre(gs._sign_key(1)), wait=0)[1:])


def time():
    return tap(gs.time_game(), gs.time_game(True), (84, 169))


def grewords():
    study = gs.grewords_study()
    quiz_ = gs.grewords(False, "128 / 250")
    return ([(study, 1200)] + tap(study, quiz_, (240, 41), wait=0)[1:]
            + tap(quiz_, gs.grewords(True, "128 / 250"), (160, 103), wait=0))


def fingers():
    frames = quiz(gs.fingers_count)
    right = [False, False, True, False, False]
    left = (True, True, True, False, False)
    for i in (1, 3, 4):                         # raise three: 4 up becomes 7
        before = gs.fingers_show(left, tuple(right))
        right[i] = True
        x = 172 + i * 26 + 11
        frames += tap(before, gs.fingers_show(left, tuple(right)), (x, 128),
                      wait=500, after_ms=300)
    frames[-1] = (frames[-1][0], AFTER)
    return frames


def _tracer(title, tabs, active_tab, strokes, spacing, turns, dot=2, steps=10):
    frames = []
    ways = [gs._resample(p, spacing) for p in strokes]
    for si, way in enumerate(ways):
        for k in range(1, steps + 1):
            fraction = k / steps
            im, d = gs.blank()
            gs.topbar(d, title[0])
            gs._tracer_chrome(d, title[1], tabs, active_tab)
            gs._tracer_canvas(d, strokes, spacing, fraction, turns, active=si, dot=dot)
            tip = way[max(1, int(len(way) * fraction)) - 1]
            frames.append((finger(im, tip, "press"), 90))
    frames.append((im, AFTER))
    return frames


def trace():
    k = gs._box_scale(200, 200)
    return _tracer(("Trace", "A"), gs.TRACE_TABS, 0,
                   gs._glyph_strokes("TraceGlyphData.cpp", "A"), 20 * k, True)


def cursive():
    import re
    hdr = (ROOT / "src" / "games" / "CursiveGlyphData.h").read_text(encoding="utf-8")
    cw = int(re.search(r"CURSIVE_COORD_W = (\d+)", hdr).group(1))
    ch = int(re.search(r"CURSIVE_COORD_H = (\d+)", hdr).group(1))
    return _tracer(("Cursive", "dog"), ["ABC", "abc", "Words"], 2,
                   gs._glyph_strokes("CursiveGlyphData.cpp", "W_DOG", cw, ch), 14, False,
                   dot=1, steps=26)


def cinnamon():
    seq = (1, 3, 0)
    pad = [(30 + (i % 2) * 150 + 56, 74 + (i // 2) * 66 + 27) for i in range(4)]
    off = (False,) * 4
    frames = [(gs.cinnamon(off, "Watch", 4), 700)]
    for i in seq:
        frames += [(gs.cinnamon(tuple(j == i for j in range(4)), "Watch", 4), 450),
                   (gs.cinnamon(off, "Watch", 4), 160)]
    frames.append((gs.cinnamon(off, "Your turn", 4), 600))
    for n, i in enumerate(seq):
        lit = tuple(j == i for j in range(4))
        status = "Correct!" if n == len(seq) - 1 else "Your turn"
        frames += [(finger(gs.cinnamon(off, "Your turn", 4), pad[i], "hover"), 160),
                   (finger(gs.cinnamon(lit, "Your turn", 4), pad[i], "press"), 320),
                   (gs.cinnamon(off, status, 5 if status == "Correct!" else 4), 260)]
    frames[-1] = (frames[-1][0], AFTER)
    return frames


def memory():
    def at(i):
        return (18 + (i % 6) * 48 + 22, 55 + (i // 6) * 48 + 22)
    base = {2: "A", 3: "A"}
    frames = []
    a = gs.memory(base)
    b = gs.memory({**base, 7: "C"})
    frames += tap(a, b, at(7), wait=900, after_ms=300)
    c = gs.memory({**base, 7: "C", 20: "D"})
    frames += tap(b, c, at(20), wait=0, after_ms=900)
    frames += [(a, 500)]                       # no match: both turn back
    d1 = gs.memory({**base, 9: "F"})
    frames += tap(a, d1, at(9), wait=0, after_ms=300)
    d2 = gs.memory({**base, 9: "F", 16: "F"}, "Pairs 4/12")
    frames += tap(d1, d2, at(16), wait=0)
    return frames


def whack():
    gx, gy = (gs.W - 9 * 20) // 2, 58

    def at(r, c):
        return (gx + c * 20 + 10, gy + r * 20 + 10)
    frames = []
    score, clock = 14, 19
    script = [([(2, 3)], (2, 3)), ([(5, 6), (1, 7)], (5, 6)),
              ([(1, 7), (7, 1)], (7, 1)), ([(1, 7), (4, 4)], (4, 4))]
    for moles, hit in script:
        before = gs.whack(moles, "Score %d" % score, "0:%02d" % clock)
        score += 1
        clock -= 1
        left = [m for m in moles if m != hit]
        after = gs.whack(left, "Score %d" % score, "0:%02d" % clock)
        frames += tap(before, after, at(*hit), wait=450, after_ms=350)
    frames[-1] = (frames[-1][0], AFTER)
    return frames


def microku():
    grid = [[1, 0, 3, 0], [0, 4, 0, 2], [2, 0, 4, 0], [0, 3, 0, 1]]

    def cell(r, c):
        return (96 + c * 32 + 16, 58 + r * 32 + 16)

    def key(v):
        return (8 + (v - 1) * 58 + 23, 205 + 15)
    frames = []
    for (r, c), v in (((0, 1), 2), ((0, 3), 4), ((1, 0), 3)):
        before = gs.microku([row[:] for row in grid])
        picked = gs.microku([row[:] for row in grid], (r, c))
        frames += tap(before, picked, cell(r, c), wait=600, after_ms=300)
        grid[r][c] = v
        frames += tap(picked, gs.microku([row[:] for row in grid]), key(v),
                      wait=0, after_ms=500)
    frames[-1] = (frames[-1][0], AFTER)
    return frames


def maze():
    grid = gs.maze_levels()[3]
    start = next((r, row.index("S")) for r, row in enumerate(grid) if "S" in row)
    goal = next((r, row.index("E")) for r, row in enumerate(grid) if "E" in row)
    came, todo = {start: None}, deque([start])
    while todo:
        cur = todo.popleft()
        if cur == goal:
            break
        r, c = cur
        for nr, nc in ((r + 1, c), (r - 1, c), (r, c + 1), (r, c - 1)):
            if (0 <= nr < len(grid) and 0 <= nc < len(grid[0])
                    and grid[nr][nc] != "#" and (nr, nc) not in came):
                came[(nr, nc)] = cur
                todo.append((nr, nc))
    path, cur = [], goal
    while cur is not None:
        path.append(cur)
        cur = came[cur]
    path.reverse()
    mx, my = (gs.W - 12 * 22) // 2, 58

    def at(p):
        return (mx + p[1] * 22 + 11, my + p[0] * 22 + 11)
    frames = [(gs.maze(), 800)]
    for n, p in enumerate(path):
        if any(f in str(n) for f in FORBIDDEN):
            continue                            # the dot takes two cells here
        frames.append((finger(gs.maze(p, n), at(p), "press"), 110))
    frames.append((gs.maze(path[-1], len(path) - 1), AFTER))
    return frames


def sorting():
    order = [(0, 1), (1, 0), (1, 2), (0, 0), (0, 2), (1, 1)]
    vals = {(0, 0): 7, (0, 1): 2, (0, 2): 9, (1, 0): 4, (1, 1): 11, (1, 2): 6}
    frames = []
    for n, rc in enumerate(order):
        nxt = lambda k: "Next: %d" % vals[order[k]] if k < len(order) else "Done!"
        before = gs.sorting(tuple(order[:n]), nxt(n))
        after = gs.sorting(tuple(order[:n + 1]), nxt(n + 1))
        r, c = rc
        frames += tap(before, after, (24 + c * 92 + 38, 82 + r * 58 + 22),
                      wait=450 if n else 900, after_ms=200)
    frames[-1] = (frames[-1][0], AFTER)
    return frames


def slide():
    def at(i):
        return (88 + (i % 3) * 48 + 24, 68 + (i // 3) * 48 + 24)
    a = gs.slide([1, 2, 3, 4, 5, 6, 0, 7, 8], 14)
    b = gs.slide([1, 2, 3, 4, 5, 6, 7, 0, 8], 15)
    c = gs.slide([1, 2, 3, 4, 5, 6, 7, 8, 0], 16)
    return tap(a, b, at(7), after_ms=300) + tap(b, c, at(8), wait=300)


def oddone():
    return tap(gs.oddone(False, "Score 5/7"), gs.oddone(True, "Score 6/7"), (155, 140))


def tictactoe():
    def at(k):
        return (76 + (k % 3) * 56 + 28, 68 + (k // 3) * 56 + 28)
    marks, frames = {}, []
    for k, mk in ((0, "X"), (4, "O"), (8, "X"), (2, "O"), (6, "X"), (3, "O"), (7, "X")):
        before = gs.tictactoe(dict(marks), "%s turn" % mk)
        marks[k] = mk
        status = "X wins!" if k == 7 else "%s turn" % ("O" if mk == "X" else "X")
        frames += tap(before, gs.tictactoe(dict(marks), status), at(k),
                      wait=500, after_ms=250)
    frames[-1] = (frames[-1][0], AFTER)
    return frames


def piano():
    kb_x, kb_w = 4, gs.W - 8

    def at(i):
        return (kb_x + kb_w * i // 8 + kb_w // 16, 34 + 150)
    frames = [(gs.piano(None), 700)]
    for i in (0, 2, 4, 7, 4):
        frames += [(finger(gs.piano(None), at(i), "hover"), 120),
                   (finger(gs.piano(i), at(i), "press"), 380),
                   (gs.piano(None), 120)]
    frames[-1] = (frames[-1][0], AFTER)
    return frames


def chess():
    cell = (gs.H - 33 - 3) // 8

    def at(f, r):
        return (3 + f * cell + cell // 2, 33 + (7 - r) * cell + cell // 2)
    idle = gs.chess(None, [])
    picked = gs.chess()
    moved = gs.chess(None, [], [((3, 0), (7, 4))], ("Black", "to move"))
    return tap(idle, picked, at(3, 0), after_ms=900) + tap(picked, moved, at(7, 4), wait=0)


def seabattle():
    cell = (gs.H - 33 - 3) // 8 * 8 // 8      # SeaBattleGame: side // 8

    def at(c):
        return (3 + (c % 8) * cell + cell // 2, 33 + (c // 8) * cell + cell // 2)
    hits = {26, 27, 28, 35}
    a = gs.sea_battle(pending=set(), status=("Your turn", "pick a square", gs.TEXT))
    b = gs.sea_battle(pending={29}, status=("Firing...", "", gs.WARN))
    c = gs.sea_battle(hits=hits | {29}, pending=set(), tally="Hits 5/11  lost 3")
    return tap(a, b, at(29), after_ms=500) + [(c, AFTER)]


def ludo():
    c = gs.LUDO_CELL

    def at(rel):
        x, y = gs._ludo_cell_xy(gs._ludo_track(rel % 52))
        return (x + c // 2 + 1, y + c // 2 + 1)
    pos = {0: [None, 3, 20, 20], 1: [None, None, 8, 17], 2: [8, 30, 53, 56]}
    start = gs.ludo()
    frames = tap(start, start, at(3), after_ms=60)[:-1]
    for rel in (4, 5, 6, 7):
        p = {s: v[:] for s, v in pos.items()}
        p[0][1] = rel
        frames.append((gs.ludo(p, set(), "Moving"), 170))
    p[0][1] = 7
    frames.append((gs.ludo(p, set(), "Green's turn"), AFTER))
    return frames


def backgammon():
    def at(i):
        x, y = gs._bg_point_rect(i)
        return (x + gs.BG_COL // 2, y + gs.BG_PT_H // 2)
    none = frozenset()
    a = gs.backgammon(None, none)
    b = gs.backgammon()
    c = gs.backgammon(None, none, [(12, 7)])
    e = gs.backgammon(12, frozenset({9}), [(12, 7)])
    f = gs.backgammon(None, none, [(12, 7), (12, 9)])
    return (tap(a, b, at(12), after_ms=700) + tap(b, c, at(7), wait=0, after_ms=500)
            + tap(c, e, at(12), wait=0, after_ms=600) + tap(e, f, at(9), wait=0))


def go():
    x, y = gs._go_xy(2, 7)
    return tap(gs.go(), gs.go(True), (x, y), wait=1400, after_ms=1600)


def dice():
    rng = random.Random(7)
    frames = tap(gs.dice((6, 2, 4)), gs.dice((6, 2, 4)), (160, 215), after_ms=60)[:-1]
    while len(frames) < 11:
        faces = tuple(rng.randint(1, 6) for _ in range(3))
        if not any(f in str(sum(faces)) for f in FORBIDDEN):
            frames.append((gs.dice(faces), 80))
    frames.append((gs.dice((3, 4, 1)), AFTER + 300))
    return frames


def coinflip():
    import math as m
    a = gs.coinflip("HTHT", "T", 1.0, "2-2, one to go")
    frames = tap(a, a, (160, 215), after_ms=60)[:-1]
    for k in range(1, 15):
        sq = abs(m.cos(k * m.pi / 5))
        frames.append((gs.coinflip("HTHT", "H" if (k // 5) % 2 == 0 else "T", sq, ""), 70))
    frames.append((gs.coinflip(), AFTER + 300))
    return frames


def elements():
    col, row = gs.elem_position(8)
    x, y = gs._e_cell(col, row)
    im, d = gs.blank()
    gs.topbar(d, "Elements")
    gs._e_tabs(d, 0)
    gs._e_strip(d, "Tap any element")
    gs._e_table(d)
    picked = gs.elements()
    return (tap(im, picked, (x + gs.E_CW // 2, y + gs.E_CH // 2), after_ms=900)
            + tap(picked, gs.elements_card(), (160, 69), wait=0, after_ms=1600))


def shapes():
    names = [("red circle", "circle", False), ("blue square", "square", False),
             ("green triangle", "triangle", False), ("yellow star", "star", True)]
    order = [1, 0, 3, 2]

    def screen(matched, taps):
        return gs._shape_screen(1, names, order, matched, "Level 1   Taps %d   Best 8" % taps)

    def row(i):
        return (16 + 68, 58 + i * 43 + 19)

    def target(j):
        return (168 + 68, 58 + order.index(j) * 43 + 19)
    a, b, c = screen(set(), 0), screen({0}, 2), screen({0, 1}, 4)
    return (tap(a, a, row(0), after_ms=150) + tap(a, b, target(0), wait=0, after_ms=700)
            + tap(b, b, row(1), wait=0, after_ms=150) + tap(b, c, target(1), wait=0))


SEQUENCES = {
    "launcher-wide": launcher_wide,
    "flags-country": flags_country,
    "states": lambda: quiz(gs.states),
    "stateflags": lambda: quiz(gs.stateflags),
    "statemaps": lambda: quiz(gs.statemaps),
    "math": math,
    "multiply": lambda: quiz(gs.multiply),
    "space": space,
    "roman": roman,
    "signs": signs,
    "time": time,
    "money": lambda: quiz(gs.money, gs.BLUE, gs.TEXT),
    "fractions": lambda: quiz(gs.fractions),
    "counting": lambda: quiz(gs.counting),
    "shapearith": lambda: quiz(gs.shapearith),
    "numberline": lambda: quiz(gs.numberline),
    "calendar": lambda: quiz(gs.calendar),
    "colormix": lambda: quiz(gs.colormix),
    "percent": lambda: quiz(gs.percent),
    "grewords": grewords,
    "fingers-count": fingers,
    "trace": trace,
    "cursive": cursive,
    "cinnamon": cinnamon,
    "memory": memory,
    "whack": whack,
    "microku": microku,
    "maze": maze,
    "sorting": sorting,
    "slide": slide,
    "oddone": oddone,
    "tictactoe": tictactoe,
    "piano": piano,
    "chess": chess,
    "seabattle": seabattle,
    "ludo": ludo,
    "backgammon": backgammon,
    "go": go,
    "dice": dice,
    "coinflip": coinflip,
    "elements": elements,
    "shapes": shapes,
}


def write(name, frames):
    big = [im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)
           for im, _ in frames]
    # One palette per animation, from a sample of its frames, so the colours
    # hold still while things move and unchanged regions stay unchanged.
    sample = big[::max(1, len(big) // 10)] + [big[-1]]
    strip = Image.new("RGB", (big[0].width, big[0].height * len(sample)))
    for i, im in enumerate(sample):
        strip.paste(im, (0, im.height * i))
    palette = strip.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    gif = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in big]
    path = OUT / ("%s.gif" % name)
    gif[0].save(path, save_all=True, append_images=gif[1:], loop=0,
                duration=[ms for _, ms in frames], disposal=1, optimize=False)
    return path.stat().st_size


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    wanted = set(sys.argv[1:])
    unknown = wanted - set(SEQUENCES)
    if unknown:
        sys.exit("gen_gameplay.py: no sequence named %s" % ", ".join(sorted(unknown)))
    total = 0
    for name, build in SEQUENCES.items():
        if wanted and name not in wanted:
            continue
        _DRAWN.clear()
        frames = build()
        bad = sorted({s for s in _DRAWN for f in FORBIDDEN if f in s})
        if bad:
            sys.exit("gen_gameplay.py: %s draws %s -- no animation may show %s"
                     % (name, ", ".join(repr(b) for b in bad), " or ".join(FORBIDDEN)))
        size = write(name, frames)
        total += size
        print("  %-14s %3d frames %5.1fs %4d KB" % (
            name, len(frames), sum(ms for _, ms in frames) / 1000, size // 1024))
    print("wrote %d KB to %s" % (total // 1024, OUT))


if __name__ == "__main__":
    main()
