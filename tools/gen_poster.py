#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""Draw the A2 poster the site offers for download.

    python tools/gen_poster.py

Writes two posters, each as site/assets/braino-poster-A2-<edition>.pdf (A2
trim, 3 mm bleed, crop marks in a 10 mm slug, 300 dpi) and
site/assets/poster-preview-<edition>.jpg (the trim, at screen size, for the
Games tab's card): "devices", for a classroom or a hallway, and "makers", the
4 MB story for a makerspace or a maker faire.

Every picture comes from site/assets, so the poster shows what the page
shows. Two QR codes: tools/poster-qr-repo.png opens the repository and
tools/poster-qr-site.png the site. Fonts are
Carlito and Caladea (SIL OFL), which Windows and LibreOffice ship. Look at the
preview after regenerating: nothing else will say a line ran off the edge.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "site", "assets")
DPI = 300
MM = DPI / 25.4

TRIM_W, TRIM_H = 420, 594          # A2, mm
BLEED = 3
SLUG = 10                          # room outside the bleed for crop marks
PAGE_W = TRIM_W + 2 * (BLEED + SLUG)
PAGE_H = TRIM_H + 2 * (BLEED + SLUG)
OX = OY = BLEED + SLUG             # the trim's top-left corner on the page

INK = (0, 0, 0)            # the photographs' own black, so they meet the page without a seam
INK2 = (20, 22, 28)
FG = (255, 255, 255)
MUTED = (178, 180, 186)
YELLOW = (255, 214, 0)
PINK = (216, 51, 110)
TEAL = (14, 124, 123)
INDIGO = (47, 51, 181)
GREEN = (31, 138, 76)
AMBER = (242, 169, 0)
PURPLE = (91, 42, 134)

FONT_DIRS = (r"C:\Windows\Fonts", "/usr/share/fonts/truetype/crosextra",
             "/usr/share/fonts/truetype")
_fonts = {}


def font(name, pt):
    key = (name, pt)
    if key not in _fonts:
        for d in FONT_DIRS:
            path = os.path.join(d, name)
            if os.path.exists(path):
                _fonts[key] = ImageFont.truetype(path, round(pt * DPI / 72))
                break
        else:
            sys.exit("gen_poster.py: font %s not found" % name)
    return _fonts[key]


def bold(pt):
    return font("Carlito-Bold.ttf", pt)


def regular(pt):
    return font("Carlito-Regular.ttf", pt)


def italic(pt):
    return font("Caladea-Italic.ttf", pt)


def px(mm):
    return round(mm * MM)


def at(x, y):
    """Trim millimetres to page pixels."""
    return (px(OX + x), px(OY + y))


def wrap(d, text, f, width_mm):
    words, lines, line = text.split(), [], ""
    for w in words:
        trial = (line + " " + w).strip()
        if d.textlength(trial, font=f) <= px(width_mm):
            line = trial
        else:
            lines.append(line)
            line = w
    if line:
        lines.append(line)
    return lines


def paragraph(d, x, y, text, f, width_mm, fill, lead=1.28):
    """Draw wrapped text from (x, y) mm; return the y below it, in mm."""
    step = f.size * lead / MM
    for i, line in enumerate(wrap(d, text, f, width_mm)):
        d.text(at(x, y + i * step), line, font=f, fill=fill)
    return y + len(wrap(d, text, f, width_mm)) * step


def picture(page, name, box_mm, fit="cover", lighten=False):
    """Paste a picture into box_mm. `name` is a file in site/assets or an
    Image. With lighten, each pixel keeps the brighter of the picture and what
    is already on the page, so a picture on black sits on the background with
    no box around it."""
    x, y, w, h = box_mm
    im = name if isinstance(name, Image.Image) else \
        Image.open(os.path.join(ASSETS, name)).convert("RGB")
    W, H = px(w), px(h)
    if fit == "cover":
        s = max(W / im.width, H / im.height)
        im = im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS)
        l, t = (im.width - W) // 2, (im.height - H) // 2
        im = im.crop((l, t, l + W, t + H))
        page.paste(im, at(x, y))
    else:
        s = min(W / im.width, H / im.height)
        im = im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS)
        pos = (at(x, y)[0] + (W - im.width) // 2, at(x, y)[1] + (H - im.height) // 2)
        if lighten:
            from PIL import ImageChops
            under = page.crop((pos[0], pos[1], pos[0] + im.width, pos[1] + im.height))
            im = ImageChops.lighter(under, im)
        page.paste(im, pos)


def _part(which, h_mm):
    """One part of tools/braino-badge.svg -- "icon" (the brain) or "wordmark"
    -- rasterised h_mm tall in yellow, cropped to its own ink, as (rgb, alpha)."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import gen_logo_mask as lm
    _, paths = lm.load(lm.SVG)
    chosen = lm.select(paths, which)
    bx, by, bw, bh = lm.bounds(chosen)
    cov = lm.rasterise((bx, by, bw, bh), chosen, px(h_mm), 3)
    alpha = Image.fromarray((cov * 255).round().astype("uint8"))
    return Image.new("RGB", alpha.size, YELLOW), alpha


def lockup(page, d, x, y, tagline, h_mm=34):
    """The product's mark as a horizontal lockup: the brain, then the wordmark
    beside it with the tagline under the wordmark, all from the same SVG and
    the same path code the firmware's LogoMask is made from. Returns its
    bottom, in mm."""
    ink, alpha = _part("icon", h_mm)
    page.paste(ink, at(x, y), alpha)
    wx = x + alpha.width / MM + 7
    wh = h_mm * 0.52
    ink, alpha = _part("wordmark", wh)
    page.paste(ink, at(wx, y + 2), alpha)
    d.text(at(wx + 1, y + 2 + wh + 4), tagline, font=bold(19), fill=MUTED)
    return y + h_mm


def darken(page, box_mm, amount):
    """Darken a region by `amount` (0..1) towards the page's black."""
    x, y, w, h = box_mm
    X, Y = at(x, y)
    region = page.crop((X, Y, X + px(w), Y + px(h)))
    page.paste(Image.blend(region, Image.new("RGB", region.size, INK), amount), (X, Y))


def pool(page, box_mm, amount, feather=15):
    """Darken a region towards black, fading out over `feather` mm at its top
    and bottom edges."""
    x, y, w, h = box_mm
    X, Y = at(x, y)
    W, H = px(w), px(h)
    region = page.crop((X, Y, X + W, Y + H))
    dark = Image.blend(region, Image.new("RGB", region.size, INK), amount)
    mask = Image.new("L", (W, H), 255)
    md = ImageDraw.Draw(mask)
    F = px(feather)
    for j in range(F):
        v = round(255 * (j / F))
        md.line([(0, j), (W, j)], fill=v)
        md.line([(0, H - 1 - j), (W, H - 1 - j)], fill=v)
    page.paste(Image.composite(dark, region, mask), (X, Y))


def fade_edges(page, box_mm, left=0, bottom=0, under=None):
    """Blend a picture's left and bottom edges into the page's black."""
    x, y, w, h = box_mm
    X, Y = at(x, y)
    W, H = px(w), px(h)
    region = page.crop((X, Y, X + W, Y + H))
    mask = Image.new("L", (W, H), 255)
    md = ImageDraw.Draw(mask)
    L, B = px(left), px(bottom)
    for i in range(L):
        md.line([(i, 0), (i, H)], fill=round(255 * (i / L) ** 1.6))
    for j in range(B):
        v = round(255 * (j / B) ** 1.6)
        row = mask.crop((0, H - 1 - j, W, H - j)).point(lambda a, v=v: min(a, v))
        mask.paste(row, (0, H - 1 - j))
    base = under if under is not None else Image.new("RGB", (W, H), INK)
    page.paste(Image.composite(region, base, mask), (X, Y))


def crop_marks(d):
    t = BLEED + SLUG
    for cx in (t, t + TRIM_W):
        for cy in (t, t + TRIM_H):
            sx = -1 if cx == t else 1
            sy = -1 if cy == t else 1
            # horizontal and vertical ticks, outside the bleed
            d.line([(px(cx + sx * (BLEED + 1)), px(cy)), (px(cx + sx * (BLEED + SLUG - 1)), px(cy))],
                   fill=(0, 0, 0), width=2)
            d.line([(px(cx), px(cy + sy * (BLEED + 1))), (px(cx), px(cy + sy * (BLEED + SLUG - 1)))],
                   fill=(0, 0, 0), width=2)


BUILDS = (
    # What a player builds, in the Games tab's own groups (README's game
    # tables), for the parent or teacher reading the poster. Keep it short.
    (PINK, "NUMBERS", "Counting to times tables, fractions, money and the clock."),
    (TEAL, "WORDS", "Tracing letters, joined-up handwriting and new words."),
    (INDIGO, "THE WORLD", "Flags and capitals, the fifty states, the planets, the elements."),
    (GREEN, "THINKING", "Memory, logic puzzles and planning moves ahead."),
    (AMBER, "PLAYING FAIR", "Chess, Go, Backgammon and Ludo: taking turns, winning and losing well."),
)


def builds(d, y, M, W):
    """The 'what a player builds' strip; returns its bottom, in mm."""
    d.text(at(M, y), "WHAT A PLAYER BUILDS, ONE GAME AT A TIME", font=bold(24), fill=FG)
    y += 12
    n = len(BUILDS)
    cw = (W - (n - 1) * 6) / n
    bottom = y
    for i, (col, h, t) in enumerate(BUILDS):
        x = M + i * (cw + 6)
        d.rectangle([at(x, y), at(x + cw, y + 2.2)], fill=col)
        d.text(at(x, y + 5), h, font=bold(20), fill=col if col != INDIGO else (122, 128, 255))
        bottom = max(bottom, paragraph(d, x, y + 14, t, regular(17), cw, FG))
    return bottom


def footer(page, d, y, M, W):
    """Two codes, the repository and the site, each with what it opens."""
    def code(name, x):
        qr = Image.open(os.path.join(ROOT, "tools", name)).convert("RGB")
        qr = qr.resize((px(40), px(40)), Image.NEAREST)
        d.rectangle([at(x, y), at(x + 44, y + 44)], fill=(255, 255, 255))
        page.paste(qr, at(x + 2, y + 2))

    code("poster-qr-repo.png", M)
    d.text(at(M + 50, y + 4), "Star it", font=bold(40), fill=YELLOW)
    d.text(at(M + 50, y + 19), "on GitHub.", font=bold(40), fill=YELLOW)
    d.text(at(M + 50, y + 35), "github.com/iamankushpandit/Gume", font=regular(17), fill=MUTED)
    sx = M + W / 2 + 4
    code("poster-qr-site.png", sx)
    d.text(at(sx + 50, y + 4), "Get started", font=bold(40), fill=YELLOW)
    d.text(at(sx + 50, y + 19), "on the site.", font=bold(40), fill=YELLOW)
    d.text(at(sx + 50, y + 35), "iamankushpandit.github.io/Gume", font=regular(17), fill=MUTED)
    return y + 44


def tile_grid(d, y, M, W, tiles, cols, th, head_pt=21, body_pt=18, pad=7):
    """Coloured tiles, each with its heading and text inside equal padding
    and centred top to bottom; returns the grid's bottom, in mm."""
    tw = (W - (cols - 1) * 7) / cols
    inner = tw - 2 * pad
    # one heading size for the whole grid: the largest that fits every heading
    hp = head_pt
    while hp > 12 and max(d.textlength(h, font=bold(hp)) for _, h, _ in tiles) > px(inner):
        hp -= 0.5
    gap = 2.5
    for i, (col, h, t) in enumerate(tiles):
        x = M + (i % cols) * (tw + 7)
        ty = y + (i // cols) * (th + 7)
        d.rectangle([at(x, ty), at(x + tw, ty + th)], fill=col)
        ink = INK if col == AMBER else FG
        head_h = bold(hp).size / MM
        lines = wrap(d, t, regular(body_pt), inner)
        body_h = len(lines) * regular(body_pt).size * 1.28 / MM
        top = ty + (th - (head_h + gap + body_h)) / 2
        d.text(at(x + pad, top), h, font=bold(hp), fill=ink)
        paragraph(d, x + pad, top + head_h + gap, t, regular(body_pt), inner, ink)
    rows = (len(tiles) + cols - 1) // cols
    return y + rows * th + (rows - 1) * 7


# Where the old photograph's screens show a player's name; each is blurred
# before the photograph goes on a poster. Pixel boxes in hero-dark.jpg.
NAME_BOXES = ((672, 236, 745, 270), (470, 952, 545, 1004),
              (980, 626, 1034, 666), (925, 1195, 1015, 1248))


def background(page, blur=0):
    """The boards glowing in the dark, behind the whole poster: the names on
    their screens blurred, then darkened so every line of type reads."""
    from PIL import ImageFilter
    im = Image.open(os.path.join(ASSETS, "hero-dark.jpg")).convert("RGB")
    for b in NAME_BOXES:
        im.paste(im.crop(b).filter(ImageFilter.GaussianBlur(9)), b[:2])
    if blur:
        # softened as a whole, so the type over it reads before the screens do
        im = im.filter(ImageFilter.GaussianBlur(blur))
    bg = (-BLEED, -BLEED, TRIM_W + 2 * BLEED, TRIM_H + 2 * BLEED)
    picture(page, im, bg, fit="cover")
    darken(page, bg, 0.72)


def blank_page():
    page = Image.new("RGB", (px(PAGE_W), px(PAGE_H)), (255, 255, 255))
    d = ImageDraw.Draw(page)
    d.rectangle([px(SLUG), px(SLUG), px(PAGE_W - SLUG), px(PAGE_H - SLUG)], fill=INK)
    return page, d


def band(d, y, text, pt=28):
    d.rectangle([px(SLUG), at(0, y)[1], px(PAGE_W - SLUG), at(0, y + 17)[1]], fill=PINK)
    tw = d.textlength(text, font=italic(pt)) / MM
    d.text(at((TRIM_W - tw) / 2, y + 3.6), text, font=italic(pt), fill=FG)


def draw_makers():
    """The board edition, for a makerspace or a maker faire: 4 MB."""
    page, d = blank_page()
    M = 24
    W = TRIM_W - 2 * M
    y = 22
    pw = 150                      # the photograph of the boards, top right
    # The boards glowing in the dark, behind the whole poster, darkened so
    # every line of type reads over it.
    background(page, blur=10)
    # A 4-inch board in its printed case, on the launcher, top right; its left
    # and bottom edges fade into the background rather than ending in a box.
    box = (TRIM_W - pw, -BLEED, pw + BLEED, 205 + BLEED)
    under = page.crop((*at(box[0], box[1]), at(box[0], box[1])[0] + px(box[2]), at(box[0], box[1])[1] + px(box[3])))
    picture(page, "in-hand-flags.jpg", box, fit="cover")
    fade_edges(page, box, left=40, bottom=40, under=under)
    y = lockup(page, d, M, y, "OPEN-SOURCE LEARNING CONSOLE FIRMWARE") + 14
    d.text(at(M - 3, y - 14), "4 MB.", font=bold(330), fill=YELLOW)
    y += 105
    d.text(at(M, y), "That is smaller than one photo.", font=bold(44), fill=FG)
    d.text(at(M, y + 18), "It runs an entire learning console.", font=bold(44), fill=FG)
    y += 42
    y = paragraph(d, M, y, "Games that teach handwriting, maths, flags, the periodic table and "
                  "more, on an ESP32 touchscreen board, with no cloud and no accounts. "
                  "Everything fits in the 4 MB of flash on the board.",
                  regular(22), W * 0.62, FG)
    y = max(y, 205) + 8

    band(d, y, "In a world of gigabytes and cloud storage, Braino fits a whole learning "
               "console in four megabytes.", pt=24)
    y += 26

    cols = (
        ("IT IS FIRMWARE", "Braino runs on a CYD, a \u201cCheap Yellow Display\u201d: an ESP32 "
                           "already soldered to a touchscreen."),
        ("FLASH IT FROM CHROME", "Plug in USB-C, open the site in Chrome or Edge, pick your board, "
                                 "press Install. About a minute, nothing to solder."),
        ("OPEN SOURCE", "GPL-3.0. The firmware and the 3D-printable cases live in one "
                        "repository. Nothing phones home."),
        ("WHICH BOARD", "2.8 inch, resistive or capacitive. 3.2 inch, resistive. 4 inch, "
                        "resistive. One firmware: pick the board when you install."),
    )
    cw = (W - 3 * 7) / 4
    bottom = y
    for i, (h, t) in enumerate(cols):
        x = M + i * (cw + 7)
        d.text(at(x, y), h, font=bold(19), fill=YELLOW)
        bottom = max(bottom, paragraph(d, x, y + 8, t, regular(17), cw, FG))
    y = bottom + 9

    tiles = (
        (PINK, "PLAYERS", "First letters to the periodic table. Most games are courses that "
         "grow with each player."),
        (TEAL, "ADMINISTRATORS", "Choose which games each player sees. Up to five players and a "
         "Guest; settings behind a PIN."),
        (INDIGO, "PLAY TOGETHER", "Chess, Go, Sea Battle, Backgammon and Ludo across consoles, "
         "over a Bluetooth beacon. No pairing, no phone."),
        ((36, 38, 140), "ESP32 MAKERS", "No OS, no SD card, no cloud: 4 MB of flash doing all "
         "of it. Nine themes, and a Pong screen saver that lights the case."),
        (AMBER, "3D PRINTING", "The case is in the repository, with room for a 3000 mAh "
         "battery and a speaker. Pink is the house colour."),
        (GREEN, "GAME DEVELOPERS", "The launcher, players, scores and courses already exist: "
         "you write the game, Braino does the rest."),
        (PURPLE, "BOARD PORTERS", "A new board is one header and one platformio.ini section. "
         "docs/PORTING.md is the checklist."),
        ((194, 65, 12), "CASE DESIGNERS", "The 2.8, 3.2 and 4-inch boards all want good "
         "enclosures, with and without a speaker."),
        (INK2, "BUILT WITH AI. ON PURPOSE.", "The privacy rules are written into the repository "
         "and CI checks every change against them."),
    )
    y = tile_grid(d, y, M, W, tiles, 3, 41, head_pt=20, body_pt=16) + 8

    y = builds(d, y, M, W) + 10

    end = footer(page, d, y, M, W)
    if end > TRIM_H - 10:
        sys.exit("gen_poster.py: the makers poster runs to %.0f mm of %d" % (end, TRIM_H))
    crop_marks(d)
    return page


def draw():
    page = Image.new("RGB", (px(PAGE_W), px(PAGE_H)), (255, 255, 255))
    d = ImageDraw.Draw(page)
    # the ink runs into the bleed
    d.rectangle([px(SLUG), px(SLUG), px(PAGE_W - SLUG), px(PAGE_H - SLUG)], fill=INK)
    background(page, blur=10)

    M = 24                      # margin, mm
    W = TRIM_W - 2 * M
    y = 22

    # The product's own mark -- the brain over the wordmark -- from the same
    # SVG and the same path code the firmware's LogoMask is made from.
    y = lockup(page, d, M, y, "OPEN-SOURCE LEARNING CONSOLE") + 20

    # The headline, as large as the width allows.
    head = "Level up for real."
    size = 260
    while d.textlength(head, font=bold(size)) > px(W) and size > 60:
        size -= 4
    d.text(at(M - 2, y - 6), head, font=bold(size), fill=YELLOW)
    y += size * 25.4 / 72 * 0.9
    d.text(at(M, y), "The console you\u2019re glad they asked for.", font=bold(48), fill=FG)
    y += 22
    y = paragraph(d, M, y, "40+ games \u2014 handwriting, maths, flags, the periodic table, "
                  "chess across the room \u2014 with new ones on their way, and most of "
                  "them grow with each player. "
                  "On an ESP32 touchscreen board, the Attaky Cyber Deck Plus or the "
                  "Anbernic RG40XX V.", regular(25), W, MUTED) + 8

    # The three devices, with their names under them.
    # Full bleed, so the picture's own black meets the page edge rather than
    # sitting in a box a shade darker than the page.
    # The picture has black above and below the devices; cover crops into it.
    lh = 128
    # Fitted whole rather than cropped: its black is the page's black, so the
    # sides it leaves empty do not show.
    # A pool of near-black behind the devices, soft at its edges, so the
    # background photo does not show through the handheld's translucent body.
    pool(page, (-BLEED, y - 8, TRIM_W + 2 * BLEED, lh + 26), 0.94, feather=18)
    picture(page, "devices-lineup3.jpg", (0, y, TRIM_W, lh), fit="contain", lighten=True)
    shown = lh * 2.4                         # the picture is 2400 x 1000
    names = ("ESP32 boards", "Attaky Cyber Deck Plus", "Anbernic RG40XX V")
    for i, n in enumerate(names):
        # each device's centre across the picture, measured from its pixels
        cx = (TRIM_W - shown) / 2 + shown * (0.213, 0.53, 0.822)[i]
        tw = d.textlength(n, font=bold(24)) / MM
        d.text(at(cx - tw / 2, y + lh + 2), n, font=bold(24), fill=YELLOW)
    y += lh + 16

    # The band.
    d.rectangle([px(SLUG), at(0, y)[1], px(PAGE_W - SLUG), at(0, y + 17)[1]], fill=PINK)
    band = "One console. Every device. Learning that looks like play."
    tw = d.textlength(band, font=italic(28)) / MM
    d.text(at((TRIM_W - tw) / 2, y + 3.6), band, font=italic(28), fill=FG)
    y += 26

    # How each one gets Braino.
    cols = (
        ("ESP32 BOARDS", "2.8, 3.2 or 4 inch. Plug one into a computer and install "
                         "Braino from the site in about a minute, in Chrome or Edge."),
        ("ATTAKY CYBER DECK PLUS", "An ESP32-S3 handheld with a gamepad and a keyboard. "
                                   "Braino installs from Attaky\u2019s firmware site."),
        ("ANBERNIC RG40XX V", "Write the card image to a microSD card, put it in and "
                              "switch on. Braino is the whole handheld."),
    )
    cw = (W - 2 * 8) / 3
    bottom = y
    for i, (h, t) in enumerate(cols):
        x = M + i * (cw + 8)
        d.text(at(x, y), h, font=bold(21), fill=YELLOW)
        bottom = max(bottom, paragraph(d, x, y + 9, t, regular(19), cw, FG))
    y = bottom + 10
    y = builds(d, y, M, W) + 10

    # Four tiles: what a parent or teacher asks first.
    tiles = (
        (PINK, "GROWS WITH EACH PLAYER", "Most games are courses: a level per player "
         "that rises and falls."),
        (TEAL, "AN ADMINISTRATOR CURATES", "Choose which games each player sees; "
         "settings sit behind a PIN."),
        (INDIGO, "PLAY ACROSS THE ROOM", "Chess, Go, Backgammon and Ludo between "
         "consoles. No phone, no internet."),
        (GREEN, "NOTHING COLLECTED", "No accounts, and nothing about the player "
         "leaves the console."),
    )
    y = tile_grid(d, y, M, W, tiles, 4, 46, head_pt=18, body_pt=16) + 10

    end = footer(page, d, y, M, W)

    if end > TRIM_H - 12:
        sys.exit("gen_poster.py: the layout runs to %.0f mm of %d" % (end, TRIM_H))
    crop_marks(d)
    return page


def save(page, stem):
    pdf = os.path.join(ASSETS, "braino-poster-A2-%s.pdf" % stem)
    page.save(pdf, "PDF", resolution=DPI, quality=90,
              title="Braino! poster, A2", author="iamankushpandit")
    trim = page.crop((px(OX), px(OY), px(OX + TRIM_W), px(OY + TRIM_H)))
    w = 1000
    trim.resize((w, round(w * TRIM_H / TRIM_W)), Image.LANCZOS).save(
        os.path.join(ASSETS, "poster-preview-%s.jpg" % stem), quality=86)
    print("wrote %s (%d KB) and poster-preview-%s.jpg" % (pdf, os.path.getsize(pdf) // 1024, stem))


def social_preview():
    """site/assets/social-preview.jpg, the picture a shared link shows: 1200 x
    630, the headline over the three devices, on the posters' background."""
    from PIL import ImageFilter, ImageChops
    W_, H_ = 1200, 630
    bg = Image.open(os.path.join(ASSETS, "hero-dark.jpg")).convert("RGB")
    for b in NAME_BOXES:
        bg.paste(bg.crop(b).filter(ImageFilter.GaussianBlur(9)), b[:2])
    bg = bg.filter(ImageFilter.GaussianBlur(10))
    s_ = max(W_ / bg.width, H_ / bg.height)
    bg = bg.resize((round(bg.width * s_), round(bg.height * s_)), Image.LANCZOS)
    l, t = (bg.width - W_) // 2, (bg.height - H_) // 2
    im = Image.blend(bg.crop((l, t, l + W_, t + H_)), Image.new("RGB", (W_, H_), INK), 0.75)
    d = ImageDraw.Draw(im)

    def f(name, size):
        for dd in FONT_DIRS:
            path = os.path.join(dd, name)
            if os.path.exists(path):
                return ImageFont.truetype(path, size)
        sys.exit("gen_poster.py: font %s not found" % name)

    d.text((60, 48), "Level up for real.", font=f("Carlito-Bold.ttf", 92), fill=YELLOW)
    d.text((62, 156), "The console you’re glad they asked for.", font=f("Carlito-Bold.ttf", 40), fill=FG)
    lineup = Image.open(os.path.join(ASSETS, "devices-lineup3.jpg")).convert("RGB")
    lw = 1080
    lineup = lineup.resize((lw, round(lineup.height * lw / lineup.width)), Image.LANCZOS)
    x, y = (W_ - lw) // 2, H_ - lineup.height - 18
    im.paste(ImageChops.lighter(im.crop((x, y, x + lw, y + lineup.height)), lineup), (x, y))
    im.save(os.path.join(ASSETS, "social-preview.jpg"), quality=88)
    print("wrote social-preview.jpg")


def main():
    save(draw(), "devices")
    save(draw_makers(), "makers")
    social_preview()


if __name__ == "__main__":
    main()
