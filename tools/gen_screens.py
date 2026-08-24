"""
Render preview images of the UI for the README.

    python tools/gen_screens.py

These are MOCK-UPS, not device photos: they redraw each view in Python using
the exact rectangles and fonts from the C++ source, so they show real layout
and real artwork but are generated on the host. The display cannot be
read back over SPI (MISO is not wired for the panel on this board), so true
screenshots are not possible without a camera.

Flag and outline artwork is decoded from the generated map-n-flag arrays, so
those really are the pixels the device draws.

Output: docs/screens/*.png
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "screens"

# map-n-flag pixel data, if the library checkout is available
MNF_TOOLS = Path(r"C:\Users\Ankus\CppEsp32Lib\tools")
HAVE_ART = MNF_TOOLS.exists()
if HAVE_ART:
    sys.path.insert(0, str(MNF_TOOLS))
    from verify import decode_i4, parse_generated  # noqa: E402
    IMAGES = parse_generated()
else:
    IMAGES = {}

# Product identity, mirroring include/AppVersion.h. The mock-ups are the
# fifth place the name used to be typed out; keep it in one name here too.
PRODUCT = "Braino!"
COPYRIGHT_SHORT = "(C) GoodTime Micro"

W, H = 320, 240
BG, SURFACE, PANEL = (18, 20, 26), (32, 36, 46), (52, 58, 72)
TEXT, MUTED, OUTLINE = (232, 236, 242), (140, 148, 162), (86, 94, 110)
SUCCESS, ERROR, WARN = (52, 254, 128), (247, 61, 82), (255, 230, 110)
BLUE, GREEN, RED, SHADOW = (36, 132, 204), (45, 154, 96), (222, 83, 83), (10, 11, 15)
GOLD, INK, WHITE = (255, 200, 0), (24, 60, 136), (255, 255, 255)


def font(size, bold=False):
    for name in (("consolab.ttf", "consola.ttf") if bold else ("consola.ttf",)):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


F1, F2, F4 = font(9), font(13), font(19, True)


def shade(c, pct):
    return tuple(min(255, v * pct // 100) for v in c)


def button(d, r, label, fill=PANEL, tc=TEXT, f=F2):
    x, y, w, h = r
    d.rounded_rectangle([x + 2, y + 3, x + w + 1, y + h + 2], 6, fill=SHADOW)
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], 6, fill=fill)
    if w > 10 and h > 8:
        d.line([(x + 4, y + 1), (x + w - 5, y + 1)], fill=shade(fill, 138))
        d.line([(x + 4, y + h - 2), (x + w - 5, y + h - 2)], fill=shade(fill, 68))
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], 6, outline=OUTLINE)
    s = label
    while s and d.textlength(s, font=f) > w - 12:
        s = s[:-1]
    d.text((x + (w - d.textlength(s, font=f)) / 2, y + h / 2 - f.size / 2 - 1), s, font=f, fill=tc)


def sync_badge(d, cx, cy, synced=True):
    d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=SUCCESS if synced else WARN)
    d.text((cx - 3, cy - 6), "v" if synced else "!", font=F1, fill=(0, 0, 0))


def wifi_badge(d, cx, cy, bars=3):
    base = cy + 6
    for i in range(4):
        h = 3 + i * 3
        x = cx - 7 + i * 4
        d.rectangle([x, base - h, x + 2, base], fill=SUCCESS if i < bars else (70, 74, 84))


def topbar(d, title, synced=True, bars=3):
    d.rectangle([0, 0, W - 1, 29], fill=SURFACE)
    d.line([(0, 0), (W, 0)], fill=shade(SURFACE, 145))
    d.line([(0, 29), (W, 29)], fill=shade(SURFACE, 60))
    d.rounded_rectangle([6, 5, 38, 24], 3, outline=MUTED)
    d.text((11, 9), "home", font=F1, fill=MUTED)
    d.text((48, 8), title, font=F2, fill=TEXT)
    t = "12:41 AM"
    d.text((W - 98 - d.textlength(t, font=F2), 8), t, font=F2, fill=TEXT)
    sync_badge(d, W - 88, 15, synced)
    wifi_badge(d, W - 64, 15, bars)
    d.ellipse([W - 34, 4, W - 12, 26], outline=TEXT)


def battery_badge(d, cx, cy, pct=72):
    d.rectangle([cx - 7, cy - 4, cx + 5, cy + 4], outline=MUTED)
    d.rectangle([cx + 6, cy - 2, cx + 7, cy + 2], fill=MUTED)
    w = int(10 * pct / 100)
    d.rectangle([cx - 6, cy - 3, cx - 6 + w, cy + 3], fill=SUCCESS)


def ble_badge(d, cx, cy):
    """Mirrors Ui::drawBleBadge -- one polyline through six points, 10x16."""
    x0, y0 = cx - 5, cy - 8
    px = [0, 10, 5, 5, 10, 0]
    py = [4, 11, 16, 0, 5, 12]
    for off in (0, 1):
        d.line([(x0 + px[i] + off, y0 + py[i]) for i in range(6)], fill=BLUE)


def art(sym):
    if sym not in IMAGES:
        return None
    r = IMAGES[sym]
    return Image.fromarray(decode_i4(r["w"], r["h"], r["data"]), "RGBA")


def tinted(sym, ink):
    if sym not in IMAGES:
        return None
    r = IMAGES[sym]
    a = decode_i4(r["w"], r["h"], r["data"])[:, :, 3]
    out = Image.new("RGBA", (r["w"], r["h"]), ink + (0,))
    out.putalpha(Image.fromarray(a))
    return out


def blank(w=W, h=H):
    im = Image.new("RGB", (w, h), BG)
    return im, ImageDraw.Draw(im)


# ---------------------------------------------------------------- screens
def flags_country():
    im, d = blank(); topbar(d, "Guess the Flag")
    d.text((8, 32), "3/5", font=F2, fill=TEXT)
    d.text((W - 8 - d.textlength("+2", font=F2), 32), "+2", font=F2, fill=GOLD)
    button(d, (132, 31, 56, 16), "Easy", f=F1)
    d.rectangle([78, 46, 242, 170], fill=WHITE, outline=OUTLINE)
    f = art("mnf_flag_br")
    if f: im.paste(f.resize((160, 120), Image.NEAREST), (80, 48), f.resize((160, 120), Image.NEAREST))
    q = "Which country?"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 169), q, font=F2, fill=TEXT)
    for i, (lab, ok) in enumerate([("Brazil", True), ("Argentina", 0), ("Portugal", 0), ("Colombia", 0)]):
        button(d, (6 + (i % 2) * 158, 186 + (i // 2) * 27, 150, 25), lab,
               SUCCESS if ok else PANEL, (0, 0, 0) if ok else TEXT)
    return im


def flags_capital():
    im, d = blank(); topbar(d, "Guess the Flag")
    d.text((8, 32), "4/5", font=F2, fill=TEXT)
    d.text((W - 8 - d.textlength("+3", font=F2), 32), "+3", font=F2, fill=GOLD)
    button(d, (132, 31, 56, 16), "Easy", f=F1)
    d.rectangle([78, 46, 242, 170], fill=WHITE, outline=OUTLINE)
    f = art("mnf_flag_br")
    if f: im.paste(f.resize((160, 120), Image.NEAREST), (80, 48), f.resize((160, 120), Image.NEAREST))
    q = "Bonus! Capital of Brazil?"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 169), q, font=F2, fill=GOLD)
    for i, lab in enumerate(["Brasilia", "Buenos Aires", "Lisbon", "Bogota"]):
        button(d, (6 + (i % 2) * 158, 186 + (i // 2) * 27, 150, 25), lab)
    return im


# The Countries game was removed, and with it the djaiss/mapsicon country
# outlines -- there is no mnf_map_* artwork in the library any more, so the
# two screens that used it are gone rather than rendering blank.


def states():
    im, d = blank(); topbar(d, "US States")
    d.text((8, 32), "3/6", font=F2, fill=TEXT)
    button(d, (132, 31, 56, 16), "Easy", f=F1)
    q = "What is the capital of Texas?"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 96), q, font=F2, fill=TEXT)
    for i, lab in enumerate(["Austin", "Houston", "Dallas", "El Paso"]):
        button(d, (6 + (i % 2) * 158, 150 + (i // 2) * 40, 150, 36), lab,
               SUCCESS if i == 0 else PANEL, (0, 0, 0) if i == 0 else TEXT)
    return im


def stateflags():
    im, d = blank(); topbar(d, "State Flags")
    d.text((8, 32), "2/6", font=F2, fill=TEXT)
    button(d, (132, 31, 56, 16), "Easy", f=F1)
    d.rectangle([78, 46, 241, 169], fill=WHITE, outline=OUTLINE)
    f = art("mnf_state_flag_ca")
    if f:
        f2 = f.resize((f.width * 2, f.height * 2), Image.NEAREST)
        im.paste(f2, (80 + (160 - f2.width) // 2, 48 + (120 - f2.height) // 2), f2)
    for i, lab in enumerate(["California", "Nevada", "Oregon", "Arizona"]):
        button(d, (6 + (i % 2) * 158, 150 + (i // 2) * 40, 150, 36), lab,
               SUCCESS if i == 0 else PANEL, (0, 0, 0) if i == 0 else TEXT)
    return im


def statemaps():
    im, d = blank(); topbar(d, "State Maps")
    d.text((8, 32), "4/6", font=F2, fill=TEXT)
    button(d, (132, 31, 56, 16), "Medium", f=F1)
    d.rectangle([110, 42, 209, 141], fill=WHITE, outline=OUTLINE)
    m = tinted("mnf_state_map_fl", INK)
    if m: im.paste(m, (112 + (96 - m.width) // 2, 44 + (96 - m.height) // 2), m)
    for i, lab in enumerate(["Florida", "Georgia", "Alabama", "Louisiana"]):
        button(d, (6 + (i % 2) * 158, 150 + (i // 2) * 40, 150, 36), lab,
               SUCCESS if i == 0 else PANEL, (0, 0, 0) if i == 0 else TEXT)
    return im


def trace():
    im, d = blank(); topbar(d, "Trace")
    for label, x, w, col in [["ABC", 38, 50, WARN], ["abc", 92, 50, PANEL], ["123", 146, 50, PANEL],
                             ["Again", 204, 52, PANEL], ["Next", 260, 52, PANEL]]:
        d.rounded_rectangle([x, 32, x + w, 54], 4, fill=col, outline=OUTLINE)
        d.text((x + w / 2 - d.textlength(label, font=F1) / 2, 37),
               label, font=F1, fill=PANEL if col == WARN else TEXT)
    d.rectangle([78, 56, 241, 192], fill=SURFACE, outline=OUTLINE)
    # Show uppercase 'A' with waypoints - first stroke partially traced, second stroke next
    # First stroke: apex to left, already completed
    apex, left, bar = (160, 64), (96, 190), (160, 137)
    for t in range(0, 11):
        x = apex[0] + (left[0] - apex[0]) * t / 10
        y = apex[1] + (left[1] - apex[1]) * t / 10
        d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=SUCCESS)
    d.line([apex, left], fill=SUCCESS, width=3)
    # Add numbered badge on first dot of first stroke
    d.ellipse([apex[0] - 7, apex[1] - 7, apex[0] + 7, apex[1] + 7], fill=WARN)
    d.text((apex[0] - 3, apex[1] - 4), "1", font=F1, fill=PANEL)
    # Second stroke: right side, with dots showing waypoints, pulsing on first unclaimed
    right = (224, 190)
    for t in range(0, 11):
        x = apex[0] + (right[0] - apex[0]) * t / 10
        y = apex[1] + (right[1] - apex[1]) * t / 10
        if t == 0:
            # Next dot, pulsing
            d.ellipse([x - 6, y - 6, x + 6, y + 6], fill=WARN)
        else:
            # Unclaimed dots
            d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=MUTED)
    # Bar stroke dots
    bar_l, bar_r = (124, 137), (196, 137)
    for t in range(0, 11):
        x = bar_l[0] + (bar_r[0] - bar_l[0]) * t / 10
        d.ellipse([x - 2, 141, x + 2, 145], fill=MUTED)
    # Watermark letter
    d.text((110, 116), "A", font=F4, fill=PANEL)
    # Progress bar
    bar_x, bar_y = 90, 222
    d.rounded_rectangle([bar_x, bar_y, 230, bar_y + 10], 4, fill=PANEL, outline=OUTLINE)
    d.rounded_rectangle([bar_x, bar_y, 130, bar_y + 10], 4, fill=SUCCESS)
    # Prev/Next buttons
    button(d, (8, 202, 60, 30), "Prev", f=F1)
    button(d, (252, 202, 60, 30), "Next", f=F1)
    return im


def trace_lower():
    im, d = blank(); topbar(d, "Trace")
    for label, x, w, col in [["ABC", 38, 50, PANEL], ["abc", 92, 50, WARN], ["123", 146, 50, PANEL],
                             ["Again", 204, 52, PANEL], ["Next", 260, 52, PANEL]]:
        d.rounded_rectangle([x, 32, x + w, 54], 4, fill=col, outline=OUTLINE)
        d.text((x + w / 2 - d.textlength(label, font=F1) / 2, 37),
               label, font=F1, fill=PANEL if col == WARN else TEXT)
    d.rectangle([78, 56, 241, 192], fill=SURFACE, outline=OUTLINE)
    # Show lowercase 'g' with descender - two strokes
    # First stroke: main bowl, completed
    pts_main = [(190, 108), (169, 94), (137, 98), (118, 121), (120, 147),
                (144, 163), (177, 158), (193, 135), (190, 108)]
    for pt in pts_main:
        d.ellipse([pt[0] - 3, pt[1] - 3, pt[0] + 3, pt[1] + 3], fill=SUCCESS)
    # Draw connecting lines for first stroke
    for i in range(len(pts_main) - 1):
        d.line([pts_main[i], pts_main[i+1]], fill=SUCCESS, width=3)
    # Badge on first point
    d.ellipse([190 - 7, 108 - 7, 190 + 7, 108 + 7], fill=WARN)
    d.text((187, 105), "1", font=F1, fill=PANEL)
    # Second stroke: descender, with dots
    d.ellipse([194 - 7, 109 - 7, 194 + 7, 109 + 7], fill=WARN)
    d.text((191, 106), "2", font=F1, fill=PANEL)
    descender_pts = [(194, 109), (194, 166), (181, 186), (149, 188), (124, 175)]
    for i, pt in enumerate(descender_pts):
        if i == 0:
            d.ellipse([pt[0] - 6, pt[1] - 6, pt[0] + 6, pt[1] + 6], fill=WARN)
        else:
            d.ellipse([pt[0] - 3, pt[1] - 3, pt[0] + 3, pt[1] + 3], fill=MUTED)
    # Watermark
    d.text((118, 112), "g", font=F4, fill=PANEL)
    # Progress bar (90% full)
    bar_x, bar_y = 90, 222
    d.rounded_rectangle([bar_x, bar_y, 230, bar_y + 10], 4, fill=PANEL, outline=OUTLINE)
    d.rounded_rectangle([bar_x, bar_y, 215, bar_y + 10], 4, fill=SUCCESS)
    # Buttons
    button(d, (8, 202, 60, 30), "Prev", f=F1)
    button(d, (252, 202, 60, 30), "Next", f=F1)
    return im


SKIN, SKIN_D, NAIL = (255, 182, 176), (156, 154, 152), (255, 220, 208)
FLEN = [34, 46, 52, 46, 36]


def hand(d, x0, raised, mirror):
    d.rounded_rectangle([x0 - 4, 146, x0 + 4 * 26 + 22 + 4, 182], 8, fill=SKIN, outline=OUTLINE)
    for i in range(5):
        up = raised[i]
        top = 152 - (FLEN[(4 - i) if mirror else i] if up else 12)
        x = x0 + i * 26
        d.rounded_rectangle([x, top, x + 22, 156], 9, fill=SKIN if up else SKIN_D, outline=OUTLINE)
        if up:
            d.rounded_rectangle([x + 5, top + 5, x + 17, top + 13], 3, fill=NAIL)
    d.text((x0 + 2 * 26 + 11 - 12, 163), "Left" if not mirror else "Right", font=F2, fill=(150, 90, 88))


def fingers_count():
    im, d = blank(); topbar(d, "Finger Counting")
    d.text((W - 8 - d.textlength("4/6", font=F2), 32), "4/6", font=F2, fill=TEXT)
    q = "How many fingers?"
    d.text((W / 2 - d.textlength(q, font=F4) / 2, 41), q, font=F4, fill=TEXT)
    h = "Count them, then tap the number"
    d.text((W / 2 - d.textlength(h, font=F2) / 2, 71), h, font=F2, fill=MUTED)
    hand(d, 22, [True, True, False, True, False], False)
    hand(d, 172, [False, True, True, False, False], True)
    for i, v in enumerate([4, 6, 5, 7]):
        button(d, (12 + i * 76, 196, 68, 34), str(v),
               SUCCESS if v == 5 else PANEL, (0, 0, 0) if v == 5 else TEXT, F4)
    return im


def fingers_show():
    im, d = blank(); topbar(d, "Finger Counting")
    d.text((W - 8 - d.textlength("5/6", font=F2), 32), "5/6", font=F2, fill=TEXT)
    q = "Show me 7 fingers"
    d.text((W / 2 - d.textlength(q, font=F4) / 2, 41), q, font=F4, fill=TEXT)
    d.text((W / 2 - d.textlength("Up: 4", font=F2) / 2, 71), "Up: 4", font=F2, fill=MUTED)
    hand(d, 22, [True, True, True, False, False], False)
    hand(d, 172, [False, False, True, False, False], True)
    t = "Tap a finger to raise or lower it"
    d.text((W / 2 - d.textlength(t, font=F2) / 2, 205), t, font=F2, fill=MUTED)
    return im


def cinnamon():
    im = Image.new("RGB", (W, H), (245, 245, 248)); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W - 1, 29], fill=SURFACE)
    d.text((48, 8), "Cinnamon Says", font=F2, fill=TEXT)
    d.rounded_rectangle([6, 5, 38, 24], 3, outline=MUTED)
    d.text((11, 9), "home", font=F1, fill=MUTED)
    d.text((10, 34), "Score 4", font=F2, fill=(30, 30, 36))
    d.text((W - 10 - d.textlength("Best 9", font=F2), 34), "Best 9", font=F2, fill=(30, 30, 36))
    st = "Watch"
    d.text((W / 2 - d.textlength(st, font=F2) / 2, 52), st, font=F2, fill=(30, 30, 36))
    lit = [False, True, False, False]
    cols_lit = [(248, 0, 0), (0, 130, 255), (0, 230, 60), (255, 240, 0)]
    cols_dim = [(96, 0, 0), (0, 0, 70), (0, 70, 0), (128, 110, 0)]
    for i in range(4):
        x, y = 30 + (i % 2) * 150, 74 + (i // 2) * 66
        d.rounded_rectangle([x + 2, y + 3, x + 114, y + 57], 8, fill=SURFACE)
        d.rounded_rectangle([x, y, x + 112, y + 54], 8,
                            fill=cols_lit[i] if lit[i] else cols_dim[i])
        if lit[i]:
            d.rounded_rectangle([x - 3, y - 3, x + 115, y + 57], 11, outline=(0, 0, 0), width=2)
    return im


def launcher_wide():
    im, d = blank(); d.rectangle([0, 0, W - 1, 47], fill=SURFACE)
    d.line([(0, 0), (W, 0)], fill=shade(SURFACE, 145))
    d.line([(0, 47), (W, 47)], fill=shade(SURFACE, 60))
    d.text((10, 5), PRODUCT, font=F4, fill=TEXT)
    d.text((10, 34), COPYRIGHT_SHORT, font=F1, fill=MUTED)
    # Profile name: plain text on the byline row, no button chrome.
    d.text((124, 34), "Ava", font=F1, fill=TEXT)
    t = "12:41 AM"
    d.text((W - 40 - d.textlength(t, font=F1), 9), t, font=F1, fill=TEXT)
    # Beacon badge sits left of the clock, off its measured width.
    ble_badge(d, int(W - 40 - d.textlength(t, font=F1)) - 14, 14)
    sync_badge(d, W - 92, 34); wifi_badge(d, W - 68, 34); battery_badge(d, W - 44, 34)
    d.line([(W - 110, 8), (W - 110, 40)], fill=OUTLINE)
    d.ellipse([W - 30, 11, W - 5, 36], outline=TEXT)
    tiles = [("Number Line", "jump to number"), ("US States", "states & capitals"),
             ("Flags", "guess the flag"), ("Shape Arith", "add & subtract"),
             ("Trace", "A-Z & 0-9"), ("Calendar", "days & months")]
    cols = [BLUE, GREEN, RED]
    for slot, (title, sub) in enumerate(tiles):
        x, y = 10 + (slot % 2) * 155, 52 + (slot // 2) * 53
        fill = cols[slot % 3]
        d.rounded_rectangle([x + 2, y + 3, x + 146, y + 48], 6, fill=SHADOW)
        d.rounded_rectangle([x, y, x + 144, y + 45], 6, fill=fill)
        d.line([(x + 4, y + 1), (x + 140, y + 1)], fill=shade(fill, 138))
        d.ellipse([x + 10, y + 8, x + 36, y + 34], fill=(120, 200, 255), outline=WHITE)
        d.text((x + 46, y + 9), title, font=F2, fill=WHITE)
        d.text((x + 46, y + 28), sub, font=F1, fill=(235, 245, 255))
    button(d, (8, 212, 74, 24), "Prev"); button(d, (W - 82, 212, 74, 24), "Next")
    d.text((W / 2 - 12, 217), "3/5", font=F2, fill=TEXT)
    return im


def launcher_tall():
    im = Image.new("RGB", (240, 320), BG); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 239, 77], fill=SURFACE)
    # Title left, copyright right, sharing the top row. See AppRuntimeLauncher.
    d.text((10, 6), PRODUCT, font=F4, fill=TEXT)
    d.text((232 - d.textlength(COPYRIGHT_SHORT, font=F1), 13), COPYRIGHT_SHORT,
           font=F1, fill=MUTED)
    d.line([(8, 30), (232, 30)], fill=shade(SURFACE, 150))
    d.text((8, 36), "Ava", font=F2, fill=TEXT)
    d.text((8, 53), "12:41 AM", font=F2, fill=TEXT)
    bx = int(8 + d.textlength("12:41 AM", font=F2) + 10)
    sync_badge(d, bx, 60); wifi_badge(d, bx + 22, 60); battery_badge(d, bx + 46, 60)
    ble_badge(d, bx + 70, 60)
    d.ellipse([208, 48, 232, 72], outline=TEXT)
    tiles = [("Number Line", "jump to number", BLUE), ("US States", "states & capitals", GREEN),
             ("Flags", "guess the flag", RED), ("Shape Arith", "add & subtract", BLUE)]
    for slot, (title, sub, fill) in enumerate(tiles):
        x, y = 8 + (slot % 2) * 116, 86 + (slot // 2) * 104
        d.rounded_rectangle([x + 2, y + 3, x + 109, y + 98], 6, fill=SHADOW)
        d.rounded_rectangle([x, y, x + 107, y + 95], 6, fill=fill)
        d.line([(x + 4, y + 1), (x + 103, y + 1)], fill=shade(fill, 138))
        d.ellipse([x + 39, y + 15, x + 69, y + 45], fill=(120, 200, 255), outline=WHITE)
        for s, f, yy in ((title, F2, 58), (sub, F1, 78)):
            s2 = s
            while d.textlength(s2, font=f) > 100 and len(s2) > 2:
                s2 = s2[:-1]
            d.text((x + 54 - d.textlength(s2, font=f) / 2, y + yy), s2, font=f,
                   fill=WHITE if f is F2 else (235, 245, 255))
    button(d, (8, 292, 74, 24), "Prev"); button(d, (158, 292, 74, 24), "Next")
    d.text((110, 297), "6/7", font=F2, fill=TEXT)
    return im


def tabs(d, active_device):
    """Mirrors Ui::drawTab: active tab is page-coloured with rounded top only."""
    for i, (lab, x) in enumerate([("Device / Wi-Fi", 4), ("Games", 160)]):
        active = (i == 0) == active_device
        top = 32 if active else 36
        h = 28 if active else 24
        fill = SURFACE if active else PANEL
        d.rounded_rectangle([x, top, x + 151, top + h], 6, fill=fill, outline=OUTLINE)
        d.rectangle([x, top + h - 7, x + 151, top + h], fill=fill)
        if active:
            d.line([(x + 6, top + 1), (x + 145, top + 1)], fill=shade(fill, 150))
        d.text((x + 76 - d.textlength(lab, font=F2) / 2, top + h / 2 - 7), lab,
               font=F2, fill=TEXT if active else MUTED)
    ax = 4 if active_device else 160
    d.line([(4, 60), (ax, 60)], fill=OUTLINE)
    d.line([(ax + 152, 60), (316, 60)], fill=OUTLINE)


def settings_tabs(d, active_index):
    """Three tabs, each SCREEN_WIDTH/3 wide -- mirrors deviceTabRect() and
    friends, which divide the live width rather than assuming 160px halves."""
    third = 320 // 3
    for i, lab in enumerate(("Device", "Power", "Admin")):
        x = i * third
        w = (320 - x) if i == 2 else third
        active = (i == active_index)
        top = 30 if active else 34
        h = 22 if active else 18
        fillc = SURFACE if active else PANEL
        d.rounded_rectangle([x, top, x + w - 1, top + h], 4, fill=fillc, outline=OUTLINE)
        d.text((x + w / 2 - d.textlength(lab, font=F2) / 2, top + h / 2 - 6), lab,
               font=F2, fill=TEXT if active else MUTED)
    d.line([(0, 52), (319, 52)], fill=OUTLINE)


def settings_device():
    im, d = blank(); topbar(d, "Settings")
    settings_tabs(d, 0)
    # Four rows of 30px from y=58, under the tab baseline. Network takes a
    # whole row so Nearby can have a half-width slot beside Reset.
    button(d, (8, 58, 144, 30), "Theme: Dark")
    button(d, (164, 58, 144, 30), "Menu: Tall")
    button(d, (8, 92, 144, 30), "Light: On")
    button(d, (164, 92, 144, 30), "Beacon: On")
    button(d, (8, 126, 304, 30), "Network", BLUE, WHITE)
    button(d, (8, 160, 144, 30), "Nearby: On")
    button(d, (164, 160, 144, 30), "Reset device", (120, 58, 58), WHITE)
    d.text((8, 194), "Brightness", font=F1, fill=MUTED)
    d.text((312 - d.textlength("80%", font=F1), 194), "80%", font=F1, fill=MUTED)
    # slider: track, filled portion, handle -- mirrors Ui::drawSlider
    r = (8, 204, 304, 32)
    cy = r[1] + r[3] // 2
    pad, span = 11, r[2] - 22
    fill = int((80 - 25) / 75 * span)
    d.rounded_rectangle([r[0] + pad, cy - 4, r[0] + pad + span, cy + 4], 4, fill=PANEL, outline=OUTLINE)
    d.rounded_rectangle([r[0] + pad, cy - 4, r[0] + pad + fill, cy + 4], 4, fill=BLUE)
    hx = r[0] + pad + fill
    d.ellipse([hx - 10, cy - 10, hx + 10, cy + 10], fill=SURFACE, outline=OUTLINE)
    d.ellipse([hx - 6, cy - 6, hx + 6, cy + 6], fill=BLUE)
    return im


def settings_power():
    """Settings tab: what happens when nobody is touching the device."""
    im, d = blank(); topbar(d, "Settings")
    settings_tabs(d, 1)
    button(d, (8, 58, 304, 30), "When idle: Saver then sleep")
    button(d, (8, 92, 304, 30), "Idle after: 1m")
    button(d, (8, 126, 304, 30), "Sleep after: 1m")
    d.text((8, 168), "Saver at 60s, screen off at 120s.", font=F1, fill=MUTED)
    d.text((8, 184), "Sleep blanks the screen. A touch wakes it.", font=F1, fill=MUTED)
    return im


def settings_admin():
    """Settings tab: the PIN that guards this screen and the admin profile."""
    im, d = blank(); topbar(d, "Settings")
    settings_tabs(d, 2)
    button(d, (8, 58, 304, 30), "Change admin PIN")
    d.text((8, 100), "The PIN guards the admin profile. It", font=F1, fill=MUTED)
    d.text((8, 116), "ships as 0000 -- change it.", font=F1, fill=MUTED)
    d.text((8, 140), "Admin profile: Admin", font=F1, fill=MUTED)
    lab = "Entered twice; only saved if both match."
    d.text((160 - d.textlength(lab, font=F1) / 2, 220), lab, font=F1, fill=MUTED)
    return im


def _pin_pad(d, heading, filled):
    """Mirrors the shared 3x4 PIN pad: rows 0-2 are 1-9, row 3 is DEL / 0 / OK.

    Geometry follows pinKeyRect(): derived from the panel size so every row
    lands above the bottom edge."""
    lab_w = d.textlength(heading, font=F2)
    d.text((160 - lab_w / 2, 38), heading, font=F2, fill=TEXT)

    # Four dots, filled from the left as digits arrive. Masked, never digits.
    pitch = 26
    x0 = 160 - (pitch * 3) // 2
    for i in range(4):
        cx = x0 + i * pitch
        fill = SUCCESS if i < filled else PANEL
        d.ellipse([cx - 7, 74 - 7, cx + 7, 74 + 7], fill=fill, outline=OUTLINE)

    top, bottom = 92, 240 - 6
    pitch_y = (bottom - top) // 4
    key_h = pitch_y - 5
    key_w = min(70, (320 - 40) // 3 - 8)
    pitch_x = key_w + 8
    left = (320 - (pitch_x * 2 + key_w)) // 2

    for row in range(4):
        for col in range(3):
            x = left + col * pitch_x
            y = top + row * pitch_y
            if row < 3:
                lab, fill = str(row * 3 + col + 1), PANEL
            elif col == 0:
                lab, fill = "DEL", (150, 60, 60)
            elif col == 1:
                lab, fill = "0", PANEL
            else:
                lab, fill = "OK", (45, 154, 96)
            d.rounded_rectangle([x, y, x + key_w, y + key_h], 4,
                                fill=fill, outline=OUTLINE)
            d.text((x + key_w / 2 - d.textlength(lab, font=F2) / 2,
                    y + key_h / 2 - 7), lab, font=F2, fill=TEXT)


def settings_pin():
    """Setting a new admin PIN: entered twice, saved only if both match."""
    im, d = blank(); topbar(d, "Settings")
    button(d, (6, 6, 52, 22), "Back", f=F1)
    _pin_pad(d, "Enter new PIN", 2)
    return im


def profiles_pin():
    """The PIN asked for on the way into the admin profile."""
    im, d = blank()
    button(d, (6, 6, 52, 22), "Back", f=F1)
    _pin_pad(d, "Enter Admin PIN", 3)
    return im


def settings_games():
    im, d = blank(); topbar(d, "Settings")
    tabs(d, False)
    for i, (lab, on) in enumerate([("Slide", 1), ("Odd One", 1), ("Shape Arith", 1),
                                   ("Fingers", 0), ("Calendar", 1)]):
        y = 62 + i * 29
        d.rounded_rectangle([8, y, 311, y + 26], 4, fill=SURFACE, outline=OUTLINE)
        d.rounded_rectangle([12, y + 6, 27, y + 21], 3, fill=SUCCESS if on else PANEL, outline=OUTLINE)
        if on: d.text((17, y + 9), "v", font=F1, fill=(0, 0, 0))
        d.text((36, y + 7), lab, font=F2, fill=TEXT)
    button(d, (8, 210, 92, 25), "Prev"); button(d, (212, 210, 92, 25), "Next")
    d.text((W / 2 - 10, 215), "4/5", font=F2, fill=TEXT)
    return im


def network_time():
    im, d = blank(); topbar(d, "Wi-Fi")
    d.text((14, 34), "WI-FI", font=F1, fill=MUTED)
    d.line([(52, 41), (306, 41)], fill=OUTLINE)
    d.text((14, 44), "DextersLab", font=F2, fill=TEXT)
    wifi_badge(d, 296, 54, 3)
    button(d, (14, 64, 140, 30), "Scan Wi-Fi", BLUE, WHITE)
    button(d, (166, 64, 140, 30), "Forget")
    d.text((14, 102), "TIME", font=F1, fill=MUTED)
    d.line([(48, 109), (306, 109)], fill=OUTLINE)
    stamp = "Tue Aug 11 2026  12:41 AM"
    d.text((14, 114), stamp, font=F2, fill=TEXT)
    sync_badge(d, int(14 + d.textlength(stamp, font=F2) + 12), 122, True)
    button(d, (14, 132, 140, 30), "Auto time: On", GREEN, WHITE)
    button(d, (166, 132, 140, 30), "US Central")
    button(d, (14, 172, 140, 30), "Sync now", GREEN, WHITE)
    button(d, (166, 172, 140, 30), "Back")
    d.text((W / 2 - d.textlength("Tap the zone to change it", font=F1) / 2, 210),
           "Tap the zone to change it", font=F1, fill=MUTED)
    return im


def timezone_picker():
    im, d = blank(); topbar(d, "Wi-Fi")
    d.text((8, 34), "Choose your time zone", font=F2, fill=TEXT)
    zones = ["UTC", "US Eastern", "US Central", "US Mountain", "US Arizona"]
    for i, z in enumerate(zones):
        y = 46 + i * 28
        sel = (z == "US Central")
        d.rounded_rectangle([8, y, 311, y + 25], 4, fill=BLUE if sel else SURFACE, outline=OUTLINE)
        d.text((18, y + 6), z, font=F2, fill=WHITE if sel else TEXT)
    button(d, (8, 208, 90, 26), "Prev")
    button(d, (106, 208, 108, 26), "Cancel")
    button(d, (222, 208, 90, 26), "Next")
    return im


def screensaver():
    im = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(im)
    for y in range(0, H, 14):
        d.rectangle([W // 2 - 1, y, W // 2, y + 8], fill=(40, 40, 40))
    rally = (255, 160, 60)
    d.rounded_rectangle([7, 70, 13, 110], 3, fill=rally)
    d.rounded_rectangle([307, 130, 313, 170], 3, fill=rally)
    d.rounded_rectangle([150, 108, 162, 120], 2, fill=rally)
    d.rounded_rectangle([153, 111, 159, 117], 1, fill=WHITE)
    d.text((W / 2 - 6, 4), "7", font=F2, fill=(70, 70, 76))
    return im


SCREENS = [
    ("launcher-wide", launcher_wide, "Home screen, Wide layout"),
    ("launcher-tall", launcher_tall, "Home screen, Tall layout"),
    ("flags-country", flags_country, "Flags: name the country"),
    ("flags-capital", flags_capital, "Flags: capital-city bonus"),
    ("states", states, "US States: name the capital"),
    ("stateflags", stateflags, "State Flags: name the state"),
    ("statemaps", statemaps, "State Maps: name the outline"),
    ("trace", trace, "Trace: uppercase and digits"),
    ("trace-lower", trace_lower, "Trace: lowercase letters"),
    ("fingers-count", fingers_count, "Finger Counting: count them"),
    ("fingers-show", fingers_show, "Finger Counting: show me N"),
    ("cinnamon", cinnamon, "Cinnamon Says"),
    ("settings-device", settings_device, "Settings: device"),
    ("settings-power", settings_power, "Settings: power and sleep"),
    ("settings-admin", settings_admin, "Settings: the admin PIN"),
    ("settings-pin", settings_pin, "Settings: setting a new admin PIN"),
    ("profiles-pin", profiles_pin, "Profiles: the admin PIN prompt"),
    ("settings-games", settings_games, "Settings: which games appear"),
    ("network-time", network_time, "Network & Time"),
    ("timezone", timezone_picker, "Time zone picker"),
    ("screensaver", screensaver, "Pong screen saver"),
]


# ------------------------------------------------------- remaining 23 games
# Geometry below is taken from each game's own Rect helpers in src/games/.

def tictactoe():
    im, d = blank(); topbar(d, "Tic-Tac-Toe")
    d.text((10, 36), "X turn", font=F2, fill=TEXT)
    bx, by, bw = 76, 68, 168
    cell = bw // 3
    d.rectangle([bx, by, bx + bw, by + bw], outline=OUTLINE)
    for i in range(1, 3):
        d.line([(bx + i * cell, by), (bx + i * cell, by + bw)], fill=OUTLINE)
        d.line([(bx, by + i * cell), (bx + bw, by + i * cell)], fill=OUTLINE)
    marks = {0: "X", 4: "O", 8: "X", 2: "O"}
    for k, mk in marks.items():
        cx = bx + (k % 3) * cell + cell // 2
        cy = by + (k // 3) * cell + cell // 2
        col = (110, 200, 255) if mk == "X" else (255, 170, 90)
        if mk == "X":
            d.line([(cx - 16, cy - 16), (cx + 16, cy + 16)], fill=col, width=4)
            d.line([(cx + 16, cy - 16), (cx - 16, cy + 16)], fill=col, width=4)
        else:
            d.ellipse([cx - 17, cy - 17, cx + 17, cy + 17], outline=col, width=4)
    return im


def memory():
    im, d = blank(); topbar(d, "Memory Match")
    d.text((10, 36), "Pairs 3/12", font=F2, fill=TEXT)
    cols, rows, cell, gap = 6, 4, 44, 4
    gw = cols * cell + (cols - 1) * gap
    gh = rows * cell + (rows - 1) * gap
    sx, sy = (W - gw) // 2, 58 + (H - 58 - gh) // 2
    faces = {2: "A", 3: "A", 9: "F"}
    for i in range(cols * rows):
        x = sx + (i % cols) * (cell + gap)
        y = sy + (i // cols) * (cell + gap)
        if i in faces:
            d.rounded_rectangle([x, y, x + cell, y + cell], 4, fill=(255, 246, 178), outline=OUTLINE)
            d.text((x + cell / 2 - 4, y + cell / 2 - 7), faces[i], font=F2, fill=(40, 40, 46))
        else:
            d.rounded_rectangle([x, y, x + cell, y + cell], 4, fill=BLUE, outline=OUTLINE)
            d.ellipse([x + 16, y + 16, x + cell - 16, y + cell - 16], outline=(150, 200, 240))
    return im


def _quiz4(title, eq, prompt, answers, correct, ay, ah, eq_y=96):
    im, d = blank(); topbar(d, title)
    d.text((10, 36), "Score 4/6", font=F2, fill=TEXT)
    d.text((W / 2 - d.textlength(eq, font=F4) / 2, eq_y), eq, font=F4, fill=TEXT)
    d.text((W / 2 - d.textlength(prompt, font=F2) / 2, 129), prompt, font=F2, fill=MUTED)
    for i, a in enumerate(answers):
        r = (18 + (i % 2) * 152, ay + (i // 2) * (ah + 8), 132, ah)
        ok = (a == correct)
        button(d, r, a, SUCCESS if ok else PANEL, (0, 0, 0) if ok else TEXT, F4)
    return im


def math_game():
    return _quiz4("Math", "7 + 5 = ?", "Tap the answer", ["12", "11", "13", "10"], "12", 144, 38)


def multiply():
    return _quiz4("Multiplication", "6 x 7 = ?", "Tap the product", ["42", "36", "48", "40"], "42", 144, 38)


def time_game():
    im, d = blank(); topbar(d, "Time")
    d.text((10, 36), "Score 2/4", font=F2, fill=TEXT)
    cx, cy, r = W // 2, 100, 42
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=WHITE, outline=OUTLINE)
    for h in range(12):
        import math
        a = math.radians(h * 30 - 90)
        d.ellipse([cx + math.cos(a) * (r - 6) - 1, cy + math.sin(a) * (r - 6) - 1,
                   cx + math.cos(a) * (r - 6) + 1, cy + math.sin(a) * (r - 6) + 1], fill=(60, 60, 70))
    d.line([(cx, cy), (cx + 18, cy - 12)], fill=(30, 30, 40), width=3)     # hour
    d.line([(cx, cy), (cx - 6, cy + 28)], fill=(200, 60, 60), width=2)     # minute
    d.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=(30, 30, 40))
    for i, a in enumerate(["2:30", "3:30", "2:15", "6:10"]):
        r2 = (18 + (i % 2) * 152, 152 + (i // 2) * 40, 132, 34)
        button(d, r2, a, SUCCESS if a == "2:30" else PANEL, (0, 0, 0) if a == "2:30" else TEXT)
    return im


def whack():
    im, d = blank(); topbar(d, "Whack A Mole")
    d.text((10, 36), "Score 12", font=F2, fill=TEXT)
    d.text((W - 10 - d.textlength("0:18", font=F2), 36), "0:18", font=F2, fill=TEXT)
    GRID, CELL = 9, 20
    gx, gy = (W - GRID * CELL) // 2, 58
    d.rectangle([gx, gy, gx + GRID * CELL, gy + GRID * CELL], outline=OUTLINE)
    for i in range(1, GRID):
        d.line([(gx + i * CELL, gy), (gx + i * CELL, gy + GRID * CELL)], fill=(48, 52, 62))
        d.line([(gx, gy + i * CELL), (gx + GRID * CELL, gy + i * CELL)], fill=(48, 52, 62))
    for (r, c) in [(2, 3), (5, 6), (7, 1)]:
        x, y = gx + c * CELL, gy + r * CELL
        d.ellipse([x + 3, y + 3, x + CELL - 3, y + CELL - 3], fill=(255, 246, 178), outline=(120, 110, 60))
        d.ellipse([x + 7, y + 8, x + 9, y + 10], fill=(60, 50, 20))
        d.ellipse([x + 12, y + 8, x + 14, y + 10], fill=(60, 50, 20))
    return im


def microku():
    im, d = blank(); topbar(d, "Microku")
    size, cell = 4, 32
    gs = size * cell
    sx, sy = (W - gs) // 2, 58
    grid = [[1, 0, 3, 0], [0, 4, 0, 2], [2, 0, 4, 0], [0, 3, 0, 1]]
    for r in range(size):
        for c in range(size):
            x, y = sx + c * cell, sy + r * cell
            d.rectangle([x, y, x + cell, y + cell], fill=SURFACE, outline=OUTLINE)
            if grid[r][c]:
                d.text((x + cell / 2 - 4, y + cell / 2 - 7), str(grid[r][c]), font=F2, fill=TEXT)
    for i in range(0, size + 1, 2):
        d.line([(sx + i * cell, sy), (sx + i * cell, sy + gs)], fill=TEXT, width=2)
        d.line([(sx, sy + i * cell), (sx + gs, sy + i * cell)], fill=TEXT, width=2)
    w, gap = 46, 12
    for v in range(1, 5):
        button(d, (8 + (v - 1) * (w + gap), 205, w, 30), str(v))
    return im


def shapes():
    im, d = blank(); topbar(d, "Shape & Color")
    d.text((W / 2 - d.textlength("Match them up", font=F2) / 2, 38), "Match them up", font=F2, fill=MUTED)
    items = [("red circle", (230, 70, 70)), ("blue square", (70, 130, 230)),
             ("green tri", (70, 200, 110)), ("yellow star", (240, 210, 70))]
    for i, (lab, col) in enumerate(items):
        r = (16, 58 + i * 43, 136, 38)
        d.rounded_rectangle([r[0], r[1], r[0] + r[2], r[1] + r[3]], 5, fill=SURFACE, outline=OUTLINE)
        d.ellipse([r[0] + 8, r[1] + 9, r[0] + 28, r[1] + 29], fill=col)
        d.text((r[0] + 36, r[1] + 12), lab, font=F1, fill=TEXT)
        t = (168, 58 + i * 43, 136, 38)
        d.rounded_rectangle([t[0], t[1], t[0] + t[2], t[1] + t[3]], 5, fill=PANEL, outline=OUTLINE)
        d.text((t[0] + 12, t[1] + 12), ["blue square", "red circle", "yellow star", "green tri"][i],
               font=F1, fill=TEXT)
    return im


def counting():
    im, d = blank(); topbar(d, "Counting")
    q = "How many objects?"
    d.text((W / 2 - d.textlength(q, font=F4) / 2, 32), q, font=F4, fill=TEXT)
    st = "Score 3/5   Streak 2   Best 5"
    d.text((W / 2 - d.textlength(st, font=F1) / 2, 60), st, font=F1, fill=MUTED)
    d.rounded_rectangle([18, 76, 302, 174], 8, fill=PANEL, outline=OUTLINE)
    import random
    random.seed(4)
    for i in range(7):
        cx = 18 + 24 + (i % 7) * 39
        cy = 76 + 24 + (i // 7) * 30
        d.ellipse([cx - 10, cy - 10, cx + 10, cy + 10], fill=(255, 170, 90), outline=(120, 120, 128))
    for i, v in enumerate(["6", "7", "8", "5"]):
        button(d, (15 + i * 76, 188, 62, 40), v,
               SUCCESS if v == "7" else PANEL, (0, 0, 0) if v == "7" else TEXT, F4)
    return im


def money():
    im, d = blank(); topbar(d, "Money")
    q = "How much is this?"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 38), q, font=F2, fill=TEXT)
    coins = [("25", (170, 175, 180)), ("10", (170, 175, 180)),
             ("5", (170, 175, 180)), ("1", (190, 130, 90)), ("1", (190, 130, 90))]
    for i, (v, col) in enumerate(coins):
        x = 14 + i * 59
        d.ellipse([x + 9, 146, x + 43, 180], fill=col, outline=(90, 90, 96))
        d.text((x + 20, 156), v, font=F2, fill=(30, 30, 36))
    for i, a in enumerate(["42c", "37c", "45c", "40c"]):
        r = (18 + (i % 2) * 152, 160 + (i // 2) * 36, 132, 30)
        button(d, r, a,
               SUCCESS if a == "42c" else PANEL, (0, 0, 0) if a == "42c" else TEXT)
    return im


def fractions():
    im, d = blank(); topbar(d, "Fractions")
    q = "Pick the matching fraction"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 38), q, font=F2, fill=MUTED)
    cx, cy, r = W // 2, 84, 30
    d.pieslice([cx - r, cy - r, cx + r, cy + r], -90, 90, fill=(255, 202, 84), outline=OUTLINE)
    d.pieslice([cx - r, cy - r, cx + r, cy + r], 90, 270, fill=WHITE, outline=OUTLINE)
    for i, a in enumerate(["1/2", "1/3", "2/3", "1/4"]):
        rr = (18 + (i % 2) * 152, 164 + (i // 2) * 34, 132, 30)
        button(d, rr, a, SUCCESS if a == "1/2" else PANEL, (0, 0, 0) if a == "1/2" else TEXT)
    return im


def maze():
    im, d = blank(); topbar(d, "Maze")
    d.text((10, 36), "Level 4", font=F2, fill=TEXT)
    COLS, ROWS, CELL = 12, 8, 22
    mx, my = (W - COLS * CELL) // 2, 58
    d.rectangle([mx, my, mx + COLS * CELL, my + ROWS * CELL], fill=SURFACE, outline=OUTLINE)
    walls = [(0, 2, 'v'), (1, 4, 'h'), (3, 1, 'v'), (4, 5, 'h'), (6, 3, 'v'),
             (2, 6, 'h'), (5, 8, 'v'), (7, 2, 'h'), (3, 9, 'v'), (6, 7, 'h')]
    for r, c, o in walls:
        x, y = mx + c * CELL, my + r * CELL
        if o == 'v':
            d.line([(x, y), (x, y + CELL * 2)], fill=(150, 158, 172), width=2)
        else:
            d.line([(x, y), (x + CELL * 2, y)], fill=(150, 158, 172), width=2)
    d.ellipse([mx + 6, my + 6, mx + 16, my + 16], fill=(255, 246, 178))
    d.rectangle([mx + COLS * CELL - 18, my + ROWS * CELL - 18,
                 mx + COLS * CELL - 6, my + ROWS * CELL - 6], fill=SUCCESS)
    return im


def sorting():
    im, d = blank(); topbar(d, "Sorting")
    q = "Tap smallest to largest"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 38), q, font=F2, fill=MUTED)
    vals = [[7, 2, 9], [4, 11, 6]]
    for r in range(2):
        for c in range(3):
            rr = (24 + c * 92, 82 + r * 58, 76, 44)
            done = (r == 0 and c == 1)
            button(d, rr, str(vals[r][c]), SUCCESS if done else PANEL,
                   (0, 0, 0) if done else TEXT, F4)
    d.text((W / 2 - d.textlength("Next: 4", font=F2) / 2, 208), "Next: 4", font=F2, fill=MUTED)
    return im


def colormix():
    im, d = blank(); topbar(d, "Color Mix")
    q = "What do you get?"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 40), q, font=F2, fill=MUTED)
    d.ellipse([84, 66, 138, 120], fill=(230, 60, 60))
    d.ellipse([182, 66, 236, 120], fill=(240, 210, 60))
    d.text((153, 84), "+", font=F4, fill=TEXT)
    for i, lab in enumerate(["orange", "purple", "green", "brown"]):
        r = (26 + (i % 2) * 148, 142 + (i // 2) * 44, 120, 34)
        button(d, r, lab, SUCCESS if lab == "orange" else PANEL,
               (0, 0, 0) if lab == "orange" else TEXT)
    return im


def slide():
    im, d = blank(); topbar(d, "Slide Puzzle")
    d.text((10, 36), "Moves 14", font=F2, fill=TEXT)
    size, cell = 3, 48
    grid = size * cell
    sx, sy = (W - grid) // 2, 68
    layout = [1, 2, 3, 4, 5, 6, 7, 0, 8]
    for i, v in enumerate(layout):
        x, y = sx + (i % size) * cell, sy + (i // size) * cell
        if v == 0:
            d.rectangle([x, y, x + cell, y + cell], fill=BG, outline=OUTLINE)
            continue
        d.rounded_rectangle([x + 2, y + 2, x + cell - 2, y + cell - 2], 5, fill=BLUE, outline=OUTLINE)
        d.text((x + cell / 2 - 6, y + cell / 2 - 9), str(v), font=F4, fill=WHITE)
    d.text((W / 2 - d.textlength("Slide tiles into order", font=F2) / 2, 218),
           "Slide tiles into order", font=F2, fill=MUTED)
    return im


def oddone():
    im, d = blank(); topbar(d, "Odd One Out")
    q = "Tap the one that is different"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 40), q, font=F2, fill=MUTED)
    for i in range(6):
        r = (36 + (i % 3) * 86, 70 + (i // 3) * 50, 66, 40)
        d.rounded_rectangle([r[0], r[1], r[0] + r[2], r[1] + r[3]], 5, fill=SURFACE, outline=OUTLINE)
        cx, cy = r[0] + r[2] // 2, r[1] + r[3] // 2
        if i == 4:
            d.rectangle([cx - 12, cy - 12, cx + 12, cy + 12], fill=(255, 246, 178))
        else:
            d.ellipse([cx - 13, cy - 13, cx + 13, cy + 13], fill=(110, 190, 255))
    d.text((W / 2 - d.textlength("Score 5/7", font=F2) / 2, 186), "Score 5/7", font=F2, fill=TEXT)
    return im


def shapearith():
    im, d = blank(); topbar(d, "Shape Arith")
    d.text((8, 34), "5 take away 2 circles", font=F2, fill=TEXT)
    d.text((W - 8 - d.textlength("3/6", font=F2), 34), "3/6", font=F2, fill=TEXT)
    lp = (8, 50, 132, 110)
    d.rounded_rectangle([lp[0], lp[1], lp[0] + lp[2], lp[1] + lp[3]], 6, fill=PANEL, outline=OUTLINE)
    d.text((lp[0] + lp[2] / 2 - d.textlength("left", font=F1) / 2, lp[1] + 7),
           "left", font=F1, fill=MUTED)
    for i in range(3):
        cx = lp[0] + 18 + (i % 4) * 28
        cy = lp[1] + 34 + (i // 4) * 32
        d.ellipse([cx - 13, cy - 13, cx + 13, cy + 13], fill=(110, 190, 255), outline=OUTLINE)
    d.line([(148, 106), (172, 106)], fill=(249, 158, 158), width=2)
    d.polygon([(172, 106), (164, 101), (164, 111)], fill=(249, 158, 158))
    rp = (180, 50, 132, 110)
    d.rounded_rectangle([rp[0], rp[1], rp[0] + rp[2], rp[1] + rp[3]], 6, fill=PANEL, outline=OUTLINE)
    d.text((rp[0] + rp[2] / 2 - d.textlength("take away", font=F1) / 2, rp[1] + 7),
           "take away", font=F1, fill=MUTED)
    for i in range(2):
        cx = rp[0] + 18 + i * 28
        cy = rp[1] + 34
        d.ellipse([cx - 13, cy - 13, cx + 13, cy + 13], fill=(249, 158, 158), outline=OUTLINE)
    d.text((W / 2 - d.textlength("How many are left?", font=F1) / 2, 162),
           "How many are left?", font=F1, fill=MUTED)
    for i, v in enumerate(["2", "3", "4", "1"]):
        button(d, (15 + i * 76, 172, 62, 42), v,
               SUCCESS if v == "3" else PANEL, (0, 0, 0) if v == "3" else TEXT, F4)
    return im


def calendar():
    im, d = blank(); topbar(d, "Calendar")
    for m, lab in enumerate(["Days", "Months"]):
        r = (8 + m * 124, 32, 120, 26)
        button(d, r, lab, BLUE if m == 0 else PANEL, WHITE if m == 0 else TEXT)
    q = "What comes after Wednesday?"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 96), q, font=F2, fill=TEXT)
    for i, day in enumerate(["Thursday", "Tuesday", "Friday", "Monday"]):
        r = (8 + (i % 2) * 159, 152 + (i // 2) * 48, 152, 40)
        button(d, r, day, SUCCESS if day == "Thursday" else PANEL,
               (0, 0, 0) if day == "Thursday" else TEXT)
    return im


def numberline():
    im, d = blank(); topbar(d, "Number Line")
    d.text((8, 34), "3 + 4 = ?", font=F4, fill=TEXT)
    d.text((W - 8 - d.textlength("2/5", font=F2), 38), "2/5", font=F2, fill=TEXT)
    y = 130
    x0, x1 = 24, 296
    d.line([(x0, y), (x1, y)], fill=TEXT, width=2)
    for n in range(11):
        x = x0 + n * (x1 - x0) // 10
        d.line([(x, y - 6), (x, y + 6)], fill=TEXT)
        d.text((x - 3, y + 10), str(n), font=F1, fill=MUTED)
    hx = x0 + 3 * (x1 - x0) // 10
    d.ellipse([hx - 8, y - 8, hx + 8, y + 8], fill=(255, 246, 178), outline=(160, 140, 60))
    for k in range(3):
        ax = x0 + (3 + k) * (x1 - x0) // 10
        bx = x0 + (4 + k) * (x1 - x0) // 10
        d.arc([ax, y - 26, bx, y], 180, 360, fill=SUCCESS, width=2)
    for i, v in enumerate(["7", "6", "8", "5"]):
        button(d, (14 + i * 74, 194, 64, 38), v,
               SUCCESS if v == "7" else PANEL, (0, 0, 0) if v == "7" else TEXT, F4)
    return im


def _si_tabs(d, active):
    """Mirrors SystemInfoGame: strip at TOP_BAR_HEIGHT+2, 28px tall, W/5 each."""
    labels = ["Board", "Memory", "Network", "BLE", "App"]
    y, tw = 32, W // 5
    d.rectangle([0, y, W - 1, y + 27], fill=PANEL)
    for i, lab in enumerate(labels):
        x = i * tw
        w = (W - x) if i == 4 else tw
        on = i == active
        top = y if on else y + 4
        h = 28 if on else 24
        fill = SURFACE if on else PANEL
        d.rounded_rectangle([x, top, x + w - 1, top + h], 6, fill=fill, outline=OUTLINE)
        d.rectangle([x, top + h - 7, x + w - 1, top + h], fill=fill)
        d.text((x + w / 2 - d.textlength(lab, font=F1) / 2, top + h / 2 - 6), lab,
               font=F1, fill=TEXT if on else MUTED)
    d.line([(0, y + 28), (W, y + 28)], fill=OUTLINE)


def _si_rows(d, rows, top=66):
    """RowList geometry: labels at x+6, values at max(92, w/2), 16px rows."""
    y = top
    for kind, label, value, colour in rows:
        if kind == "s":
            d.text((6, y), label, font=F2, fill=MUTED)
            d.line([(60, y + 8), (300, y + 8)], fill=OUTLINE)
            y += 18
        else:
            d.text((6, y), label, font=F1, fill=MUTED)
            d.text((160, y), value, font=F1, fill=colour or TEXT)
            y += 16
    return y


def systeminfo_ble():
    im, d = blank(); topbar(d, "System Info")
    _si_tabs(d, 3)
    _si_rows(d, [
        ("s", "Beacon", "", None),
        ("r", "Status", "Advertising", SUCCESS),
        ("r", "Mode", "BLE Advertise Only", None),
        ("s", "On Air", "", None),
        ("r", "Name", "Braino-A4F2", None),
        ("r", "Family ID", "Braino", None),
        ("r", "Device ID", "A4F2", None),
        ("s", "Mfr Data", "", None),
        ("r", "Company", "0xffff unassigned", None),
        ("r", "Family", "Braino (BR)", None),
        ("r", "Nearby play", "Sharing", WARN),
        ("r", "Game index", "12 Maze", None),
        ("r", "Best score", "7", None),
        ("s", "Privacy", "", None),
        ("r", "Child name", "Not Broadcast", SUCCESS),
        ("r", "Wi-Fi SSID", "Not Broadcast", SUCCESS),
        ("r", "Open game", "Broadcast", WARN),
        ("r", "Best score", "Broadcast", WARN),
    ])
    d.rounded_rectangle([306, 63, 312, 236], 3, fill=PANEL, outline=OUTLINE)
    d.rounded_rectangle([307, 66, 311, 130], 2, fill=(88, 164, 224))
    return im


def systeminfo_memory():
    im, d = blank(); topbar(d, "System Info")
    _si_tabs(d, 1)
    y = _si_rows(d, [
        ("s", "Heap", "", None),
        ("r", "Used", "63.4 KB / 320 KB", None),
    ])
    d.rounded_rectangle([6, y, 300, y + 8], 3, fill=PANEL, outline=OUTLINE)
    d.rounded_rectangle([7, y + 1, 66, y + 7], 2, fill=SUCCESS)
    y += 12
    _si_rows(d, [
        ("r", "Free", "256.6 KB", None),
        ("r", "Min free", "124.7 KB", None),
        ("r", "Largest", "110.2 KB", None),
        ("r", "Fragmented", "13%", SUCCESS),
    ], top=y)
    y += 64
    d.rounded_rectangle([6, y, 300, y + 8], 3, fill=PANEL, outline=OUTLINE)
    d.rounded_rectangle([7, y + 1, 45, y + 7], 2, fill=SUCCESS)
    _si_rows(d, [
        ("s", "CPU", "", None),
        ("r", "Loop load", "18% (3/17 ms)", None),
        ("r", "Worst frame", "31 ms", None),
    ], top=y + 14)
    return im


def nearby():
    """Nearby: the anonymous peer list.

    Geometry from NearbyGame: a full-width toggle at y=TOP_BAR_HEIGHT+6 with
    30px height, then the RowList below it."""
    im, d = blank(); topbar(d, "Nearby")
    button(d, (8, 36, 304, 30), "Sharing: On", SUCCESS, (12, 20, 14))
    _si_rows(d, [
        ("s", "You", "", None),
        ("r", "Your tag", "A4F2", None),
        ("r", "Listening", "Yes", SUCCESS),
        ("s", "7C1B", "", None),
        ("r", "Distance", "Near", MUTED),
        ("r", "Playing", "Maze", None),
        ("r", "Their best", "9 lvl", WARN),
        ("r", "Your best", "7 lvl", None),
        ("r", "", "They are ahead of you", WARN),
        ("s", "B930", "", None),
        ("r", "Distance", "Far", MUTED),
        ("r", "Playing", "Multiplication", None),
        ("r", "Their best", "12 pts", None),
    ], top=76)
    return im


def about_radios():
    im, d = blank(); topbar(d, "About")
    d.rounded_rectangle([10, 38, 309, 195], 6, fill=SURFACE, outline=OUTLINE)
    d.text((14, 44), "What the radios do", font=F2, fill=TEXT)
    d.text((14, 70), "Wi-Fi: the clock only (NTP), plus", font=F1, fill=MUTED)
    d.text((14, 84), "a one-off time zone lookup.", font=F1, fill=MUTED)
    d.text((14, 100), "Now: connected", font=F1, fill=TEXT)
    d.text((14, 120), "Bluetooth beacon", font=F2, fill=TEXT)
    d.text((14, 146), "Now: broadcasting Braino-A4F2", font=F1, fill=WARN)
    d.text((14, 162), "Nearby on: also the game open and", font=F1, fill=WARN)
    d.text((14, 176), "its best score. No name, no profile.", font=F1, fill=WARN)
    button(d, (12, 206, 92, 28), "Prev")
    button(d, (216, 206, 92, 28), "Next")
    d.text((W / 2 - 14, 212), "6/8", font=F2, fill=MUTED)
    return im


def percent():
    im, d = blank(); topbar(d, "Percent")
    q = "What percent?"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 38), q, font=F2, fill=MUTED)

    # Draw circle at (96, 130) with radius 62
    cx, cy, r = 96, 130, 62

    # Draw the circle background and outline
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(81, 113, 147), outline=BLUE)

    # Draw shaded portion (60%)
    import math
    for angle in range(-90, -90 + 216, 3):  # 60% of 360 = 216 degrees
        rad = math.radians(angle)
        x = cx + int(r * math.cos(rad))
        y = cy + int(r * math.sin(rad))
        next_angle = angle + 3
        next_rad = math.radians(next_angle)
        next_x = cx + int(r * math.cos(next_rad))
        next_y = cy + int(r * math.sin(next_rad))
        d.polygon([
            (cx, cy),
            (x, y),
            (next_x, next_y)
        ], fill=(255, 202, 84))

    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=BLUE, width=2)

    # Draw spokes every 10%
    for i in range(10):
        angle = -90 + i * 36
        rad = math.radians(angle)
        x = cx + int(r * math.cos(rad))
        y = cy + int(r * math.sin(rad))
        d.line([(cx, cy), (x, y)], fill=(100, 150, 200), width=1)

    # Four option buttons
    for i, pct in enumerate(["25%", "50%", "60%", "75%"]):
        rr = (164 + (i % 2) * 76, 96 + (i // 2) * 44, 70, 38)
        button(d, rr, pct, SUCCESS if pct == "60%" else PANEL,
               (0, 0, 0) if pct == "60%" else TEXT)

    # Score line
    d.text((8, 226), "Score: 15  Streak: 2  Lvl: 1", font=F1, fill=TEXT)

    return im


def grewords():
    """GRE Words, Quiz tab: the word, four glosses, one right."""
    im, d = blank(); topbar(d, "GRE Words")
    for i, (lab, x) in enumerate([("Study", 0), ("Quiz", 160)]):
        active = (i == 1)
        top = 30 if active else 34
        h = 22 if active else 18
        fillc = SURFACE if active else PANEL
        d.rounded_rectangle([x, top, x + 159, top + h], 4, fill=fillc, outline=OUTLINE)
        d.text((x + 80 - d.textlength(lab, font=F2) / 2, top + h / 2 - 6), lab,
               font=F2, fill=TEXT if active else MUTED)
    d.line([(0, 52), (319, 52)], fill=OUTLINE)

    word = "laconic"
    d.text((W / 2 - d.textlength(word, font=F4) / 2, 60), word, font=F4, fill=TEXT)

    opts = ["using very few words", "eager to argue or fight",
            "lasting a very short time", "showing great attention to detail"]
    for i, o in enumerate(opts):
        y = 88 + i * 34
        fillc = SUCCESS if i == 0 else PANEL
        ink = (0, 0, 0) if i == 0 else TEXT
        d.rounded_rectangle([10, y, 309, y + 30], 5, fill=fillc, outline=OUTLINE)
        d.text((18, y + 15 - 5), o, font=F1, fill=ink)

    d.text((8, 226), "40 pts  x4", font=F1, fill=MUTED)
    lbl = "132 / 250"
    d.text((312 - d.textlength(lbl, font=F1), 226), lbl, font=F1, fill=MUTED)
    return im


def grewords_study():
    """GRE Words, Study tab: the card flipped to its meaning."""
    im, d = blank(); topbar(d, "GRE Words")
    for i, (lab, x) in enumerate([("Study", 0), ("Quiz", 160)]):
        active = (i == 0)
        top = 30 if active else 34
        h = 22 if active else 18
        fillc = SURFACE if active else PANEL
        d.rounded_rectangle([x, top, x + 159, top + h], 4, fill=fillc, outline=OUTLINE)
        d.text((x + 80 - d.textlength(lab, font=F2) / 2, top + h / 2 - 6), lab,
               font=F2, fill=TEXT if active else MUTED)
    d.line([(0, 52), (319, 52)], fill=OUTLINE)

    d.rounded_rectangle([12, 58, 307, 187], 8, fill=SURFACE, outline=OUTLINE)
    d.text((160 - d.textlength("laconic", font=F2) / 2, 70), "laconic", font=F2, fill=TEXT)
    d.text((22, 94), "using very few words", font=F1, fill=SUCCESS)
    d.text((22, 140), "His laconic reply was a single word.", font=F1, fill=MUTED)

    button(d, (12, 192, 92, 30), "Knew it", GREEN, WHITE)
    button(d, (114, 192, 92, 30), "Didn't", (178, 58, 58), WHITE)
    button(d, (216, 192, 92, 30), "Next")
    d.text((8, 226), "40 pts  x4", font=F1, fill=MUTED)
    return im


def scores_mine():
    """Scores tab: this player's best and worst per game."""
    im, d = blank(); topbar(d, "Scores")
    # Tab strip at y=32-50
    for i, (lab, x) in enumerate([("Mine", 8), ("Device", 80)]):
        active = (i == 0)
        top = 34 if active else 38
        h = 18 if active else 14
        fill = SURFACE if active else PANEL
        d.rounded_rectangle([x, top, x + 64, top + h], 4, fill=fill, outline=OUTLINE)
        if active:
            d.line([(x + 6, top + 1), (x + 58, top + 1)], fill=shade(fill, 150))
        d.text((x + 32 - d.textlength(lab, font=F2) / 2, top + h / 2 - 6), lab,
               font=F2, fill=TEXT if active else MUTED)
    d.line([(8, 52), (312, 52)], fill=OUTLINE)

    # Profile name and column headings
    d.text((8, 60), "Ava", font=F2, fill=TEXT)
    d.text((244 - d.textlength("best", font=F1), 64), "best", font=F1, fill=MUTED)
    d.text((306 - d.textlength("worst", font=F1), 64), "worst", font=F1, fill=MUTED)

    # Sample score rows
    rows = [
        ("Multiply", "250pts", "240pts"),
        ("Math", "120pts", "80pts"),
        ("Memory", "35s", "45s"),
        ("Maze", "9lvl", "7lvl"),
        ("Slide", "18moves", "25moves"),
    ]
    for i, (name, best, worst) in enumerate(rows):
        y = 56 + (i + 1) * 30
        d.rounded_rectangle([8, y, 312, y + 28], 4, fill=SURFACE, outline=OUTLINE)
        d.text((16, y + 14 - 6), name, font=F2, fill=TEXT)
        d.text((244 - d.textlength(best, font=F2) / 2, y + 14 - 6), best, font=F2, fill=SUCCESS)
        d.text((306 - d.textlength(worst, font=F2) / 2, y + 14 - 6), worst, font=F2, fill=MUTED)
        if i >= 2:  # Show "lower is better" on Memory
            if i == 2:
                d.text((100, y + 14 - 6), "lower is better", font=F1, fill=MUTED)

    # Pager buttons
    button(d, (8, 208, 88, 26), "Prev")
    button(d, (104, 208, 112, 26), "Switch player", BLUE, WHITE, F2)
    button(d, (224, 208, 88, 26), "Next")
    return im


def scores_device():
    """Scores tab: device-wide best and holder per game."""
    im, d = blank(); topbar(d, "Scores")
    # Tab strip at y=32-50
    for i, (lab, x) in enumerate([("Mine", 8), ("Device", 80)]):
        active = (i == 1)
        top = 34 if active else 38
        h = 18 if active else 14
        fill = SURFACE if active else PANEL
        d.rounded_rectangle([x, top, x + 80, top + h], 4, fill=fill, outline=OUTLINE)
        if active:
            d.line([(x + 6, top + 1), (x + 74, top + 1)], fill=shade(fill, 150))
        d.text((x + 40 - d.textlength(lab, font=F2) / 2, top + h / 2 - 6), lab,
               font=F2, fill=TEXT if active else MUTED)
    d.line([(8, 52), (312, 52)], fill=OUTLINE)

    # Sample device best rows
    rows = [
        ("Multiply", "250pts", "Priya"),    # Priya is current player (gold)
        ("Math", "150pts", "Kai"),
        ("Memory", "30s", "Liam"),
        ("Maze", "12lvl", "Ava"),
        ("Slide", "10moves", "Priya"),
    ]
    for i, (name, best, holder) in enumerate(rows):
        y = 56 + (i + 1) * 30
        d.rounded_rectangle([8, y, 312, y + 28], 4, fill=SURFACE, outline=OUTLINE)
        d.text((16, y + 14 - 6), name, font=F2, fill=TEXT)
        d.text((244 - d.textlength(best, font=F2) / 2, y + 14 - 6), best, font=F2, fill=SUCCESS)
        holder_color = GOLD if holder == "Priya" else MUTED
        d.text((312 - d.textlength(holder, font=F2) / 2, y + 14 - 6), holder, font=F2, fill=holder_color)

    # Pager buttons
    button(d, (8, 208, 88, 26), "Prev")
    button(d, (104, 208, 112, 26), "Switch player", BLUE, WHITE, F2)
    button(d, (224, 208, 88, 26), "Next")
    return im


def profiles_pick():
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    # Header band
    d.rectangle([0, 0, W, 30], fill=SURFACE)
    d.text((10, 15 - 9), PRODUCT, font=F4, fill=TEXT)
    d.text((W - 8 - d.textlength(COPYRIGHT_SHORT, font=F1), 15 - 5), COPYRIGHT_SHORT, font=F1, fill=MUTED)
    # "Who is playing?" and guest hint. Baselines come from ProfileGame::render:
    # promptY = 32 in landscape, and the hint sits 18px below it.
    d.text((W / 2 - d.textlength("Who is playing?", font=F2) / 2, 32), "Who is playing?", font=F2, fill=TEXT)
    d.text((W / 2 - d.textlength("Guest plays without saving scores", font=F1) / 2, 50),
           "Guest plays without saving scores", font=F1, fill=MUTED)
    # Six rows (five children + Guest) from ProfileGame's rowsTop/rowsPitch.
    profiles = ["Alice", "Bob", "Carol", "Diana", "Eve", "Guest"]
    for i, prof in enumerate(profiles):
        y = 60 + i * 25
        slot_w = W - 22 - 62
        # Profile slot
        d.rounded_rectangle([8, y, 8 + slot_w, y + 23], 4, fill=PANEL if prof != "Alice" else BLUE, outline=OUTLINE)
        d.text((8 + 4 + (slot_w - 8 - d.textlength(prof, font=F2)) / 2, y + 23 / 2 - 6),
               prof, font=F2, fill=WHITE if prof == "Alice" else TEXT)
        # Edit button for non-Guest
        if prof != "Guest":
            d.rounded_rectangle([W - 8 - 62, y, W - 8, y + 23], 4, fill=SURFACE, outline=OUTLINE)
            d.text((W - 8 - 62 + (62 - d.textlength("Edit", font=F2)) / 2, y + 23 / 2 - 6),
                   "Edit", font=F2, fill=MUTED)
    # Add Player and Done buttons
    btn_w = (W - 24) // 2
    button(d, (8, H - 30, btn_w, 26), "Add Player", GREEN, WHITE)
    button(d, (16 + btn_w, H - 30, btn_w, 26), "Done")
    return im


# Geometry below comes from DiceGame's and CoinFlipGame's own Rect helpers:
# the count buttons at (86 + i*52, 46, 44, 26), the action button at
# (85, 198, 150, 34), dice at 68px on a 14px gap, coin at r=34 centred on
# (160, 116). Change either game and change these to match.
DIE_PIPS = {
    2: (0, 8),
    5: (0, 2, 4, 6, 8),
    6: (0, 2, 3, 5, 6, 8),
}


def dice():
    im, d = blank(); topbar(d, "Dice")
    d.text((W / 2 - d.textlength("How many dice?", font=F1) / 2, 34),
           "How many dice?", font=F1, fill=MUTED)
    for i, label in enumerate("123"):
        on = label == "3"
        button(d, (86 + i * 52, 46, 44, 26), label,
               BLUE if on else PANEL, WHITE if on else TEXT)

    faces = (5, 2, 6)
    span = 3 * 68 + 2 * 14
    x0 = (W - span) // 2
    for i, face in enumerate(faces):
        x = x0 + i * 82
        d.rounded_rectangle([x, 88, x + 67, 155], 10, fill=WHITE, outline=OUTLINE)
        step = 68 // 4
        for cell in DIE_PIPS[face]:
            cx = x + step + (cell % 3) * step
            cy = 88 + step + (cell // 3) * step
            d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=(0, 0, 0))

    total = "Total %d" % sum(faces)
    d.text((W / 2 - d.textlength(total, font=F4) / 2, 178 - 9), total, font=F4, fill=TEXT)
    button(d, (85, 198, 150, 34), "Roll", GREEN, WHITE, F4)
    return im


COIN_FACE, COIN_EDGE = (226, 182, 72), (150, 116, 34)


def coinflip():
    im, d = blank(); topbar(d, "Coin Flip")
    d.text((W / 2 - d.textlength("Best of how many?", font=F1) / 2, 34),
           "Best of how many?", font=F1, fill=MUTED)
    for i, label in enumerate("135"):
        on = label == "5"
        button(d, (86 + i * 52, 46, 44, 26), label,
               BLUE if on else PANEL, WHITE if on else TEXT)

    # The settled coin: full width, so it carries a readable face.
    d.ellipse([126, 82, 194, 150], fill=COIN_FACE, outline=COIN_EDGE)
    d.text((160 - d.textlength("H", font=F4) / 2, 116 - 9), "H", font=F4, fill=COIN_EDGE)

    results = "HTHTH"
    px0 = (W - (len(results) - 1) * 28) // 2
    for i, side in enumerate(results):
        px = px0 + i * 28
        d.ellipse([px - 10, 156, px + 10, 176], fill=COIN_FACE, outline=COIN_EDGE)
        d.text((px - d.textlength(side, font=F1) / 2, 166 - 5), side, font=F1, fill=COIN_EDGE)

    verdict = "Heads wins 3-2"
    d.text((W / 2 - d.textlength(verdict, font=F2) / 2, 187 - 7), verdict, font=F2, fill=TEXT)
    button(d, (85, 198, 150, 34), "Flip", GREEN, WHITE, F4)
    return im


EXTRA_SCREENS = [
    ("scores-mine", scores_mine, "Scores: this player"),
    ("scores-device", scores_device, "Scores: device best"),
    ("profiles", profiles_pick, "Profiles: who is playing"),
    ("nearby", nearby, "Nearby: who else is playing"),
    ("systeminfo-ble", systeminfo_ble, "System Info: what BLE is broadcasting"),
    ("systeminfo-memory", systeminfo_memory, "System Info: heap and CPU"),
    ("about-radios", about_radios, "About: what the radios do"),
    ("tictactoe", tictactoe, "Tic-Tac-Toe"),
    ("memory", memory, "Memory Match"),
    ("math", math_game, "Math"),
    ("multiply", multiply, "Multiplication"),
    ("time", time_game, "Time"),
    ("whack", whack, "Whack A Mole"),
    ("microku", microku, "Microku"),
    ("shapes", shapes, "Shape & Color"),
    ("counting", counting, "Counting"),
    ("money", money, "Money"),
    ("fractions", fractions, "Fractions"),
    ("maze", maze, "Maze"),
    ("sorting", sorting, "Sorting"),
    ("colormix", colormix, "Color Mix"),
    ("slide", slide, "Slide Puzzle"),
    ("oddone", oddone, "Odd One Out"),
    ("shapearith", shapearith, "Shape Arith"),
    ("calendar", calendar, "Calendar"),
    ("numberline", numberline, "Number Line"),
    ("percent", percent, "Percent: read the circle"),
    ("grewords", grewords, "GRE Words: pick the meaning"),
    ("grewords-study", grewords_study, "GRE Words: study card"),
    ("dice", dice, "Dice: three dice thrown"),
    ("coinflip", coinflip, "Coin Flip: best of five"),
]
SCREENS.extend(EXTRA_SCREENS)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    if not HAVE_ART:
        print("NOTE: map-n-flag checkout not found; flag/outline art will be blank")
    for name, fn, _desc in SCREENS:
        img = fn()
        img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(OUT / f"{name}.png")
        print(f"  {name}.png  {img.width}x{img.height}")
    print(f"wrote {len(SCREENS)} screens to {OUT}")


if __name__ == "__main__":
    main()
