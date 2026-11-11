#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""Animate the three boards on the landing page: each one launches a game.

    python tools/gen_board_launch.py

Reads site/assets/boards-braino.png -- LCDWIKI's board artwork with Braino's
launcher composited onto each screen -- and writes site/assets/
boards-launch.webp, in which each board shows its launcher, a finger taps a
tile, the game opens sideways (a game is drawn landscape, and these boards
stand portrait), the board turns a quarter to play it, Home is pressed, and
it turns back. The three run the same story a third of a loop apart.

Every screen is drawn by tools/gen_screens.py in the board's own theme, the
one the still already shows, and the games are gen_gameplay.py's sequences.
The screen rectangles below were found by fitting gen_screens' own launcher
render to the still (mean error under 6 levels), so the redrawn screens land
where the composited ones were. If boards-braino.png is ever recomposited,
refit them.

The still stays as it is: the page serves it to a reader whose system asks
for reduced motion. As with gen_gameplay.py, no frame may show the number 13.

Watch the result after regenerating; nothing else will say a board turned
the wrong way or a screen landed off its glass.
"""

import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import gen_gameplay as gg
import gen_screens as gs
from check_contrast import parse_palettes

ROOT = Path(__file__).resolve().parent.parent
STILL = ROOT / "site" / "assets" / "boards-braino.png"
OUT = ROOT / "site" / "assets" / "boards-launch.webp"

FPS = 12
LOOP = 10.0                   # seconds; each board's story fits inside it
OUT_W = 1400                  # the still is 2000 wide; the page shows ~700


def light_flags():
    return gg.quiz(gs.flags_country) + gg.tap(gs.flags_capital(),
                                              gs.flags_capital(True), (81, 198))


# Board box and screen rect are in the still's pixels. The launcher, the tile
# tapped (its centre in the launcher's own pixels) and the game it opens.
BOARDS = (
    {"theme": "Light", "box": (200, 93, 494, 595), "screen": (216, 153, 263, 351),
     "launcher": gs.launcher_tall, "tile": (62, 238), "game": light_flags,
     "offset": 0.0},
    {"theme": "Dark", "box": (838, 55, 1161, 646), "screen": (851, 111, 298, 447),
     "launcher": gs.launcher_tall_dense, "tile": (56, 382), "game": gg.trace,
     "offset": LOOP / 3},
    {"theme": "Pocket", "box": (1515, 120, 1782, 581), "screen": (1533, 181, 231, 308),
     "launcher": gs.launcher_tall, "tile": (178, 134), "game": lambda: gg.quiz(gs.money),
     "offset": 2 * LOOP / 3},
)
HOME = (16, 15)               # the Home button in a game's top bar


def ease(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def prepare(board, palettes):
    gs.apply_theme(palettes[board["theme"]])
    launcher = board["launcher"]()
    game = board["game"]()
    cap, kept, t = 5.6, [], 0.0
    for im, ms in game:                      # at most `cap` seconds of play
        if t >= cap:
            break
        kept.append((im, min(ms, int((cap - t) * 1000))))
        t += ms / 1000
    board["launcher_im"] = launcher
    board["game_frames"] = kept
    board["game_s"] = sum(ms for _, ms in kept) / 1000


def at_time(frames, t):
    for im, ms in frames:
        if t < ms / 1000:
            return im
        t -= ms / 1000
    return frames[-1][0]


def screen_state(board, t):
    """(screen image in the panel's portrait pixels, board angle) at t."""
    L = board["launcher_im"]
    sideways = lambda im: im.transpose(Image.ROTATE_90)
    marks = [1.2, 1.35, 1.55, 1.9, 2.5]
    if t < marks[0]:
        return L, 0
    if t < marks[1]:
        return gg.finger(L, board["tile"], "hover"), 0
    if t < marks[2]:
        return gg.finger(L, board["tile"], "press"), 0
    first = board["game_frames"][0][0]
    if t < marks[3]:
        return sideways(first), 0
    if t < marks[4]:
        return sideways(first), 90 * ease((t - marks[3]) / 0.6)
    play_end = marks[4] + board["game_s"]
    if t < play_end:
        return sideways(at_time(board["game_frames"], t - marks[4])), 90
    last = board["game_frames"][-1][0]
    if t < play_end + 0.15:
        return sideways(gg.finger(last, HOME, "hover")), 90
    if t < play_end + 0.35:
        return sideways(gg.finger(last, HOME, "press")), 90
    if t < play_end + 0.65:
        return L, 90
    if t < play_end + 1.25:
        return L, 90 * (1 - ease((t - play_end - 0.65) / 0.6))
    return L, 0


def cut_boards(still):
    """Each board as RGBA with the white card around it made transparent."""
    a = np.asarray(still).copy()
    for b in BOARDS:
        x0, y0, x1, y1 = b["box"]
        crop = a[y0:y1 + 1, x0:x1 + 1].copy()
        white = (np.abs(crop[:, :, :3].astype(int) - 255).sum(axis=2) <= 40)
        crop[white, 3] = 0
        b["art"] = Image.fromarray(crop, "RGBA")


def frame(t):
    canvas = Image.new("RGB", (2000, 702), (255, 255, 255))
    for b in BOARDS:
        local = (t - b["offset"]) % LOOP
        screen, angle = screen_state(b, local)
        art = b["art"].copy()
        sx, sy, sw, sh = b["screen"]
        x0, y0 = b["box"][:2]
        art.paste(screen.convert("RGB").resize((sw, sh), Image.LANCZOS), (sx - x0, sy - y0))
        if angle:
            art = art.rotate(-angle, resample=Image.BICUBIC, expand=True)
        cx = (b["box"][0] + b["box"][2]) / 2
        cy = (b["box"][1] + b["box"][3]) / 2
        canvas.paste(art, (round(cx - art.width / 2), round(cy - art.height / 2)), art)
    return canvas.resize((OUT_W, round(702 * OUT_W / 2000)), Image.LANCZOS)


def main():
    palettes = {p["name"]: p for p in parse_palettes()}
    gg._DRAWN.clear()
    for b in BOARDS:
        prepare(b, palettes)
    bad = sorted({s for s in gg._DRAWN for f in gg.FORBIDDEN if f in s})
    if bad:
        sys.exit("gen_board_launch.py: draws %s -- no animation may show %s"
                 % (", ".join(map(repr, bad)), " or ".join(gg.FORBIDDEN)))
    cut_boards(Image.open(STILL).convert("RGBA"))
    frames = [frame(i / FPS) for i in range(int(LOOP * FPS))]
    frames[0].save(OUT, save_all=True, append_images=frames[1:], loop=0,
                   duration=round(1000 / FPS), quality=80, method=6)
    print("wrote %s: %d frames, %d KB" % (OUT.name, len(frames), OUT.stat().st_size // 1024))


if __name__ == "__main__":
    main()
