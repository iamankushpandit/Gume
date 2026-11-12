#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""Draw the animations on the site's Privacy and Get started cards.

    python tools/gen_site_anims.py [name ...]

Writes, into site/assets/, for every scene in SCENES (or only those named):

    <name>.gif   the animation the card shows
    <name>.png   its last frame, which the page serves instead to a reader
                 whose system asks for reduced motion

privacy-* go on the Privacy tab's cards, start-* on Get started's three
steps and three extras.

Every scene ends on a frame that holds the whole story at once, so that the
still is not a lesser version of the animation, only a quieter one.

The colours are the landing page's own (site/index.template.html, :root), and
the background is --ink-2, the Privacy section's, so a card shows no seam.
Every label drawn here is a claim -- about what the firmware sends, or about
what a board needs -- and the card beside it is where that claim is written in
words: change one and read the other. The facts behind the privacy scenes are
the README's Privacy section.

Look at every GIF after regenerating. Nothing else will say a label overlaps.
"""

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "site", "assets")

W, H = 640, 360          # output size; the card shows it at about half
S = 2                    # drawn at twice that, then reduced, for clean edges
FPS = 15
HOLD_MS = 2200           # the last frame stays up this long before the loop


def hex_rgb(value):
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


# The page's palette, :root in site/index.template.html.
INK = hex_rgb("#0b0c10")
BG = hex_rgb("#14161c")             # --ink-2, the Privacy section
YELLOW = hex_rgb("#ffd600")
PINK = hex_rgb("#d8336e")
TEAL = hex_rgb("#0e7c7b")
INDIGO = hex_rgb("#2f33b5")
GREEN = hex_rgb("#1f8a4c")
FG = (255, 255, 255)


def over(fg, bg, alpha):
    return tuple(round(f * alpha + b * (1 - alpha)) for f, b in zip(fg, bg))


MUTED = over(FG, BG, 0.66)          # --muted on --ink-2
LINE = over(FG, BG, 0.14)           # --line on --ink-2
PANEL = over(FG, BG, 0.05)
SOFT_PINK = (255, 120, 160)         # pink text that stays readable on ink
SOFT_GREEN = (88, 214, 140)


# ---------------------------------------------------------------- fonts

_FONTS = {}

# The maintainer asked that no animation show the number 13. Every string a
# scene draws goes through Canvas.text, which records it, and main() refuses
# to write a scene that drew one containing it.
FORBIDDEN = ("13",)
DRAWN = []


def font(size, bold=False, mono=False):
    key = (size, bold, mono)
    if key not in _FONTS:
        if mono:
            names = ("consolab.ttf", "DejaVuSansMono-Bold.ttf") if bold else (
                "consola.ttf", "DejaVuSansMono.ttf")
        else:
            names = ("segoeuib.ttf", "DejaVuSans-Bold.ttf") if bold else (
                "segoeui.ttf", "DejaVuSans.ttf")
        for name in names:
            try:
                _FONTS[key] = ImageFont.truetype(name, size * S)
                break
            except OSError:
                continue
        else:
            sys.exit("gen_privacy_anims.py: no usable font among %s" % (names,))
    return _FONTS[key]


# ---------------------------------------------------------------- timing

def clamp(x, lo=0.0, hi=1.0):
    return max(lo, min(hi, x))


def ease(x):
    x = clamp(x)
    return x * x * (3 - 2 * x)


def phase(t, start, length):
    """0 before start, 1 after start + length, eased in between."""
    return ease((t - start) / length)


def lerp(a, b, k):
    return a + (b - a) * k


def lerp_pt(a, b, k):
    return (lerp(a[0], b[0], k), lerp(a[1], b[1], k))


def fade(color, k, bg=BG):
    """color at strength k against the background (GIF has no alpha)."""
    return over(color, bg, clamp(k))


# ---------------------------------------------------------------- drawing

class Canvas:
    def __init__(self):
        self.image = Image.new("RGB", (W * S, H * S), BG)
        self.d = ImageDraw.Draw(self.image)

    def rrect(self, box, r=12, fill=None, outline=None, width=2):
        x0, y0, x1, y1 = box
        self.d.rounded_rectangle((x0 * S, y0 * S, x1 * S, y1 * S), radius=r * S,
                                 fill=fill, outline=outline,
                                 width=int(width * S) if outline else 0)

    def line(self, pts, color, width=2):
        self.d.line([(x * S, y * S) for x, y in pts], fill=color,
                    width=int(width * S), joint="curve")

    def dashed(self, a, b, color, width=2, dash=8, gap=7):
        length = math.dist(a, b)
        if length == 0:
            return
        pos = 0.0
        while pos < length:
            end = min(pos + dash, length)
            self.line([lerp_pt(a, b, pos / length), lerp_pt(a, b, end / length)],
                      color, width)
            pos = end + gap

    def circle(self, c, r, fill=None, outline=None, width=2):
        x, y = c
        self.d.ellipse(((x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S),
                       fill=fill, outline=outline,
                       width=int(width * S) if outline else 0)

    def arc(self, c, r, start, end, color, width=3):
        x, y = c
        self.d.arc(((x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S),
                   start, end, fill=color, width=int(width * S))

    def text(self, xy, s, size=18, color=FG, bold=False, mono=False, anchor="mm"):
        DRAWN.append(str(s))
        self.d.text((xy[0] * S, xy[1] * S), s, font=font(size, bold, mono),
                    fill=color, anchor=anchor)

    def text_w(self, s, size=18, bold=False, mono=False):
        return self.d.textlength(s, font=font(size, bold, mono)) / S

    def done(self):
        return self.image.resize((W, H), Image.LANCZOS)


def console(c, center, label=None, screen=None, screen_fill=INK, w=128, h=88,
            border=LINE, label_color=MUTED, screen_color=FG, screen_size=18):
    """The board: a dark case with a screen in it."""
    x, y = center
    c.rrect((x - w / 2, y - h / 2, x + w / 2, y + h / 2), 14, PANEL, border, 2)
    c.rrect((x - w / 2 + 10, y - h / 2 + 10, x + w / 2 - 10, y + h / 2 - 10), 7,
            screen_fill)
    if screen:
        lines = screen if isinstance(screen, (list, tuple)) else [screen]
        step = screen_size + 4
        top = y - step * (len(lines) - 1) / 2
        for i, s in enumerate(lines):
            c.text((x, top + i * step), s, screen_size, screen_color, bold=True)
    if label:
        c.text((x, y + h / 2 + 20), label, 20, label_color)


def tag(c, center, s, fill=YELLOW, color=INK, size=18, bold=True, border=None,
        mono=False, pad=12, height=32):
    w = c.text_w(s, size, bold, mono) + pad * 2
    x, y = center
    c.rrect((x - w / 2, y - height / 2, x + w / 2, y + height / 2), height / 2,
            fill, border, 2)
    c.text((x, y + 1), s, size, color, bold, mono)
    return w


def check(c, center, size, color, width=4):
    x, y = center
    c.line([(x - size * .5, y), (x - size * .12, y + size * .38),
            (x + size * .55, y - size * .4)], color, width)


def cross(c, center, size, color, width=4):
    x, y = center
    c.line([(x - size / 2, y - size / 2), (x + size / 2, y + size / 2)], color, width)
    c.line([(x - size / 2, y + size / 2), (x + size / 2, y - size / 2)], color, width)


def struck_chip(c, center, s, k=1.0):
    """A thing that is never sent: a pink-edged chip led by a pink cross. A
    line through the word was tried and made the word unreadable at the size
    the card shows it."""
    size, height = 20, 38
    x, y = center
    w = c.text_w(s, size) + 50
    c.rrect((x - w / 2, y - height / 2, x + w / 2, y + height / 2), height / 2,
            PANEL, fade(PINK, k), 2)
    cross(c, (x - w / 2 + 20, y), 11, fade(PINK, k), 3)
    c.text((x - w / 2 + 34, y + 1), s, size, fade(FG, k), anchor="lm")


def toggle(c, xy, on, label, label_color=MUTED):
    """A switch at xy (its left edge, vertical centre); on is 0..1."""
    x, y = xy
    track = over(YELLOW, PANEL, on)
    c.rrect((x, y - 14, x + 52, y + 14), 14, track, LINE, 2)
    knob_x = lerp(x + 14, x + 38, on)
    c.circle((knob_x, y), 10, over(INK, FG, on))
    c.text((x + 66, y), label, 21, label_color, anchor="lm")


def waves(c, origin, t, color, reach=200, gap=46, start=-38, end=38, speed=70,
          k=1.0):
    """Radio: arcs leaving origin, fading with distance."""
    if k <= 0:
        return
    offset = (t * speed) % gap
    r = offset + 18
    while r < reach:
        strength = (1 - r / reach) * k
        c.arc(origin, r, start, end, fade(color, strength), 3)
        r += gap


def rings(c, origin, t, color, reach=260, gap=55, speed=80, k=1.0):
    waves(c, origin, t, color, reach, gap, 0, 360, speed, k)


# ---------------------------------------------------------------- scenes
#
# Each scene is (seconds, draw(c, t)). The last frame must tell the whole
# story on its own: it is also the still.
#
# The card shows a scene at roughly half size -- 300 to 400 CSS pixels wide --
# so no label is drawn below 19px here, and a scene says only what a picture
# says better than the paragraph under it. The words are in the paragraph.

def scene_wifi(c, t):
    hub = (100, 104)
    console(c, hub, None, "12:41" if t > 2.0 else "--:--", w=140, h=96,
            screen_size=26)

    glow = phase(t, 7.4, 0.6)
    stays = (14, 224, 284, 352)
    c.rrect(stays, 14, PANEL, over(YELLOW, LINE, glow), 2)
    c.text((149, 246), "stays on the console", 20, over(YELLOW, MUTED, glow),
           bold=True)
    tag(c, (80, 282), "players", PANEL, FG, 20, False, LINE, height=34)
    tag(c, (204, 282), "scores", PANEL, FG, 20, False, LINE, height=34)
    tag(c, (149, 323), "Wi-Fi password", PANEL, FG, 20, False, LINE, height=34)

    dests = (
        (52, "Clock server", "the time", "time?", "12:41"),
        (176, "ip-api.com", "once: time zone", "zone?", "UTC-5"),
        (300, "Update list", "daily, no device id", "newer?", "5.14"),
    )
    start = (170, 104)
    for i, (y, title, sub, ask, reply) in enumerate(dests):
        box = (382, y - 42, 626, y + 42)
        end = (382, y)
        c.dashed(start, end, LINE)
        local = t - (0.3 + i * 2.35)
        active = 0 <= local <= 2.35
        c.rrect(box, 14, PANEL,
                YELLOW if active and 0.9 <= local <= 1.4 else LINE, 2)
        c.text((box[0] + 18, y - 13), title, 23, FG, True, anchor="lm")
        c.text((box[0] + 18, y + 17), sub, 19, MUTED, anchor="lm")
        if active:
            if local < 1.0:
                tag(c, lerp_pt(start, end, ease(local)), ask, size=21)
            elif local > 1.3:
                tag(c, lerp_pt(end, start, ease(local - 1.3)), reply, TEAL, FG,
                    size=21)
        if t > 7.4:
            k = phase(t, 7.4 + i * .15, .4)
            tag(c, lerp_pt(start, end, .5), ask, fade(YELLOW, k),
                fade(INK, k, PANEL), size=21)


def scene_ble(c, t):
    on = phase(t, 1.5, .4)
    hub = (104, 96)
    console(c, hub, None, "A4F2" if on > .5 else None, w=140, h=96,
            screen_size=24)
    toggle(c, (40, 196), on, "Bluetooth")
    c.text((104, 244), "off by default" if on < .5 else "switched on", 21,
           SOFT_PINK if on < .5 else YELLOW, bold=True)
    waves(c, (174, 96), t, YELLOW, reach=100, k=phase(t, 1.9, .5))

    k = phase(t, 2.3, .5)
    if k > 0:
        x, y = 440, 50
        name, short, size = "Braino-", "A4F2", 26
        w1, w2 = c.text_w(name, size, True), c.text_w(short, size, True)
        w = w1 + w2 + 36
        c.rrect((x - w / 2, y - 26, x + w / 2, y + 26), 26, PANEL,
                fade(YELLOW, k, PANEL), 2)
        c.text((x - w / 2 + 18, y), name, size, fade(FG, k), True, anchor="lm")
        c.text((x - w / 2 + 18 + w1, y), short, size, fade(YELLOW, k), True,
               anchor="lm")
        c.text((x, y + 44), "on air", 19, fade(MUTED, k))

    panel = (226, 128, 626, 346)
    pk = phase(t, 2.8, .4)
    if pk > 0:
        c.rrect(panel, 14, fade(INK, pk), fade(LINE, pk), 2)
        c.text((246, 154), "shown on its own screen", 19, fade(MUTED, pk),
               anchor="lm")
        rows = (("02 01 06", ""), ("0C 09 42 72 61 69", ""),
                ("6E 6F 2D ", "41 34 46 32"))
        typed = (t - 3.0) / 1.6
        for i, (plain, hot) in enumerate(rows):
            rk = clamp(typed * len(rows) - i)
            if rk <= 0:
                continue
            y = 198 + i * 40
            c.text((246, y), plain, 24, fade(FG, rk), mono=True, anchor="lm")
            if hot:
                off = c.text_w(plain, 24, mono=True)
                c.text((246 + off, y), hot, 24, fade(YELLOW, rk), True, True,
                       anchor="lm")
        ek = phase(t, 4.6, .4)
        c.text((246, 318), '= "Braino-A4F2"', 21, fade(MUTED, ek), mono=True,
               anchor="lm")


def scene_nearby(c, t):
    console(c, (100, 92), None, "Maze", w=140, h=96, screen_size=24)
    toggle(c, (36, 190), 1, "Beacon")
    second = phase(t, 0.9, .4)
    toggle(c, (36, 238), second, "Nearby")
    c.text((100, 290), "off by default" if second < .5 else "switched on", 21,
           SOFT_PINK if second < .5 else YELLOW, bold=True)
    waves(c, (170, 92), t, YELLOW, reach=86, k=phase(t, 1.2, .5))

    for i, (s, at) in enumerate((("A4F2", 0.0), ("game: Maze", 1.6),
                                 ("best: 18", 2.2))):
        k = phase(t, at, .4)
        if k > 0:
            tag(c, (342, 44 + i * 50), s, fade(YELLOW, k), fade(INK, k), 22)

    never = phase(t, 3.2, .5)
    if never > 0:
        y = lerp(380, 296, never)
        struck_chip(c, (322, y), "player name", phase(t, 3.8, .5))
        struck_chip(c, (494, y), "profile", phase(t, 3.9, .5))
        c.text((408, 246), "never on air", 23,
               fade(SOFT_PINK, phase(t, 4.2, .4)), True)

    peer = phase(t, 4.8, .4)
    console(c, (552, 92), None, ["A4F2", "Maze · 18"] if peer > .5 else None,
            w=136, h=96, screen_size=20)
    c.text((552, 166), "another console", 19, MUTED)


def broadcast(c, origin, t, start, stop, reach):
    """Rings leaving origin while a message is going out, then held still and
    faint: moving rings repaint the whole frame, which is what makes a GIF
    heavy, and the still has to show that it was a broadcast."""
    if t < start:
        return
    k = 1 - .65 * phase(t, stop - .3, .6)
    rings(c, origin, min(t, stop), YELLOW, reach=reach, gap=60, k=k)


def scene_games(c, t):
    a, b, by = (88, 92), (530, 92), (300, 278)
    broadcast(c, a, t, .3, 3.2, 300)
    console(c, a, None, w=132, h=92)
    c.text((88, 162), "you", 21, MUTED)
    console(c, b, None, w=132, h=92)
    c.text((530, 162), "opponent", 21, MUTED)
    console(c, by, None, w=120, h=84)
    done = phase(t, 3.2, .4)
    if done > 0:
        check(c, b, 34, fade(SOFT_GREEN, done), 6)
        c.line([(by[0] - 14, by[1]), (by[0] + 14, by[1])], fade(MUTED, done), 5)
        c.text((530, 194), "checks it on its board", 20, fade(SOFT_GREEN, done))
        c.text((376, 264), "anyone else in range", 20, fade(FG, done), True,
               anchor="lm")
        c.text((376, 294), "hears it, plays nothing", 20, fade(MUTED, done),
               anchor="lm")

    move = "move 12: 12→28"
    centre = (309, 92)
    if t < 2.0:
        tag(c, lerp_pt((160, 92), centre, phase(t, .3, 1.6)), move, size=22)
    else:
        tag(c, centre, move, size=22)
        split = phase(t, 2.0, 1.1)
        if split < 1:
            c.circle(lerp_pt((380, 92), (458, 92), split), 7, YELLOW)
            c.circle(lerp_pt((309, 114), (309, 232), split), 7, YELLOW)

    dk = phase(t, 4.4, .5)
    if dk > 0:
        x, y = 88, 262
        c.rrect((x - 26, y - 26, x + 26, y + 26), 10, fade(FG, dk), None)
        for dx, dy in ((-12, -12), (12, 12), (0, 0)):
            c.circle((x + dx, y + dy), 5, fade(INK, dk, FG))
        c.line([(x - 36, y + 36), (x + 36, y - 36)], fade(PINK, dk), 6)
        c.text((x, y + 60), "dice never sent", 20, fade(SOFT_PINK, dk), True)


def scene_poke(c, t):
    sender = (92, 180)
    broadcast(c, sender, t, .3, 2.6, 330)
    console(c, sender, None, w=132, h=92)
    c.text((92, 250), "you", 21, MUTED)

    poke = "poke → B7C1"
    centre = (282, 180)
    if t < 2.4:
        tag(c, lerp_pt((170, 180), centre, phase(t, .3, 1.2)), poke, size=22)
    else:
        tag(c, centre, poke, size=22)

    reached = phase(t, 2.4, .9)
    react = t > 3.4
    for name, y in (("7C10", 56), ("B7C1", 180), ("33E9", 304)):
        mine = name == "B7C1"
        lit = mine and react
        shake = math.sin(t * 40) * 5 * (1 - phase(t, 4.6, .3)) if lit else 0
        console(c, (552 + shake, y), None, name, w=136, h=84,
                screen_fill=YELLOW if lit else INK,
                screen_color=INK if lit else FG,
                border=YELLOW if lit else LINE, screen_size=22)
        if 0 < reached < 1:
            c.circle(lerp_pt((364, 180), (480, y), reached), 7, YELLOW)
        if react:
            c.text((470, y), "rings" if mine else "ignores", 21,
                   YELLOW if mine else MUTED, bold=mine, anchor="rm")


def scene_names(c, t):
    console(c, (92, 84), None, "A4F2", w=132, h=92, screen_size=22)
    waves(c, (158, 84), t, YELLOW, reach=90)

    air = (252, 14, 626, 178)
    c.rrect(air, 14, INK, LINE, 2)
    c.text((272, 40), "on air", 19, MUTED, anchor="lm")
    for i, (label, at) in enumerate((("before", 0.4), ("after a label", 3.6))):
        k = phase(t, at, .5)
        if k <= 0:
            continue
        y = 82 + i * 40
        c.text((272, y), label, 20, fade(MUTED, k), anchor="lm")
        c.text((606, y), "Braino-A4F2", 22, fade(FG, k), True, True, anchor="rm")
    sk = phase(t, 4.3, .4)
    if sk > 0:
        check(c, (420, 154), 20, fade(SOFT_GREEN, sk), 5)
        c.text((438, 154), "identical", 21, fade(SOFT_GREEN, sk), True,
               anchor="lm")

    book = (14, 196, 330, 346)
    c.rrect(book, 14, PANEL, LINE, 2)
    c.text((172, 222), "saved on this console only", 19, MUTED)
    lk = phase(t, 3.2, .5)
    if lk > 0:
        tag(c, (172, 268), "B7C1 = Blue case", fade(YELLOW, lk), fade(INK, lk),
            22)
    else:
        c.text((172, 268), "no labels yet", 21, MUTED)
    tag(c, (172, 316), "admin only", PANEL, MUTED, 19, False, LINE, height=34)

    xk = phase(t, 4.6, .5)
    if xk > 0:
        c.dashed((330, 268), (380, 268), fade(LINE, xk))
        cross(c, (398, 268), 24, fade(PINK, xk), 5)
        c.text((422, 254), "the beacon", 21, fade(SOFT_PINK, xk), True,
               anchor="lm")
        c.text((422, 284), "never reads it", 21, fade(SOFT_PINK, xk), True,
               anchor="lm")


def scene_scores(c, t):
    c.rrect((14, 18, 360, 342), 18, PANEL, YELLOW, 2)
    c.text((36, 54), "in its own flash", 23, YELLOW, True, anchor="lm")
    players = (("Player 1", .82, YELLOW), ("Player 2", .55, TEAL),
               ("Player 3", .68, INDIGO))
    for i, (name, full, color) in enumerate(players):
        y = 114 + i * 64
        c.text((36, y), name, 21, FG, anchor="lm")
        c.rrect((150, y - 12, 336, y + 12), 12, INK)
        k = phase(t, .3 + i * .35, 1.6)
        if k > 0:
            c.rrect((150, y - 12, 150 + 186 * full * k, y + 12), 12, color)
    sk = phase(t, 2.6, .4)
    if sk > 0:
        c.text((306, 306), "saved", 21, fade(SOFT_GREEN, sk), True, anchor="rm")
        check(c, (324, 306), 20, fade(SOFT_GREEN, sk), 5)

    cx, cy = 510, 104
    for dx, dy, r in ((-36, 8, 28), (0, -10, 36), (38, 6, 28)):
        c.circle((cx + dx, cy + dy), r, PANEL, LINE, 2)
    c.rrect((cx - 64, cy + 4, cx + 66, cy + 34), 15, PANEL)
    c.text((cx, cy + 6), "accounts", 20, MUTED)

    c.dashed((360, 180), (500, 180), LINE)
    xk = phase(t, 3.2, .5)
    if xk > 0:
        cross(c, (432, 180), 26, fade(PINK, xk), 6)
    for i, s in enumerate(("no account", "no sign-in", "no upload")):
        k = phase(t, 3.6 + i * .3, .4)
        c.text((500, 236 + i * 36), s, 22, fade(FG, k), bold=i == 0)


def scene_page(c, t):
    win = (12, 96, 352, 346)
    c.rrect(win, 14, PANEL, LINE, 2)
    for i, col in enumerate((PINK, YELLOW, GREEN)):
        c.circle((34 + i * 18, 120), 5, col)
    c.rrect((94, 106, 338, 134), 14, INK)
    c.text((110, 120), "github.io/Gume", 19, MUTED, anchor="lm")
    c.rrect((106, 150, 258, 190), 20, YELLOW)
    c.text((182, 170), "Install", 22, INK, True)

    for i, (y, title, sub) in enumerate(((10, "GitHub Pages", "page + firmware"),
                                        (90, "unpkg", "installer code"))):
        c.rrect((376, y, 626, y + 68), 12, PANEL, LINE, 2)
        c.text((394, y + 24), title, 22, FG, True, anchor="lm")
        c.text((394, y + 50), sub, 19, MUTED, anchor="lm")
        start, end = (376, y + 34), (352, 130 + i * 20)
        c.dashed(start, end, LINE)
        k = phase(t, .3 + i * .5, 1.0)
        if 0 < k < 1:
            c.circle(lerp_pt(start, end, k), 7, TEAL)

    console(c, (560, 266), None, w=124, h=84)
    c.text((560, 328), "your board", 20, MUTED)
    c.line([(352, 266), (498, 266)], MUTED, 4)
    serial = phase(t, 2.2, .4)
    if serial > 0:
        for i in range(3):
            pos = (t * 0.9 + i / 3) % 1
            c.circle(lerp_pt((356, 266), (494, 266), pos), 6, fade(YELLOW, serial))
        c.text((425, 232), "Web Serial", 21, fade(YELLOW, serial), True)

    for i, (x, y, s) in enumerate(((104, 224, "analytics"), (262, 224, "cookies"),
                                   (182, 270, "web fonts"))):
        k = phase(t, 3.6 + i * .35, .5)
        if k > 0:
            struck_chip(c, (x, y), s, k)
    c.text((182, 318), "your board's details stay here", 20,
           fade(FG, phase(t, 4.9, .4)))


# ---------------------------------------------------------------- Get started

PCB = YELLOW                          # a Cheap Yellow Display is yellow
PCB_EDGE = over(INK, YELLOW, .25)
SILVER = (196, 200, 208)
TILES = (PINK, TEAL, INDIGO, GREEN, (242, 169, 0), (91, 42, 134))


def screen_tiles(c, box, k=1.0):
    """The launcher, as six coloured tiles: what Braino looks like at a glance."""
    x0, y0, x1, y1 = box
    gw, gh = (x1 - x0 - 16) / 3, (y1 - y0 - 12) / 2
    for i, col in enumerate(TILES):
        tx = x0 + 4 + (i % 3) * (gw + 4)
        ty = y0 + 4 + (i // 3) * (gh + 4)
        c.rrect((tx, ty, tx + gw, ty + gh), 5, fade(col, k, INK))


def cyd(c, center, w=200, h=150, side="front", squash=1.0, screen=None):
    """The board, front (screen) or back (the ESP32 and the USB socket).
    squash < 1 narrows it, for the flip."""
    x, y = center
    hw = w / 2 * max(squash, .02)
    c.rrect((x - hw, y - h / 2, x + hw, y + h / 2), 12, PCB, PCB_EDGE, 2)
    if hw < 14:
        return
    sx = hw / (w / 2)
    if side == "front":
        box = (x - hw + 14 * sx, y - h / 2 + 14, x + hw - 30 * sx, y + h / 2 - 14)
        c.rrect(box, 6, INK)
        if screen == "tiles" and squash > .6:
            screen_tiles(c, (box[0] + 6, box[1] + 6, box[2] - 6, box[3] - 6))
        elif screen and squash > .6:
            c.text(((box[0] + box[2]) / 2, (box[1] + box[3]) / 2), screen, 22,
                   YELLOW, True)
    else:
        mw, mh = w * .42 * sx, h * .5
        mod = (x - mw / 2 - 10 * sx, y - mh / 2 - 8, x + mw / 2 - 10 * sx, y + mh / 2 - 8)
        c.rrect(mod, 4, SILVER, over(INK, SILVER, .4), 2)
        if squash > .6:
            c.text(((mod[0] + mod[2]) / 2, (mod[1] + mod[3]) / 2), "ESP32", 20,
                   INK, True)
        usb = (x - 18 * sx, y + h / 2 - 12, x + 18 * sx, y + h / 2 + 4)
        c.rrect(usb, 4, SILVER, over(INK, SILVER, .4), 2)
        for i in range(3):
            px = x + hw - 26 * sx
            c.rrect((px, y - 40 + i * 22, px + 12 * sx, y - 30 + i * 22), 2, INK)


def scene_board(c, t):
    # Flip front, back, front in the middle; then front and back side by side.
    split = phase(t, 3.6, .8)
    if split < 1:
        angle = clamp((t - .9) / 2.4) * 2 * math.pi        # one full turn
        cos = math.cos(angle)
        side = "front" if cos >= 0 else "back"
        pos = lerp_pt((320, 160), (176, 160), split)
        cyd(c, pos, side=side, squash=abs(cos), screen="tiles")
    else:
        cyd(c, (176, 160), side="front", screen="tiles")
    if split > 0:
        cyd(c, (464, 160), side="back", squash=split)
        k = phase(t, 4.2, .5)
        c.text((176, 262), "the touchscreen", 21, fade(FG, k), True)
        c.text((464, 262), "the computer, ESP32", 21, fade(FG, k), True)
        c.text((464, 290), "already on the back", 19, fade(MUTED, k))
    nk = phase(t, 4.8, .5)
    if nk > 0:
        check(c, (210, 330), 20, fade(SOFT_GREEN, nk), 5)
        c.text((230, 330), "nothing to solder or wire", 21, fade(SOFT_GREEN, nk),
               True, anchor="lm")


def laptop(c, center, screen=None, screen_color=FG):
    x, y = center
    c.rrect((x - 110, y - 80, x + 110, y + 50), 12, PANEL, LINE, 2)
    c.rrect((x - 98, y - 68, x + 98, y + 38), 6, INK)
    c.rrect((x - 136, y + 50, x + 136, y + 64), 7, LINE)
    if screen:
        lines = screen if isinstance(screen, (list, tuple)) else [screen]
        for i, s in enumerate(lines):
            c.text((x, y - 15 + (i - (len(lines) - 1) / 2) * 30), s, 21,
                   screen_color, True)


def scene_plug(c, t):
    data = t > 3.4
    swap = phase(t, 3.0, .4)
    found = phase(t, 4.4, .3)
    if not data:
        laptop(c, (150, 130), ["no board", "found"], SOFT_PINK if t > 1.6 else MUTED)
    else:
        laptop(c, (150, 130), ["board", "found"] if found > .5 else None,
               SOFT_GREEN)
    cyd(c, (530, 130), w=160, h=120, screen="tiles" if t > .9 else None)

    a, b = (260, 150), (450, 150)
    c.line([a, b], over(PINK, BG, 1 - swap) if not data else SOFT_GREEN, 5)
    if not data:
        for i in range(3):
            pos = (t * .8 + i / 3) % 1
            c.circle(lerp_pt((272, 150), (438, 150), pos), 7, YELLOW)
        c.text((355, 212), "charge-only:", 20,
               fade(SOFT_PINK, phase(t, 1.6, .4)), True)
        c.text((355, 240), "power, no data", 20,
               fade(SOFT_PINK, phase(t, 1.6, .4)), True)
    else:
        for i in range(3):
            pos = (t * .8 + i / 3) % 1
            c.circle(lerp_pt((272, 140), (438, 140), pos), 7, YELLOW)
            c.circle(lerp_pt((438, 160), (272, 160), pos), 7, TEAL)
        c.text((355, 212), "data cable:", 20, SOFT_GREEN, True)
        c.text((355, 240), "both ways", 20, SOFT_GREEN, True)

    fk = phase(t, 5.2, .5)
    if fk > 0:
        struck_chip(c, (196, 316), "charge-only cable", fk)
        w = c.text_w("data cable", 20) + 50
        x, y = 454, 316
        c.rrect((x - w / 2, y - 19, x + w / 2, y + 19), 19, PANEL,
                fade(SOFT_GREEN, fk), 2)
        check(c, (x - w / 2 + 20, y), 14, fade(SOFT_GREEN, fk), 3)
        c.text((x - w / 2 + 34, y + 1), "data cable", 20, fade(FG, fk), anchor="lm")


def scene_install(c, t):
    win = (14, 28, 360, 300)
    c.rrect(win, 14, PANEL, LINE, 2)
    for i, col in enumerate((PINK, YELLOW, GREEN)):
        c.circle((36 + i * 18, 52), 5, col)
    c.rrect((96, 38, 346, 66), 14, INK)
    c.text((112, 52), "Chrome or Edge", 19, MUTED, anchor="lm")

    pressed = 0.8 <= t <= 1.1
    c.rrect((90, 96, 284, 142), 23, over(INK, YELLOW, .25) if pressed else YELLOW)
    c.text((187, 119), "Install", 23, INK, True)
    if t < 1.3:
        cx, cy = lerp_pt((320, 220), (210, 128), phase(t, .1, .7))
        c.d.polygon([(cx * S, cy * S), ((cx + 0) * S, (cy + 26) * S),
                     ((cx + 7) * S, (cy + 20) * S), ((cx + 18) * S, (cy + 20) * S)],
                    fill=FG)

    prog = clamp((t - 1.3) / 3.2)
    if t > 1.3:
        c.rrect((40, 178, 334, 202), 12, INK)
        c.rrect((40, 178, 40 + 294 * prog, 202), 12, YELLOW)
        label = "about a minute" if prog < 1 else "done"
        c.text((187, 232), label, 21, FG if prog < 1 else SOFT_GREEN, True)
        if prog >= 1:
            check(c, (130, 232), 18, SOFT_GREEN, 4)

    c.line([(360, 200), (420, 200)], MUTED, 4)
    boot = phase(t, 4.6, .4)
    if boot <= 0:
        screen = None
    elif t < 5.4:
        screen = "Braino!"
    else:
        screen = "tiles"
    cyd(c, (518, 170), w=196, h=148, screen=screen)
    c.text((518, 268), "boots into Braino", 20, fade(FG, phase(t, 5.4, .4)), True)

    pk = phase(t, 5.8, .5)
    if pk > 0:
        struck_chip(c, (187, 334), "phone or tablet", pk)


def speaker_drum(c, center, r=46):
    x, y = center
    c.circle((x, y), r, PANEL, LINE, 3)
    c.circle((x, y), r * .62, over(FG, PANEL, .12), LINE, 2)
    c.circle((x, y), r * .22, MUTED)


def scene_speaker(c, t):
    cyd(c, (200, 150), w=260, h=190, side="back")
    sock = (300, 202)
    c.rrect((sock[0] - 22, sock[1] - 12, sock[0] + 22, sock[1] + 12), 4, FG,
            over(INK, FG, .4), 2)
    c.text((sock[0] - 30, sock[1]), "SPK", 19, INK, True, anchor="rm")

    plug_in = phase(t, .5, 1.6)
    plug = lerp_pt((450, 290), (sock[0] - 16, sock[1]), plug_in)
    drum = (490, 136)
    c.line([(drum[0] - 30, drum[1] + 36), (plug[0] + 40, plug[1] + 4)],
           (60, 60, 60), 5)
    c.line([(drum[0] - 26, drum[1] + 40), (plug[0] + 40, plug[1] - 4)], PINK, 4)
    c.rrect((plug[0] - 6, plug[1] - 10, plug[0] + 40, plug[1] + 10), 3, FG,
            over(INK, FG, .4), 2)
    speaker_drum(c, drum)

    sound = phase(t, 2.3, .4)
    waves(c, (drum[0] + 40, drum[1]), t, YELLOW, reach=96, start=-50, end=50,
          k=sound)
    c.text((200, 290), "two pins, 1.25 mm", 21, fade(FG, phase(t, 2.3, .4)), True)
    c.text((200, 322), "none? it plays silently", 20, fade(MUTED, phase(t, 3.2, .4)))


def scene_case(c, t):
    come = phase(t, .6, 2.2)
    centre = (230, 172)
    back = lerp_pt((150, 236), centre, come)
    board = lerp_pt((230, 172), centre, come)
    front = lerp_pt((310, 108), centre, come)
    w, h = 250, 186
    bx, by = back
    c.rrect((bx - w / 2 - 12, by - h / 2 - 12, bx + w / 2 + 12, by + h / 2 + 12),
            22, over(TEAL, BG, .7), over(FG, TEAL, .2), 2)
    cyd(c, board, w=w, h=h, screen="tiles")
    fx, fy = front
    frame = over(TEAL, BG, .9)
    m = 12
    outer = (fx - w / 2 - m, fy - h / 2 - m, fx + w / 2 + m, fy + h / 2 + m)
    win = (fx - w / 2 + 14, fy - h / 2 + 14, fx + w / 2 - 30, fy + h / 2 - 14)
    for box in ((outer[0], outer[1], outer[2], win[1]),
                (outer[0], win[3], outer[2], outer[3]),
                (outer[0], outer[1], win[0], outer[3]),
                (win[2], outer[1], outer[2], outer[3])):
        c.rrect(box, 0, frame)
    c.rrect(outer, 22, None, over(FG, TEAL, .25), 2)

    labels = (("no screws", 3.2), ("no supports", 3.6), ("2.8, 3.2 and 4-inch", 4.0))
    for i, (s, at) in enumerate(labels):
        k = phase(t, at, .4)
        if k > 0:
            y = 112 + i * 52
            check(c, (424, y), 18, fade(SOFT_GREEN, k), 4)
            c.text((444, y), s, 21, fade(FG, k), i < 2, anchor="lm")
    c.text((520, 290), "optional", 20, fade(MUTED, phase(t, 4.4, .4)))


def battery(c, center, level):
    x, y = center
    c.rrect((x - 80, y - 46, x + 80, y + 46), 12, PANEL, LINE, 2)
    c.rrect((x - 68, y - 34, x - 68 + 136 * level, y + 34), 8,
            over(SOFT_GREEN, PANEL, .8))
    c.text((x, y), "3.7 V", 22, FG, True)


def scene_battery(c, t):
    # The socket on the board, with its + and - marks.
    c.rrect((24, 60, 300, 300), 18, PCB, PCB_EDGE, 2)
    sock = (210, 180)
    c.rrect((sock[0] - 22, sock[1] - 40, sock[0] + 22, sock[1] + 40), 5, FG,
            over(INK, FG, .4), 2)
    c.text((sock[0] - 50, sock[1] - 20), "+", 30, INK, True)
    c.text((sock[0] - 50, sock[1] + 20), "−", 30, INK, True)

    wrong = t < 3.0
    if wrong:
        come = phase(t, .3, 1.0) * (1 - phase(t, 2.2, .6))
        stop = 64                    # never reaches the socket
    else:
        come = phase(t, 3.2, 1.0)
        stop = 26
    px = lerp(420, sock[0] + stop, come)
    top_col, bottom_col = ((40, 40, 40), (214, 46, 46)) if wrong else (
        (214, 46, 46), (40, 40, 40))
    cell = (520, 180)
    c.line([(px + 22, sock[1] - 14), (cell[0] - 80, cell[1] - 14)], top_col, 6)
    c.line([(px + 22, sock[1] + 14), (cell[0] - 80, cell[1] + 14)], bottom_col, 6)
    c.rrect((px - 4, sock[1] - 30, px + 26, sock[1] + 30), 4, FG,
            over(INK, FG, .4), 2)

    level = .25 + .7 * phase(t, 5.6, 1.8)
    battery(c, cell, level)

    if wrong and t > 1.1:
        k = phase(t, 1.1, .3) * (1 - phase(t, 2.6, .3))
        cross(c, (380, 110), 28, fade(PINK, k), 6)
        c.text((404, 110), "red to −: stop", 21, fade(SOFT_PINK, k), True,
               anchor="lm")
    if not wrong:
        k = phase(t, 4.2, .4)
        if k > 0:
            check(c, (380, 110), 26, fade(SOFT_GREEN, k), 6)
            c.text((402, 110), "red wire to +", 21, fade(SOFT_GREEN, k), True,
                   anchor="lm")
        ck = phase(t, 5.2, .4)
        c.text((520, 262), "charges over USB", 20, fade(FG, ck), True)
    c.text((162, 330), "check before plugging in", 20,
           fade(YELLOW, phase(t, .2, .4)), True)


SCENES = (
    ("privacy-wifi", 9.4, scene_wifi),
    ("privacy-ble", 6.0, scene_ble),
    ("privacy-nearby", 6.5, scene_nearby),
    ("privacy-games", 6.0, scene_games),
    ("privacy-poke", 6.0, scene_poke),
    ("privacy-names", 6.0, scene_names),
    ("privacy-scores", 5.0, scene_scores),
    ("privacy-page", 6.0, scene_page),
    ("start-board", 6.0, scene_board),
    ("start-plug", 7.0, scene_plug),
    ("start-install", 7.0, scene_install),
    ("start-speaker", 5.0, scene_speaker),
    ("start-case", 5.4, scene_case),
    ("start-battery", 8.0, scene_battery),
)


def render(seconds, draw):
    frames = []
    count = int(seconds * FPS) + 1
    for i in range(count):
        c = Canvas()
        draw(c, i / FPS)
        frames.append(c.done())
    return frames


def write(name, frames):
    # One palette for the whole animation, from a sample of its frames, so the
    # colours hold still while things move.
    sample = frames[::max(1, len(frames) // 8)] + [frames[-1]]
    strip = Image.new("RGB", (W, H * len(sample)))
    for i, frame in enumerate(sample):
        strip.paste(frame, (0, H * i))
    palette = strip.quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    gif = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
    durations = [round(1000 / FPS)] * (len(gif) - 1) + [HOLD_MS]
    gif_path = os.path.join(OUT, "%s.gif" % name)
    gif[0].save(gif_path, save_all=True, append_images=gif[1:], loop=0,
                duration=durations, disposal=1, optimize=False)
    png_path = os.path.join(OUT, "%s.png" % name)
    frames[-1].quantize(colors=128).save(png_path, optimize=True)
    return os.path.getsize(gif_path), os.path.getsize(png_path)


def main():
    wanted = set(sys.argv[1:])
    total = 0
    for name, seconds, draw in SCENES:
        if wanted and name not in wanted:
            continue
        DRAWN.clear()
        frames = render(seconds, draw)
        bad = sorted({t for t in DRAWN for f in FORBIDDEN if f in t})
        if bad:
            sys.exit("gen_site_anims.py: %s draws %s -- no animation may show %s"
                     % (name, ", ".join(repr(b) for b in bad), " or ".join(FORBIDDEN)))
        gif, png = write(name, frames)
        total += gif + png
        print("%-15s %4d KB gif  %3d KB png" % (name, gif // 1024, png // 1024))
    print("total %d KB" % (total // 1024))


if __name__ == "__main__":
    main()
