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

import math
import re
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
# This file cannot include a C header, so the mirror is manual: change
# either of these and change AppVersion.h in the same commit.
PRODUCT = "Braino!"
COPYRIGHT_SHORT = "(C) iamankushpandit"

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


def fitted_text(d, text, max_w, f):
    s = text
    while len(s) > 2 and d.textlength(s, font=f) > max_w:
        s = s[:-1]
    if len(s) < len(text) and len(s) > 1:
        s = s[:-1] + "."
    return s


def centered_fitted(d, text, cx, y, max_w, f, fill):
    s = fitted_text(d, text, max_w, f)
    d.text((cx - d.textlength(s, font=f) / 2, y), s, font=f, fill=fill)


def sync_badge(d, cx, cy, synced=True):
    d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=SUCCESS if synced else WARN)
    d.text((cx - 3, cy - 6), "v" if synced else "!", font=F1, fill=(0, 0, 0))


# Mirrors Ui::drawWifiBadge -- a fan of arcs over a dot, not signal bars. The
# offsets are the same table the firmware carries, relative to the dot.
WIFI_ARCS = (
    ((-2, -3), (-1, -3), (0, -3), (1, -3), (2, -3), (-2, -2), (2, -2)),
    ((-2, -6), (-1, -6), (0, -6), (1, -6), (2, -6), (-4, -5), (-3, -5), (-2, -5),
     (2, -5), (3, -5), (4, -5)),
    ((-3, -9), (-2, -9), (-1, -9), (0, -9), (1, -9), (2, -9), (3, -9), (-5, -8),
     (-4, -8), (-3, -8), (3, -8), (4, -8), (5, -8), (-6, -7), (-5, -7), (5, -7),
     (6, -7)),
)


def wifi_badge(d, cx, cy, bars=3):
    """`bars` is the lit level 0-4: 0 nothing, 1 the dot, then one arc each."""
    off = (70, 74, 84)
    dot_y = cy + 5
    d.rectangle([cx - 1, dot_y - 1, cx, dot_y], fill=SUCCESS if bars >= 1 else off)
    for band, arc in enumerate(WIFI_ARCS):
        col = SUCCESS if bars >= band + 2 else off
        for dx, dy in arc:
            d.point((cx + dx, dot_y + dy), fill=col)


def lock_icon(d, r, color=TEXT, bg=SURFACE):
    """Mirrors Ui::drawLockIcon: a hoop over a body, keyhole punched back out."""
    x, y, w, h = r
    cx = x + w // 2
    body_w = w - w // 4
    body_h = max(7, h * 5 // 11)
    body_y = y + h - body_h - h // 10
    shackle_w = max(6, body_w - body_w // 3)
    shackle_h = body_y - y + body_h // 2
    stroke = max(1, w // 12)
    for i in range(stroke):
        d.rounded_rectangle([cx - shackle_w // 2 + i, y + i,
                             cx + shackle_w // 2 - i, y + shackle_h - i],
                            max(2, shackle_w // 2 - i), outline=color)
    d.rounded_rectangle([cx - body_w // 2, body_y, cx + body_w // 2, body_y + body_h],
                        max(2, body_w // 6), fill=color)
    key_r = max(1, body_w // 8)
    key_cy = body_y + body_h // 2 - key_r // 2
    d.ellipse([cx - key_r, key_cy - key_r, cx + key_r, key_cy + key_r], fill=bg)
    d.rectangle([cx - max(1, key_r // 2), key_cy,
                 cx + max(1, key_r // 2) - 1, key_cy + body_h // 3], fill=bg)


def topbar(d, title, synced=True, bars=3, w=W):
    """Ui::drawTopBar reads tft.width() at render time, so this takes a width
    rather than assuming the landscape canvas -- that is what lets the portrait
    mock-ups below carry the same bar the device draws."""
    d.rectangle([0, 0, w - 1, 29], fill=SURFACE)
    d.line([(0, 0), (w, 0)], fill=shade(SURFACE, 145))
    d.line([(0, 29), (w, 29)], fill=shade(SURFACE, 60))
    # Home narrowed from 42px to 32px to make room for Lock beside it, and the
    # title starts at 62 instead of 48. See LauncherLayout.
    d.rounded_rectangle([2, 5, 30, 24], 3, outline=MUTED)
    d.text((5, 9), "home", font=F1, fill=MUTED)
    lock_icon(d, (40, 6, 18, 18))
    t = "12:41 AM"
    batt_w = battery_width(72)
    batt_right = w - 40
    wifi_cx = batt_right - batt_w - 6 - 8
    sync_cx = wifi_cx - 8 - 6 - 6
    clock_right = sync_cx - 12
    # Ui::drawTopBar's own rule, restated: the title is truncated to the gap
    # between where it starts and where the clock begins -- but with a 32px
    # floor, so on a narrow panel it stops shrinking and runs under the clock
    # instead. At 240px wide that floor bites: the gap is about 14px and the
    # title is drawn 32px, so a portrait top bar really does overlap. Mirrored
    # rather than tidied, because tidying it here would hide it.
    status_left = clock_right - d.textlength(t, font=F2)
    title_max = max(32, status_left - 62 - 4)
    s = title
    while len(s) > 2 and d.textlength(s, font=F2) > title_max:
        s = s[:-1]
    d.text((62, 8), s, font=F2, fill=TEXT)
    d.text((clock_right - d.textlength(t, font=F2), 8), t, font=F2, fill=TEXT)
    sync_badge(d, sync_cx, 15, synced)
    wifi_badge(d, wifi_cx, 15, bars)
    battery_badge(d, batt_right - batt_w // 2, 15, 72)
    d.ellipse([w - 34, 4, w - 12, 26], outline=TEXT)


BATT_H, BATT_PAD, BATT_TERM_W = 15, 3, 2
BATT_TRACK_H = 4


def battery_width(pct=72):
    """Mirrors Ui::batteryBadgeWidth. Font 1 advances exactly 6px on the
    device, so the width is counted in characters rather than measured with
    PIL's proportional stand-in -- otherwise the mock-up packs differently
    from the firmware and stops being evidence."""
    text = "" if pct is None or pct < 0 else str(min(pct, 100))
    inner = BATT_PAD * 2 + 6 * len(text)
    return max(inner, 11) + 2 + BATT_TERM_W


def battery_badge(d, cx, cy, pct=72):
    """Mirrors Ui::drawBatteryBadge: the percentage as numerals inside the
    shell over a bordered two-pixel level gauge along the inside bottom. No
    charging state. Variable width -- see battery_width."""
    text = "" if pct is None or pct < 0 else str(min(pct, 100))
    total = battery_width(pct)
    shell = total - BATT_TERM_W
    bx, by = cx - total // 2, cy - BATT_H // 2
    low = 0 <= pct <= 15
    out = ERROR if low else MUTED
    if pct <= 15:
        level = ERROR
    elif pct <= 40:
        level = WARN
    else:
        level = SUCCESS
    d.rectangle([bx, by, bx + shell - 1, by + BATT_H - 1], outline=out)
    d.rectangle([bx + shell, cy - 3, bx + shell + 1, cy + 3], fill=out)
    track_x = bx + 2
    track_y = by + BATT_H - 1 - BATT_TRACK_H
    track_w = shell - 4
    if track_w > 2:
        d.rectangle([track_x, track_y, track_x + track_w - 1, track_y + BATT_TRACK_H - 1], outline=MUTED)
    if pct > 0 and track_w > 2:
        gw = max(1, int(pct * (track_w - 2) / 100))
        d.rectangle([track_x + 1, track_y + 1, track_x + gw, track_y + BATT_TRACK_H - 2], fill=level)
    penx = bx + 1 + BATT_PAD
    if text:
        d.text((penx, cy - 6), text, font=F1, fill=out)


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

def piano():
    """Piano: one octave, a key held.

    Geometry taken from PianoGame's own rects, not approximated -- eight white
    keys dividing the span, black keys three fifths as wide and two thirds as
    tall, straddling the boundary between their neighbours.
    """
    im, d = blank(); topbar(d, "Piano")
    import math
    top, capH, m = 34, 22, 4
    kb = (m, top, W - m * 2, H - top - capH - m)
    whites = ["C", "D", "E", "F", "G", "A", "B", "C"]
    lit = 4                                    # G held down
    for i, name in enumerate(whites):
        x0 = kb[0] + kb[2] * i // 8
        x1 = kb[0] + kb[2] * (i + 1) // 8
        fill = SUCCESS if i == lit else (248, 248, 244)
        d.rectangle([x0, kb[1], x1 - 1, kb[1] + kb[3] - 1], fill=fill, outline=OUTLINE)
        tw = d.textlength(name, font=F2)
        d.text((x0 + (x1 - x0) / 2 - tw / 2, kb[1] + kb[3] - 20), name, font=F2,
               fill=BG if i == lit else (60, 60, 70))
    for i, after in enumerate([0, 1, 3, 4, 5]):
        x0 = kb[0] + kb[2] * after // 8
        x1 = kb[0] + kb[2] * (after + 1) // 8
        bw = (x1 - x0) * 3 // 5
        bx = x1 - bw // 2
        d.rectangle([bx, kb[1], bx + bw - 1, kb[1] + kb[3] * 2 // 3], fill=(24, 24, 28),
                    outline=OUTLINE)
    cap = "Tap the keys"
    d.text((W / 2 - d.textlength(cap, font=F1) / 2, H - 14), cap, font=F1, fill=MUTED)
    return im


def chess():
    """Chess: a piece selected, its moves ringed, and the panel beside it.

    The geometry is ChessGame's, restated: the board takes the full height in
    landscape and the panel takes the width left over. Getting that wrong here
    would be worse than useless -- a mock-up is the only place most people look
    at this screen -- so every number below is derived the same way the
    firmware derives it, from TOP_BAR_H, MARGIN, ACTION_H and STATUS_H.

    The sprite masks are read from the generated table, so the pieces cannot
    drift from the ones the device actually draws.

    NOTE the standing caveat from CLAUDE.md: this draws elements in isolation
    with no clear rectangles at all, so it cannot show an erase-over between
    the panel and the board. Looking right here is not evidence.
    """
    im, d = blank(); topbar(d, "Chess")
    import re as _re
    top, m, gap = 33, 3, 3
    action_h, status_h = 26, 24
    side = (H - top - m) // 8 * 8
    bx, by = m, top
    cell = side // 8

    px = bx + side + gap
    pw = W - px - m
    controls = status_h + gap + action_h
    body = side - controls - gap
    each = (body - gap) // 2

    masks = {}
    src = (ROOT / "src" / "games" / "ChessSprites.cpp").read_text(encoding="utf-8")
    blocks = _re.findall(r"\{\s*//\s*(\w+)(.*?)\}", src, _re.S)
    for name, body_ in blocks:
        masks[name] = [int(v, 16) for v in _re.findall(r"0x([0-9A-Fa-f]{8})", body_)]

    # The opening position after 1.e4, with the white queen selected.
    back = ["ROOK", "KNIGHT", "BISHOP", "QUEEN", "KING", "BISHOP", "KNIGHT", "ROOK"]
    board = {}
    for f in range(8):
        board[(f, 0)] = (back[f], True)
        board[(f, 1)] = ("PAWN", True)
        board[(f, 6)] = ("PAWN", False)
        board[(f, 7)] = (back[f], False)
    del board[(4, 1)]
    board[(4, 3)] = ("PAWN", True)             # the pawn on e4
    selected = (3, 0)                          # white queen
    targets = [(4, 1), (5, 2), (6, 3), (7, 4)]

    def paint(mask, pxx, pyy, size, colour):
        for ry in range(size):
            row = mask[ry * 32 // size]
            for rx in range(size):
                if (row >> (31 - rx * 32 // size)) & 1:
                    d.point((pxx + rx, pyy + ry), fill=colour)

    def piece(mask, x0, y0, size, white):
        paint(mask, x0 + 1, y0 + 1, size, (20, 20, 26) if white else (250, 250, 246))
        paint(mask, x0, y0, size, (250, 250, 246) if white else (20, 20, 26))

    for f in range(8):
        for r in range(8):
            x0, y0 = bx + f * cell, by + (7 - r) * cell
            light = (f + r) % 2 != 0
            fill = SUCCESS if (f, r) == selected else ((222, 210, 180) if light else (120, 96, 72))
            d.rectangle([x0, y0, x0 + cell - 1, y0 + cell - 1], fill=fill)
            if (f, r) in board:
                name, white = board[(f, r)]
                piece(masks[name], x0 + 1, y0 + 1, cell - 2, white)
            if (f, r) in targets:
                cx, cy = x0 + cell // 2, y0 + cell // 2
                rr = cell // 2 - 2
                d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=SUCCESS)
    d.rectangle([bx, by, bx + side - 1, by + side - 1], outline=OUTLINE)

    # The panel: what each side has lost, the status, and the one button.
    tcell = 20
    for i, (ty, lost) in enumerate((
            (by, [("PAWN", True), ("PAWN", True), ("KNIGHT", True)]),
            (by + each + gap, [("PAWN", False), ("BISHOP", False)]))):
        # Backed by the shade its pieces are NOT, exactly as ChessGame does:
        # White's losses on the dark square colour, Black's on the light one.
        strip = (120, 96, 72) if i == 0 else (222, 210, 180)
        d.rectangle([px, ty, px + pw - 1, ty + each - 1], fill=strip)
        cols = pw // tcell
        for n, (name, white) in enumerate(lost):
            x0 = px + (n % cols) * tcell
            y0 = ty + (n // cols) * tcell
            piece(masks[name], x0 + 1, y0 + 1, tcell - 2, white)

    sy = by + side - action_h - gap - status_h
    d.text((px, sy), "White", font=F1, fill=TEXT)
    d.text((px, sy + 10), "to move", font=F1, fill=MUTED)
    button(d, (px, by + side - action_h, pw, action_h), "End game")
    return im


def sea_battle():
    """Sea Battle: hunting the enemy fleet, with your own sea beside it.

    Geometry restated from SeaBattleGame: the board takes the canvas height and
    the panel takes the width left over. Landscape only, on the fixed 320x240
    canvas, which is why there is no orientation branch here either.
    """
    im, d = blank(); topbar(d, "Sea Battle")
    top, m, gap = 33, 3, 3
    action_h, status_h, tally_h, label_h = 26, 24, 12, 11
    side = (H - top - m) // 8 * 8
    bx, by = m, top
    cell = side // 8

    px = bx + side + gap
    pw = W - px - m

    SEA, GRIDL = (30, 62, 104), (58, 96, 142)
    MISS, HIT, SHIP = (150, 168, 190), (214, 66, 54), (120, 128, 140)

    # A game part way through: some misses, some hits, one ship going down.
    misses = {5, 9, 18, 22, 33, 41, 47, 52, 58, 61}
    hits = {26, 27, 28, 35}
    pending = {13}

    for c in range(64):
        col, row = c % 8, c // 8
        x0, y0 = bx + col * cell, by + row * cell
        d.rectangle([x0, y0, x0 + cell - 1, y0 + cell - 1], fill=SEA, outline=GRIDL)
        cx, cy = x0 + cell // 2, y0 + cell // 2
        if c in misses:
            r = cell // 6
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=MISS)
        elif c in hits:
            r = cell // 3
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=HIT)
        elif c in pending:
            r = cell // 3
            d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=WARN)
    d.rectangle([bx, by, bx + side - 1, by + side - 1], outline=OUTLINE)

    # Our own sea, small, in the column: ships, damage, and their wasted shots.
    ms = (pw // 8) * 8
    mx, my = px + (pw - ms) // 2, by + label_h
    mcell = ms // 8
    d.text((mx, my - label_h), "Your sea", font=F1, fill=MUTED)
    ours = {1, 2, 3, 4, 19, 27, 35, 44, 45, 58, 59}
    incoming = {4, 19, 27, 30, 51, 58}
    for c in range(64):
        col, row = c % 8, c // 8
        x0, y0 = mx + col * mcell, my + row * mcell
        if c in ours and c in incoming:
            fill = HIT
        elif c in ours:
            fill = SHIP
        elif c in incoming:
            fill = MISS
        else:
            fill = SEA
        d.rectangle([x0, y0, x0 + mcell - 1, y0 + mcell - 1], fill=fill)
    d.rectangle([mx, my, mx + ms - 1, my + ms - 1], outline=OUTLINE)

    sy = my + ms + gap + 2
    d.text((px, sy), "Hit!", font=F2, fill=SUCCESS)
    d.text((px, sy + 14), "fire again", font=F1, fill=MUTED)
    d.text((px, sy + status_h), "Hits 4/11  lost 3", font=F1, fill=MUTED)
    button(d, (px, by + side - action_h, pw, action_h), "End game")
    return im


# Ludo. Every number here is restated from src/games/LudoBoard.cpp -- the
# cell size, the board and panel origins, the yard insets and spot offsets,
# the colours -- so the picture is the device's layout rather than an
# impression of it. Change one there, change it here.
LUDO_SEAT = [(220, 48, 48), (36, 160, 72), (240, 196, 24), (32, 112, 216)]
LUDO_NAMES = ["Red", "Green", "Yellow", "Blue"]
LUDO_PAPER, LUDO_RULE, LUDO_INK = (250, 250, 244), (120, 124, 132), (26, 34, 48)
LUDO_SPOT, LUDO_STAR, LUDO_HI = (226, 229, 234), (150, 154, 162), (120, 230, 255)
LUDO_QUARTER = [(1, 6), (2, 6), (3, 6), (4, 6), (5, 6), (6, 5), (6, 4), (6, 3),
                (6, 2), (6, 1), (6, 0), (7, 0), (8, 0)]
LUDO_YARD = [(0, 0), (9, 0), (9, 9), (0, 9)]
LUDO_CELL, LUDO_BX, LUDO_BY = 13, 6, 37
LUDO_PX = LUDO_BX + 15 * LUDO_CELL + 8
LUDO_PW = W - LUDO_PX - 6


def _ludo_rot(c, times):
    col, row = c
    for _ in range(times):
        col, row = 14 - row, col
    return col, row


def _ludo_track(a):
    return _ludo_rot(LUDO_QUARTER[a % 13], a // 13)


def _ludo_home(seat, step):
    return _ludo_rot((1 + step, 7), seat)


def _ludo_cell_xy(c):
    return LUDO_BX + c[0] * LUDO_CELL, LUDO_BY + c[1] * LUDO_CELL


def _ludo_on_seat(seat):
    return LUDO_INK if seat == 2 else WHITE


def _ludo_token(d, cx, cy, seat, r, count=1):
    """One token: a shape per colour as well as the colour, as on the device."""
    fill = LUDO_SEAT[seat]
    if seat == 0:
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill, outline=LUDO_INK)
    elif seat == 1:
        d.rectangle([cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1], fill=fill, outline=LUDO_INK)
    elif seat == 2:
        d.polygon([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)], fill=fill, outline=LUDO_INK)
    else:
        d.polygon([(cx, cy - r), (cx + r, cy + r), (cx - r, cy + r)], fill=fill, outline=LUDO_INK)
    if count > 1:
        s = str(count)
        d.text((cx - d.textlength(s, font=F1) / 2, cy - 5), s, font=F1, fill=_ludo_on_seat(seat))


def _ludo_star(d, cx, cy, r, fill):
    pts = []
    for i in range(10):
        rad = r if i % 2 == 0 else r * 0.45
        ang = -math.pi / 2 + i * math.pi / 5
        pts.append((cx + rad * math.cos(ang), cy + rad * math.sin(ang)))
    d.polygon(pts, fill=fill)


def _ludo_board(d, pos, lit):
    """pos: seat -> four relative squares (None = yard, 56 = home).
    lit: set of (seat, token) that can move and are lit cyan."""
    C = LUDO_CELL
    d.rectangle([LUDO_BX - 1, LUDO_BY - 1, LUDO_BX + 15 * C, LUDO_BY + 15 * C], outline=LUDO_INK)
    for s in range(4):
        yx, yy = LUDO_BX + LUDO_YARD[s][0] * C, LUDO_BY + LUDO_YARD[s][1] * C
        d.rectangle([yx, yy, yx + 6 * C - 1, yy + 6 * C - 1], fill=LUDO_SEAT[s])
        d.rounded_rectangle([yx + 9, yy + 9, yx + 6 * C - 10, yy + 6 * C - 10], 6, fill=LUDO_PAPER)
        for t in range(4):
            sx = yx + (54 if t & 1 else 24)
            sy = yy + (54 if t & 2 else 24)
            here = s in pos and pos[s][t] is None
            d.ellipse([sx - 8, sy - 8, sx + 8, sy + 8],
                      fill=LUDO_HI if here and (s, t) in lit else LUDO_SPOT, outline=LUDO_SEAT[s])
            if here:
                _ludo_token(d, sx, sy, s, 6)

    # Who stands on which grid cell.
    at = {}
    for s, toks in pos.items():
        for t, rel in enumerate(toks):
            if rel is None or rel >= 56:
                continue
            c = _ludo_track((rel + 13 * s) % 52) if rel <= 50 else _ludo_home(s, rel - 51)
            at.setdefault(c, []).append((s, t))

    cells = [(_ludo_track(a), a) for a in range(52)]
    cells += [(_ludo_home(s, k), None) for s in range(4) for k in range(5)]
    home_seat = {_ludo_home(s, k): s for s in range(4) for k in range(5)}
    for c, a in cells:
        x, y = _ludo_cell_xy(c)
        if a is not None and a % 13 == 0:
            base = LUDO_SEAT[a // 13]
        elif a is None:
            base = LUDO_SEAT[home_seat[c]]
        else:
            base = LUDO_PAPER
        here = at.get(c, [])
        is_lit = any(k in lit for k in here)
        d.rectangle([x, y, x + C - 1, y + C - 1], fill=LUDO_RULE)
        d.rectangle([x + 1, y + 1, x + C - 1, y + C - 1], fill=LUDO_HI if is_lit else base)
        cx, cy = x + C // 2 + 1, y + C // 2 + 1
        if a is not None and a % 13 == 8 and not here:
            _ludo_star(d, cx, cy, 5, LUDO_STAR)
        seats = sorted({s for s, _ in here})
        if len(seats) == 1:
            _ludo_token(d, cx, cy, seats[0], 5, len(here))
        elif seats:
            corner = [(-3, -3), (3, -3), (3, 3), (-3, 3)]
            for s in seats:
                _ludo_token(d, cx + corner[s][0], cy + corner[s][1], s, 3)

    x0, y0 = LUDO_BX + 6 * C, LUDO_BY + 6 * C
    x1, y1 = x0 + 3 * C - 1, y0 + 3 * C - 1
    mx, my = (x0 + x1) // 2, (y0 + y1) // 2
    d.polygon([(x0, y0), (x0, y1), (mx, my)], fill=LUDO_SEAT[0])
    d.polygon([(x0, y0), (x1, y0), (mx, my)], fill=LUDO_SEAT[1])
    d.polygon([(x1, y0), (x1, y1), (mx, my)], fill=LUDO_SEAT[2])
    d.polygon([(x0, y1), (x1, y1), (mx, my)], fill=LUDO_SEAT[3])
    d.line([(x0, y0), (x1, y1)], fill=LUDO_INK)
    d.line([(x1, y0), (x0, y1)], fill=LUDO_INK)
    span, mid = 3 * C - 1, (3 * C - 1) // 2
    spots = [(x0 + 7, y0 + mid), (x0 + mid, y0 + 7), (x0 + span - 7, y0 + mid), (x0 + mid, y0 + span - 7)]
    for s, toks in pos.items():
        home = sum(1 for rel in toks if rel is not None and rel >= 56)
        if home:
            hx, hy = spots[s]
            d.text((hx - d.textlength(str(home), font=F1) / 2, hy - 5), str(home), font=F1,
                   fill=_ludo_on_seat(s))


def ludo():
    """Ludo: Red, a player, holding a 4 against two computers.

    Red's two tokens on one square are a block and carry a 2; Green shares a
    star with nobody yet; Yellow has one token home and one in its column. The
    two squares Red can move from are lit, which is what a player sees after
    rolling when there is a real choice to make.
    """
    im, d = blank(); topbar(d, "Ludo")
    pos = {0: [None, 3, 20, 20], 1: [None, None, 8, 17], 2: [8, 30, 53, 56]}
    _ludo_board(d, pos, lit={(0, 1), (0, 2), (0, 3)})

    px, pw = LUDO_PX, LUDO_PW
    _ludo_token(d, px + 7, 38 + 9, 0, 6)
    d.text((px + 17, 39), "Red", font=F2, fill=TEXT)

    dx, dy = px + (pw - 44) // 2, 62
    d.rounded_rectangle([dx, dy, dx + 43, dy + 43], 8, fill=LUDO_SEAT[0])
    d.rounded_rectangle([dx + 4, dy + 4, dx + 39, dy + 39], 6, fill=(252, 252, 250))
    cx, cy = dx + 22, dy + 22
    for ox, oy in ((-10, -10), (10, 10), (10, -10), (-10, 10)):
        d.ellipse([cx + ox - 4, cy + oy - 4, cx + ox + 4, cy + oy + 4], fill=LUDO_INK)

    msg = "Pick a token"
    d.text((px + (pw - d.textlength(msg, font=F2)) / 2, 114), msg, font=F2, fill=TEXT)

    y = 132
    for s, right in ((0, ""), (1, "CPU"), (2, "CPU")):
        if s == 0:
            # The turn dot, lit: it blinks beside whoever's turn it is.
            d.ellipse([px + 1, y + 5, px + 7, y + 11], fill=TEXT)
        _ludo_token(d, px + 14, y + 8, s, 5)
        d.text((px + 23, y + 1), LUDO_NAMES[s], font=F2, fill=TEXT)
        if right:
            d.text((px + pw - 2 - d.textlength(right, font=F2), y + 1), right, font=F2, fill=MUTED)
        y += 17
    button(d, (px + 6, 204, pw - 14, 28), "End game")
    return im


def ludo_lobby():
    """Ludo: the lobby. Each seat in its own corner, as on the board.

    Tapping a seat cycles Empty, Player, Computer. The level applies to every
    computer in the game. Start needs two seats and at least one person.
    """
    im, d = blank(); topbar(d, "Ludo")
    kinds = ["Player", "Computer", "Player", "Empty"]
    for s in range(4):
        col = 1 if s in (1, 2) else 0
        row = 1 if s >= 2 else 0
        x, y, w, h = 8 + col * 156, 38 + row * 50, 148, 44
        empty = kinds[s] == "Empty"
        d.rounded_rectangle([x, y, x + w - 1, y + h - 1], 6,
                            fill=SURFACE if empty else LUDO_SEAT[s], outline=OUTLINE)
        tx, ty = x + 20, y + h // 2
        d.ellipse([tx - 12, ty - 12, tx + 12, ty + 12], fill=LUDO_PAPER)
        _ludo_token(d, tx, ty, s, 8)
        ink = MUTED if empty else _ludo_on_seat(s)
        d.text((x + 40, y + 5), LUDO_NAMES[s], font=F2, fill=ink)
        d.text((x + 40, y + 23), kinds[s], font=F2, fill=ink)

    d.text((8, 145), "Computer", font=F2, fill=TEXT)
    button(d, (112, 138, 94, 30), "Easy", fill=TEXT, tc=BG)
    button(d, (212, 138, 94, 30), "Normal", fill=SURFACE, tc=TEXT)
    hint = "Tap a seat to change who sits there"
    d.text((8 + (304 - d.textlength(hint, font=F2)) / 2, 178), hint, font=F2, fill=MUTED)
    button(d, (60, 198, 120, 34), "Start")
    button(d, (192, 198, 120, 34), "Nearby")
    return im


def ludo_table():
    """Ludo: the table lobby, as the host sees it. Geometry from LudoLobby.cpp.

    One console has answered, one is still being asked -- invitations go out
    one at a time -- and one has not been invited. A computer fills the fourth
    seat. The tags are the four hex digits a console advertises; a console the
    owner has named would show the name instead, looked up on this device.
    """
    im, d = blank(); topbar(d, "Ludo")
    rows = [("A4F2 joined", PANEL, SUCCESS), ("Asking B1C3...", PANEL, TEXT),
            ("Invite 7E09", SURFACE, TEXT)]
    for i, (label, fill, ink) in enumerate(rows):
        button(d, (8, 36 + i * 34, 304, 30), label, fill=fill, tc=ink)
    note = "Moves travel by Bluetooth. Anyone near hears them."
    d.text((8 + (304 - d.textlength(note, font=F1)) / 2, 36 + 3 * 34 + 4), note, font=F1,
           fill=MUTED)
    button(d, (8, 172, 148, 26), "Computers: 1", fill=SURFACE)
    button(d, (164, 172, 148, 26), "Level: Easy", fill=SURFACE)
    button(d, (8, 204, 110, 30), "Back")
    button(d, (202, 204, 110, 30), "Start")
    return im


# Backgammon. Restated from src/games/BackgammonDraw.cpp -- the board origin,
# the point width, the bar and tray, the checker size and pitch, the panel.
BG_BX, BG_BY, BG_COL, BG_BAR_W = 4, 34, 17, 14
BG_BAR_X = BG_BX + 6 * BG_COL
BG_RIGHT_X = BG_BAR_X + BG_BAR_W
BG_BOARD_R = BG_RIGHT_X + 6 * BG_COL
BG_TRAY_X, BG_TRAY_W = BG_BOARD_R + 2, 18
BG_BOARD_H, BG_PT_H, BG_TRI_H, BG_R, BG_PITCH = 202, 96, 88, 7, 15
BG_PANEL_X = BG_TRAY_X + BG_TRAY_W + 4
BG_PANEL_W = W - BG_PANEL_X - 4
BG_FELT, BG_WOOD, BG_TRAY = (28, 96, 62), (96, 62, 38), (70, 45, 28)
BG_TRI = [(222, 190, 140), (160, 70, 52)]
BG_WHITE, BG_WHITE_EDGE = (246, 242, 232), (120, 110, 95)
BG_BLACK, BG_BLACK_EDGE = (44, 44, 54), (190, 190, 205)
BG_HI, BG_INK = (120, 230, 255), (26, 34, 48)


def _bg_point_rect(i):
    if i < 6:
        x = BG_RIGHT_X + (5 - i) * BG_COL
    elif i < 12:
        x = BG_BX + (11 - i) * BG_COL
    elif i < 18:
        x = BG_BX + (i - 12) * BG_COL
    else:
        x = BG_RIGHT_X + (i - 18) * BG_COL
    y = BG_BY if i >= 12 else BG_BY + BG_BOARD_H - BG_PT_H
    return x, y


def _bg_checker(d, cx, cy, white):
    fill, edge = (BG_WHITE, BG_WHITE_EDGE) if white else (BG_BLACK, BG_BLACK_EDGE)
    d.ellipse([cx - BG_R, cy - BG_R, cx + BG_R, cy + BG_R], fill=fill, outline=edge)
    d.ellipse([cx - BG_R + 3, cy - BG_R + 3, cx + BG_R - 3, cy + BG_R - 3], outline=edge)


def backgammon():
    """Backgammon against the computer: a 5-3 rolled, the checker on White's
    13-point picked up, and the two points it can reach marked.

    The position is a plausible middle game with fifteen checkers a side; the
    pip counts in the panel are computed from it, not typed.
    """
    im, d = blank(); topbar(d, "Backgammon")
    pt = [0] * 24
    for i, n in ((5, 4), (7, 3), (12, 4), (15, 1), (23, 2), (3, 1)):
        pt[i] = n
    for i, n in ((0, 2), (11, 4), (16, 3), (18, 4), (20, 2)):
        pt[i] = -n
    selected, targets = 12, {7, 9}
    d.rectangle([BG_BX - 2, BG_BY - 2, BG_BOARD_R + 1, BG_BY + BG_BOARD_H + 1], fill=BG_WOOD)
    for i in range(24):
        x, y = _bg_point_rect(i)
        top = i >= 12
        cx = x + BG_COL // 2
        d.rectangle([x, y, x + BG_COL - 1, y + BG_PT_H - 1], fill=BG_FELT)
        if top:
            d.polygon([(x, y), (x + BG_COL - 1, y), (cx, y + BG_TRI_H)], fill=BG_TRI[i & 1])
        else:
            base = y + BG_PT_H - 1
            d.polygon([(x, base), (x + BG_COL - 1, base), (cx, base - BG_TRI_H)], fill=BG_TRI[i & 1])
        n = abs(pt[i])

        def slot(s, top=top, y=y):
            return y + BG_R + 1 + s * BG_PITCH if top else y + BG_PT_H - 2 - BG_R - s * BG_PITCH
        for s in range(min(n, 5)):
            _bg_checker(d, cx, slot(s), pt[i] > 0)
        if i == selected:
            cy = slot(min(n, 5) - 1)
            d.ellipse([cx - BG_R, cy - BG_R, cx + BG_R, cy + BG_R], outline=BG_HI, width=2)
        if i in targets:
            cy = slot(min(n, 4))
            d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=BG_HI)
            d.rectangle([x, y, x + BG_COL - 1, y + BG_PT_H - 1], outline=BG_HI)
    d.rectangle([BG_BAR_X, BG_BY, BG_RIGHT_X - 1, BG_BY + BG_BOARD_H - 1], fill=BG_WOOD)
    d.rectangle([BG_TRAY_X, BG_BY, BG_TRAY_X + BG_TRAY_W - 1, BG_BY + BG_BOARD_H - 1], fill=BG_TRAY)

    px, pw = BG_PANEL_X, BG_PANEL_W
    d.ellipse([px, 38, px + 12, 50], fill=BG_WHITE, outline=BG_WHITE_EDGE)
    d.text((px + 16, 37), "You", font=F2, fill=TEXT)
    for k, face in enumerate((5, 3)):
        x0 = px + 2 + k * 36
        d.rounded_rectangle([x0, 56, x0 + 27, 83], 5, fill=(252, 252, 250), outline=BG_INK)
        cx, cy = x0 + 14, 70
        spots = {5: [(-8, -8), (8, 8), (8, -8), (-8, 8), (0, 0)], 3: [(-8, -8), (0, 0), (8, 8)]}[face]
        for ox, oy in spots:
            d.ellipse([cx + ox - 3, cy + oy - 3, cx + ox + 3, cy + oy + 3], fill=BG_INK)

    def pips(white):
        total = 0
        for i, v in enumerate(pt):
            if white and v > 0:
                total += v * (i + 1)
            if not white and v < 0:
                total += -v * (24 - i)
        return total
    d.text((px + 2, 98), "W%d B%d" % (pips(True), pips(False)), font=F1, fill=MUTED)
    d.text((px, 110), "Pick a", font=F2, fill=TEXT)
    d.text((px, 128), "point", font=F2, fill=TEXT)
    button(d, (px, 150, pw, 26), "Roll", fill=SURFACE, tc=MUTED)
    button(d, (px, 180, pw, 24), "Undo", fill=SURFACE, tc=MUTED)
    button(d, (px, 207, pw, 26), "End")
    return im


def backgammon_lobby():
    """Backgammon: three ways to play -- one console, the computer, or a
    console nearby. One nearby console is offering a game."""
    im, d = blank(); topbar(d, "Backgammon")
    rows = [("Two players", PANEL, TEXT), ("Play the computer", PANEL, TEXT),
            ("A4F2 invites you", SUCCESS, (0, 0, 0)), ("Play B1C3 nearby", SURFACE, TEXT)]
    for r, (label, fill, ink) in enumerate(rows):
        button(d, (10, 38 + r * 34, 300, 30), label, fill=fill, tc=ink)
    note = "Moves travel by Bluetooth. Anyone near hears them."
    d.text(((W - d.textlength(note, font=F1)) / 2, H - 14), note, font=F1, fill=MUTED)
    return im


def cursive():
    """Cursive: the word 'dog' part traced, with the target behind it.

    A word rather than a letter, because Trace already contributes two
    letter-tracing stills and joining up is what this game adds. The dots come
    from the real table in src/games/CursiveGlyphData.cpp, so the picture
    cannot drift from the letterforms the device draws.

    Geometry matches LetterTracer: 52px control columns, a caption row above,
    canvas at 60,52 sized 200x162, and the word set's 12px dot spacing.
    """
    import math as _m
    import re as _re
    im, d = blank(); topbar(d, "Cursive")
    for label, y, col in [["ABC", 52, PANEL], ["abc", 78, PANEL],
                          ["Words", 104, WARN]]:
        d.rounded_rectangle([4, y, 56, y + 22], 4, fill=col, outline=OUTLINE)
        d.text((30 - d.textlength(label, font=F1) / 2, y + 7), label, font=F1,
               fill=PANEL if col == WARN else TEXT)
    for label, y in [["Again", 52], ["Next", 78]]:
        d.rounded_rectangle([264, y, 316, y + 22], 4, fill=PANEL, outline=OUTLINE)
        d.text((290 - d.textlength(label, font=F1) / 2, y + 7), label, font=F1,
               fill=TEXT)
    d.rounded_rectangle([4, 142, 56, 164], 4, fill=PANEL, outline=OUTLINE)
    d.text((30 - d.textlength("Prev", font=F1) / 2, 149), "Prev", font=F1, fill=TEXT)

    word = "dog"
    d.text((160 - d.textlength(word, font=F2) / 2, 33), word, font=F2, fill=TEXT)

    src = (ROOT / "src" / "games" / "CursiveGlyphData.cpp").read_text(encoding="utf-8")

    hdr = (ROOT / "src" / "games" / "CursiveGlyphData.h").read_text(encoding="utf-8")
    cw = int(_re.search(r"CURSIVE_COORD_W = (\d+)", hdr).group(1))
    ch = int(_re.search(r"CURSIVE_COORD_H = (\d+)", hdr).group(1))
    m2p = _box_map(cw, ch)

    def stroke(tag):
        m = _re.search(r"static const int16_t %s\[\] = \{([^}]*)\}" % tag, src)
        if m is None:
            raise LookupError(tag)
        n = [int(v) for v in m.group(1).replace(" ", "").split(",") if v]
        return [m2p(n[i], n[i + 1]) for i in range(0, len(n), 2)]

    def resample(pts, step):
        out, carry = [pts[0]], 0.0
        for a, b in zip(pts, pts[1:]):
            seg = _m.dist(a, b)
            if seg <= 0:
                continue
            pos = step - carry
            while pos <= seg:
                t = pos / seg
                out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
                pos += step
            carry = seg - (pos - step)
        return out

    paths = []
    while True:
        try:
            paths.append(stroke("W_DOG_s%d" % len(paths)))
        except LookupError:
            break
    assert paths, "W_DOG strokes not found in CursiveGlyphData.cpp"

    # The finished shape, faintly: the thing the child is matching.
    for pts in paths:
        d.line(pts, fill=OUTLINE, width=1)

    way = resample(paths[0], 12)
    inked = int(len(way) * 0.45)
    for i in range(1, inked):
        d.line([way[i - 1], way[i]], fill=SUCCESS, width=3)
    for i, (x, y) in enumerate(way):
        if i < inked:
            d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=SUCCESS)
        elif i == inked:
            d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=WARN)
        else:
            d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=MUTED)
    ahead = [c for c in _corners(way) if c >= inked]
    if ahead:
        _arrow(d, way, ahead[0])
    hx, hy = way[0]
    d.ellipse([hx - 7, hy - 7, hx + 7, hy + 7], fill=WARN)
    d.text((hx - 3, hy - 4), "1", font=F1, fill=PANEL)

    for pts in paths[1:]:
        for x, y in resample(pts, 12):
            d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=MUTED)

    total = sum(len(resample(pp, 12)) for pp in paths)
    barW, barX, barY = 120, (W - 120) // 2, 220
    d.rounded_rectangle([barX, barY, barX + barW, barY + 10], 4, fill=PANEL,
                        outline=OUTLINE)
    fill = int(barW * inked / max(1, total))
    d.rounded_rectangle([barX, barY, barX + fill, barY + 10], 4, fill=SUCCESS)
    return im


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


# --- the tracing games ------------------------------------------------------
#
# Trace and Cursive share LetterTracer, so they share one mock-up chrome and
# one way of reading glyph data. Both stills draw the REAL letterform from the
# real table -- these were hand-placed points once and drifted the moment the
# layout changed, which is the whole argument for deriving them.

def _tracer_chrome(d, title, tabs, active):
    """The side columns and caption row. Keep in step with LetterTracer.cpp."""
    for i, label in enumerate(tabs):
        y = 52 + i * 26
        on = i == active
        d.rounded_rectangle([4, y, 56, y + 22], 4, fill=WARN if on else PANEL,
                            outline=OUTLINE)
        d.text((30 - d.textlength(label, font=F1) / 2, y + 7), label, font=F1,
               fill=PANEL if on else TEXT)
    for label, y in (("Again", 52), ("Next", 78)):
        d.rounded_rectangle([264, y, 316, y + 22], 4, fill=PANEL, outline=OUTLINE)
        d.text((290 - d.textlength(label, font=F1) / 2, y + 7), label, font=F1,
               fill=TEXT)
    d.rounded_rectangle([4, 142, 56, 164], 4, fill=PANEL, outline=OUTLINE)
    d.text((30 - d.textlength("Prev", font=F1) / 2, 149), "Prev", font=F1, fill=TEXT)
    d.text((160 - d.textlength(title, font=F2) / 2, 33), title, font=F2, fill=TEXT)


def _box_map(coord_w, coord_h):
    """LetterTracer's mapping: ONE scale for both axes, box letterboxed.

    Restated here rather than approximated, because getting it wrong is
    invisible on this sheet and obvious on the panel -- which is exactly what
    happened when x scaled by DRAW_W/200 and y by DRAW_H/200.
    """
    dx, dy, dw, dh = 60, 52, 200, 156
    k = min(dw / coord_w, dh / coord_h)
    ox = dx + (dw - coord_w * k) / 2
    oy = dy + (dh - coord_h * k) / 2
    return lambda x, y: (ox + x * k, oy + y * k)


def _glyph_strokes(source, tag, coord_w=200, coord_h=200):
    """Strokes of one glyph from a data table, in canvas pixels."""
    import re as _re
    src = (ROOT / "src" / "games" / source).read_text(encoding="utf-8")
    m2p = _box_map(coord_w, coord_h)
    out = []
    while True:
        m = _re.search(r"static const int16_t %s_s%d\[\] = \{([^}]*)\}"
                       % (tag, len(out)), src)
        if m is None:
            break
        n = [int(v) for v in m.group(1).replace(" ", "").split(",") if v]
        out.append([m2p(n[i], n[i + 1]) for i in range(0, len(n), 2)])
    assert out, "%s not found in %s" % (tag, source)
    return out


def _resample(pts, step):
    import math as _m
    out, carry = [pts[0]], 0.0
    for a, b in zip(pts, pts[1:]):
        seg = _m.dist(a, b)
        if seg <= 0:
            continue
        pos = step - carry
        while pos <= seg:
            t = pos / seg
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
            pos += step
        carry = seg - (pos - step)
    return out


def _corners(way, cos_t=0.70, gap=3):
    """Which waypoints LetterTracer would mark as turns. Same rule, restated."""
    import math as _m
    out, last = [], 0
    for i in range(len(way)):
        hit = (i == 0)
        if 0 < i < len(way) - 1:
            if i - last < gap:
                continue
            ax, ay = way[i][0] - way[i - 1][0], way[i][1] - way[i - 1][1]
            bx, by = way[i + 1][0] - way[i][0], way[i + 1][1] - way[i][1]
            la, lb = _m.hypot(ax, ay), _m.hypot(bx, by)
            hit = la >= 0.5 and lb >= 0.5 and (ax * bx + ay * by) / (la * lb) < cos_t
        if hit:
            out.append(i)
            last = i
    return out


def _arrow(d, way, index):
    """The direction arrow, just past a turn, pointing where the stroke goes."""
    import math as _m
    if index + 1 >= len(way):
        return
    ax, ay = way[index]
    bx, by = way[index + 1]
    dx, dy = bx - ax, by - ay
    n = _m.hypot(dx, dy)
    if n < 0.5:
        return
    ux, uy = dx / n, dy / n
    bx0, by0 = ax + ux * 3, ay + uy * 3
    d.polygon([(bx0 + ux * 11, by0 + uy * 11),
               (bx0 - uy * 4, by0 + ux * 4),
               (bx0 + uy * 4, by0 - ux * 4)], fill=WARN)


def _tracer_canvas(d, paths, spacing, fraction, badge="1"):
    """The ghost, the traced part, the pulsing next dot and the rest."""
    for pts in paths:
        d.line(pts, fill=OUTLINE, width=1)
    way = _resample(paths[0], spacing)
    inked = max(1, int(len(way) * fraction))
    for i in range(1, inked):
        d.line([way[i - 1], way[i]], fill=SUCCESS, width=3)
    for i, (x, y) in enumerate(way):
        if i < inked:
            d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=SUCCESS)
        elif i == inked:
            d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=WARN)
        else:
            d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=MUTED)
    # The arrow at the next turn at or after the finger -- one at a time.
    ahead = [c for c in _corners(way) if c >= inked]
    if ahead:
        _arrow(d, way, ahead[0])
    hx, hy = way[0]
    d.ellipse([hx - 7, hy - 7, hx + 7, hy + 7], fill=WARN)
    d.text((hx - 3, hy - 4), badge, font=F1, fill=PANEL)
    for pts in paths[1:]:
        for x, y in _resample(pts, spacing):
            d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=MUTED)
    total = sum(len(_resample(pp, spacing)) for pp in paths)
    barW, barX, barY = 120, (W - 120) // 2, 220
    d.rounded_rectangle([barX, barY, barX + barW, barY + 10], 4, fill=PANEL,
                        outline=OUTLINE)
    fill = int(barW * inked / max(1, total))
    d.rounded_rectangle([barX, barY, barX + fill, barY + 10], 4, fill=SUCCESS)


def trace():
    im, d = blank(); topbar(d, "Trace")
    _tracer_chrome(d, "A", ["ABC", "abc", "123"], 0)
    # Traced short of the apex on purpose, so the turn arrow is in shot.
    _tracer_canvas(d, _glyph_strokes("TraceGlyphData.cpp", "A"), 20, 0.30)
    return im


def trace_lower():
    im, d = blank(); topbar(d, "Trace")
    _tracer_chrome(d, "g", ["ABC", "abc", "123"], 1)
    _tracer_canvas(d, _glyph_strokes("TraceGlyphData.cpp", "g"), 20, 0.8)
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


def page_label(per_page):
    """"1/N" for a launcher mock-up showing the first page.

    N is derived from the game count, so it cannot say 5 pages while the
    console has 6. Both launcher mock-ups drew a middle page with a made-up
    pager; now that they show page 1, the label has to agree.
    """
    from app_registry_parser import playable_apps
    total = len(playable_apps())
    return "1/%d" % ((total + per_page - 1) // per_page)


def front_page_tiles(count):
    """The first `count` launcher tiles, read from the registry.

    Derived rather than typed, for the reason the About game list is derived:
    these two mock-ups are the picture of the product in the README and on the
    installer page, and they had drifted to showing page 6 (Number Line, US
    States, Shape Arith...) while page 1 was something else entirely. A picture
    of a page the launcher does not open with is worse than no picture, because
    it is the one a parent forms an impression from.
    """
    from app_registry_parser import playable_apps
    apps = sorted(playable_apps(), key=lambda a: a.index)
    return [(a.label, a.subtitle) for a in apps[:count]]


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
    batt_w = battery_width(72)
    batt_right = W - 36
    wifi_cx = batt_right - batt_w - 6 - 8
    sync_cx = wifi_cx - 8 - 6 - 6
    sync_badge(d, sync_cx, 34); wifi_badge(d, wifi_cx, 34)
    battery_badge(d, batt_right - batt_w // 2, 34)
    d.line([(W - 138, 8), (W - 138, 40)], fill=OUTLINE)
    d.ellipse([W - 30, 11, W - 5, 36], outline=TEXT)
    # Lock at the left-hand end of the badge row, inside the hairline, at badge
    # size. See LauncherLayout::lockRect().
    lock_icon(d, (W - 136, 25, 18, 18))
    tiles = front_page_tiles(6)
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
    d.text((W / 2 - 12, 217), page_label(6), font=F2, fill=TEXT)
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
    batt_w = battery_width(72)
    batt_left = bx + 40
    sync_badge(d, bx + 6, 60); wifi_badge(d, bx + 26, 60)
    battery_badge(d, batt_left + batt_w // 2, 60)
    ble_badge(d, batt_left + batt_w + 11, 60)
    d.ellipse([208, 48, 232, 72], outline=TEXT)
    lock_icon(d, (176, 51, 18, 18))
    _fills = (BLUE, GREEN, RED, BLUE)
    tiles = [(t, sub, _fills[i % 4])
             for i, (t, sub) in enumerate(front_page_tiles(4))]
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
    d.text((110, 297), page_label(4), font=F2, fill=TEXT)
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


SETTINGS_TABS = ("Device", "Power", "Sound", "Admin")


def settings_tabs(d, active_index):
    """Four tabs, each SCREEN_WIDTH/4 wide -- mirrors SettingsApp::tabRect(),
    which divides the live width rather than assuming fixed halves, and gives
    the last tab the rounding so the strip reaches the right edge."""
    each = 320 // len(SETTINGS_TABS)
    for i, lab in enumerate(SETTINGS_TABS):
        x = i * each
        w = (320 - x) if i == len(SETTINGS_TABS) - 1 else each
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
    # Four rows of 30px from y=58, under the tab baseline. Network shares a
    # clock-settings row with the NTP resync cadence.
    button(d, (8, 58, 144, 30), "Theme: Dark")
    button(d, (164, 58, 144, 30), "Menu: Tall")
    button(d, (8, 92, 144, 30), "Light: On")
    button(d, (164, 92, 144, 30), "Beacon: On")
    button(d, (8, 126, 144, 30), "Network", BLUE, WHITE)
    button(d, (164, 126, 144, 30), "Sync: 6h")
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
    button(d, (8, 160, 304, 30), "Hold to unlock: On")
    d.text((8, 196), "Saver at 60s, screen off at 120s.", font=F1, fill=MUTED)
    d.text((8, 212), "A touch lights the screen; hold to go back.",
           font=F1, fill=MUTED)
    return im


def wakelock(w=W, h=H):
    """The unlock screen the saver and panel sleep hand you, not the screen
    underneath. Geometry from BrainoApp::lockButtonRect().

    Drawn in both orientations, because the landscape still is what hid a
    real defect: the footer sentence is at its tightest on the 240px portrait
    panel, and only the wide one was ever pictured. The mock still cannot
    prove the device: PIL has its own font metrics and clips nothing, where
    TFT_eSPI drops characters at the viewport edge. It shows the layout, not
    the widths -- see LOCK_FOOTERS in AppRuntimeLock.cpp.
    """
    im = Image.new("RGB", (w, h), BG); d = ImageDraw.Draw(im)
    W, H = w, h
    bw, bh = min(200, W - 48), 58
    bx, by = (W - bw) // 2, (H - bh) // 2 + 37
    text_max = W - 16

    # Header, two rows: wordmark and battery, then the copyright, then a
    # hairline. Fixed, not drifting like the saver's -- this screen is up for
    # seconds, not hours. The copyright gets its own row because the badge is
    # variable width and all three do not fit across 240px. Mirrors the
    # HEADER_* constants in AppRuntimeLock.cpp.
    header_h, header_pad, row1_cy, row2_y = 40, 10, 14, 26
    d.text((header_pad, row1_cy - 8), PRODUCT, font=F2, fill=TEXT)
    batt_w = battery_width()
    battery_badge(d, W - header_pad - batt_w // 2, row1_cy)
    d.text((header_pad, row2_y), COPYRIGHT_SHORT, font=F1, fill=MUTED)
    d.line([(header_pad, header_h), (W - header_pad, header_h)], fill=OUTLINE)

    lock_icon(d, (W // 2 - 15, by - 82, 30, 30), MUTED, BG)
    centered_fitted(d, "Locked", W / 2, by - 44, text_max, F4, TEXT)
    centered_fitted(d, "Press and hold the button", W / 2, by - 16, text_max, F1, MUTED)
    button(d, (bx, by, bw, bh), "Hold to unlock", fill=BLUE, tc=WHITE)
    barx, bary, barh = bx, by + bh + 10, 10
    d.rounded_rectangle([barx, bary, barx + bw - 1, bary + barh - 1], 4, outline=OUTLINE)
    d.rectangle([barx + 2, bary + 2, barx + 2 + (bw - 4) * 62 // 100, bary + barh - 3],
                fill=SUCCESS)
    # The longest form that fits whole, never a truncated sentence. Mirrors
    # LOCK_FOOTERS in AppRuntimeLock.cpp.
    footer = next((s for s in ("Nothing under here can be touched yet",
                               "Nothing under here can be touched",
                               "Nothing below can be touched",
                               "Nothing below is live")
                   if d.textlength(s, font=F1) <= text_max),
                  "Nothing below is live")
    d.text((W / 2 - d.textlength(footer, font=F1) / 2, H - 16), footer,
           font=F1, fill=MUTED)
    return im


def wakelock_tall():
    """The same screen on the portrait panel, where the footer is tightest."""
    return wakelock(240, 320)


def settings_sound():
    """Settings tab: the mute switch and the volume, with the two test buttons.

    Geometry from muteRect() / volumeRect() / testCueRect() / testVoiceRect().

    Drawn mid-travel rather than at the default. The default is AT the ceiling
    (Board::AUDIO_VOLUME_MAX, 85), and a slider drawn full-width would show
    nothing about the range -- a picture of a handle at the right-hand end
    looks like 100% whatever the readout says. The ceiling is a fact for the
    prose to carry; what the picture is for is the layout.
    """
    im, d = blank(); topbar(d, "Settings")
    settings_tabs(d, 2)
    button(d, (8, 58, 304, 30), "Sound: On")
    d.text((8, 92), "Volume", font=F1, fill=MUTED)
    d.text((312 - d.textlength("70%", font=F1), 92), "70%", font=F1, fill=MUTED)
    r = (8, 104, 304, 32)
    cy = r[1] + r[3] // 2
    pad, span = 11, r[2] - 22
    fill = int(70 / 85 * span)          # value / AUDIO_VOLUME_MAX
    d.rounded_rectangle([r[0] + pad, cy - 4, r[0] + pad + span, cy + 4], 4,
                        fill=PANEL, outline=OUTLINE)
    d.rounded_rectangle([r[0] + pad, cy - 4, r[0] + pad + fill, cy + 4], 4, fill=BLUE)
    hx = r[0] + pad + fill
    d.ellipse([hx - 10, cy - 10, hx + 10, cy + 10], fill=SURFACE, outline=OUTLINE)
    d.ellipse([hx - 6, cy - 6, hx + 6, cy + 6], fill=BLUE)
    button(d, (8, 148, 144, 30), "Test sound", BLUE, WHITE)
    button(d, (164, 148, 144, 30), "Say hello", BLUE, WHITE)
    d.text((8, 190), "Volume is capped for young ears.", font=F1, fill=MUTED)
    d.text((8, 206), "Every sound is made by the device, not a file.",
           font=F1, fill=MUTED)
    return im


def settings_admin():
    """Settings tab: the PIN that guards this screen, and touch calibration.

    Geometry follows changePinRect() and recalibrateRect(). The Recalibrate
    row is drawn because this mock-up stands for the resistive boards; a
    capacitive panel has nothing to fit and the firmware puts a line of text
    there instead."""
    im, d = blank(); topbar(d, "Settings")
    settings_tabs(d, 3)
    button(d, (8, 58, 304, 30), "Change admin PIN")
    d.text((8, 100), "The PIN guards the admin profile. It", font=F1, fill=MUTED)
    d.text((8, 116), "ships as 0000 -- change it.", font=F1, fill=MUTED)
    d.text((8, 140), "Admin profile: Admin", font=F1, fill=MUTED)
    button(d, (8, 164, 304, 30), "Recalibrate touch")
    lab = "The PIN is entered twice, and must match."
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


def profiles_games():
    """Per-player game visibility: Profiles -> Edit -> Games.

    This lived in a Settings tab once and the mock-up went on depicting that
    long after Settings stopped having one -- Settings holds Device, Power and
    Admin. Visibility is per player, so it belongs with the player. Geometry
    follows ProfileApp::gameCheckRect() and gamesBackRect()."""
    im, d = blank()
    lab = "Ada"
    d.text((W / 2 - d.textlength(lab, font=F4) / 2, 8), lab, font=F4, fill=TEXT)
    # gamesBackRect() is top-right, clear of the centred name and the hint.
    button(d, (W - 72, 4, 64, 24), "Back", f=F2)
    hint = "Tap to show or hide from launcher"
    d.text((W / 2 - d.textlength(hint, font=F1) / 2, 30), hint, font=F1, fill=MUTED)

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
    """Mirrors BrainoApp::renderScreenSaver(): the wordmark still and centred,
    "Braino!" in a 60% shade of the rally colour, the battery at top centre,
    and a net that skips the stretches behind both."""
    im = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(im)
    rally = (255, 160, 60)
    mid_x, mid_y = W // 2, H // 2
    text_w = int(max(d.textlength(PRODUCT, font=F4), d.textlength(COPYRIGHT_SHORT, font=F1))) + 8
    text_y, text_h = mid_y - 26, 48
    bat_y, bat_h = 14 - 8, 16
    for y in range(0, H, 14):
        behind_text = y + 8 > text_y and y < text_y + text_h
        behind_bat = y + 8 > bat_y and y < bat_y + bat_h
        if not behind_text and not behind_bat:
            d.rectangle([mid_x - 1, y, mid_x, y + 8], fill=(40, 40, 40))
    name = tuple(c * 60 // 100 for c in rally)
    d.text((mid_x - d.textlength(PRODUCT, font=F4) / 2, mid_y - 22), PRODUCT, font=F4, fill=name)
    d.text((mid_x - d.textlength(COPYRIGHT_SHORT, font=F1) / 2, mid_y + 9), COPYRIGHT_SHORT, font=F1, fill=(70, 76, 92))
    battery_badge(d, mid_x, 14, 72)
    d.rounded_rectangle([7, 70, 13, 110], 3, fill=rally)
    d.rounded_rectangle([307, 130, 313, 170], 3, fill=rally)
    d.rounded_rectangle([230, 180, 242, 192], 2, fill=rally)
    d.rounded_rectangle([233, 183, 239, 189], 1, fill=WHITE)
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
    ("settings-sound", settings_sound, "Settings: sound and volume"),
    ("settings-admin", settings_admin, "Settings: the admin PIN"),
    ("settings-pin", settings_pin, "Settings: setting a new admin PIN"),
    ("profiles-pin", profiles_pin, "Profiles: the admin PIN prompt"),
    ("profiles-games", profiles_games, "Profiles: which games a player sees"),
    ("network-time", network_time, "Network & Time"),
    ("timezone", timezone_picker, "Time zone picker"),
    ("screensaver", screensaver, "Pong screen saver"),
    ("wakelock-tall", wakelock_tall, "Hold to unlock, Tall layout"),
    ("wakelock", wakelock, "Hold to unlock, after the saver or sleep"),
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
    """Time.

    The geometry here is taken from TimeGame::drawClock() and answerRect(),
    not approximated, and that matters more than it sounds: this mock-up used
    to draw the dial at cy=100 r=42 and omit the question label entirely, so
    the collision between the clock face and "Which time is shown?" -- which
    was real on every board and severe on the 4-inch -- was invisible in every
    generated screen. A mock-up that leaves out the element being overlapped
    cannot show an overlap.
    """
    im, d = blank(); topbar(d, "Time")
    d.text((10, 35), "Level 1", font=F2, fill=TEXT)
    d.text((10, 52), "Score 0", font=F2, fill=TEXT)
    d.text((W - 10 - d.textlength("Streak 0", font=F2), 35), "Streak 0", font=F2, fill=TEXT)
    d.text((W - 10 - d.textlength("Best 1", font=F2), 52), "Best 1", font=F2, fill=TEXT)

    import math
    cx, cy, r = W // 2, 86, 43
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=WHITE, outline=OUTLINE)
    for h in range(12):
        a = math.radians(h * 30 - 90)
        d.line([(cx + math.cos(a) * (r - 8), cy + math.sin(a) * (r - 8)),
                (cx + math.cos(a) * (r - 3), cy + math.sin(a) * (r - 3))],
               fill=(30, 30, 40))
    for label, off in (("12", (0, -32)), ("3", (32, 0)), ("6", (0, 32)), ("9", (-32, 0))):
        tw = d.textlength(label, font=F2)
        d.text((cx + off[0] - tw / 2, cy + off[1] - 8), label, font=F2, fill=(30, 30, 40))
    # 6:00 -- hour hand down, minute hand up.
    d.line([(cx, cy), (cx, cy + 22)], fill=(30, 30, 40), width=3)
    d.line([(cx, cy), (cx, cy - 32)], fill=(200, 60, 60), width=2)
    d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=(30, 30, 40))

    q = "Which time is shown?"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 136), q, font=F2, fill=TEXT)

    for i, a in enumerate(["6:00", "5:00", "2:00", "4:00"]):
        r2 = (18 + (i % 2) * 152, 152 + (i // 2) * 40, 132, 34)
        button(d, r2, a, PANEL, TEXT)
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


# MoneyGame::coinFill / coinText -- the mint colours, per denomination.
COIN_FILL = {1: (184, 96, 52), 5: (160, 170, 176), 10: (210, 218, 224),
             25: (128, 146, 166), 50: (222, 184, 82)}
COIN_OUTLINE = (8, 8, 24)


def cents_text(cents):
    """MoneyGame::centsText -- '42c' under a dollar, '$1.25' at or above."""
    return "%dc" % cents if cents < 100 else "$%d.%02d" % (cents // 100, cents % 100)


def draw_coin(d, cx, cy, value):
    """MoneyGame::drawCoin -- radius 12, denomination inside, white ink on the
    penny and the quarter."""
    d.ellipse([cx - 12, cy - 12, cx + 12, cy + 12],
              fill=COIN_FILL[value], outline=COIN_OUTLINE)
    label = cents_text(value)
    d.text((cx - d.textlength(label, font=F1) / 2, cy - F1.size / 2 - 1), label,
           font=F1, fill=WHITE if value in (1, 25) else (0, 0, 0))


def money():
    im, d = blank(); topbar(d, "Money")
    # MoneyGame::render -- the score header, then the mode name, then the coin
    # group panel, then the options. The coins live in a panel at y78..148 and
    # the buttons start at y160; the old mock-up put loose coins at y146 with
    # no panel, so they collided with the answers and lost their labels.
    d.text((8, 35), "Level 3", font=F2, fill=TEXT)
    d.text((8, 51), "Score 40", font=F1, fill=TEXT)
    streak, best = "Streak 4", "Best 7"
    d.text((W - 8 - d.textlength(streak, font=F2), 35), streak, font=F2, fill=TEXT)
    d.text((W - 8 - d.textlength(best, font=F1), 51), best, font=F1, fill=TEXT)

    q = "How much is this?"           # drawLabel(Rect{8,58,304,12}, muted, 1)
    d.text((160 - d.textlength(q, font=F1) / 2, 64 - F1.size / 2 - 1), q,
           font=F1, fill=MUTED)

    px, py, pw, ph = 18, 78, 284, 70  # drawCoinGroup(Rect{18,78,284,70})
    d.rounded_rectangle([px, py, px + pw - 1, py + ph - 1], 6,
                        fill=SURFACE, outline=OUTLINE)
    coins = [25, 10, 5, 1, 1]         # 42c, the highlighted answer
    cols = pw // 34
    for i, v in enumerate(coins):
        draw_coin(d, px + 18 + (i % cols) * 34, py + 18 + (i // cols) * 30, v)

    for i, a in enumerate(["42c", "37c", "45c", "40c"]):
        r = (18 + (i % 2) * 152, 160 + (i // 2) * 36, 132, 30)   # optionRect
        button(d, r, a,
               SUCCESS if a == "42c" else BLUE, (0, 0, 0) if a == "42c" else TEXT)
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


def maze_levels():
    """The real maze grids, read from src/games/MazeData.cpp.

    Transcribing a level by hand is how a mock-up ends up showing a maze the
    firmware has never drawn. The old version invented ten floating wall
    segments that crossed each other into plus shapes -- a shape no MazeData
    level contains, and not how the game draws walls in the first place.
    """
    text = (ROOT / "src" / "games" / "MazeData.cpp").read_text(encoding="utf-8")
    body = text[text.index("MAZES[][ROWS]"):]
    if "FALLBACK_MAZE" in body:
        body = body[:body.index("FALLBACK_MAZE")]
    rows = re.findall(r'"([#SE.]+)"', body)
    return [rows[i:i + 8] for i in range(0, len(rows) - 7, 8)]


def maze():
    im, d = blank(); topbar(d, "Maze")
    # MazeGame draws the board as filled CELLS -- white path, dark wall, green
    # exit, each cell outlined -- not as thin wall segments over a dark board.
    COLS, ROWS, CELL = 12, 8, 22
    MX, MY = (W - COLS * CELL) // 2, 30 + 28      # MAZE_X, TOP_BAR_HEIGHT + 28
    WALL_C, PATH_C = (74, 73, 74), (255, 255, 255)
    EXIT_C, PLAYER_C, GRID_C = (0, 186, 140), (237, 28, 33), (210, 210, 210)

    label = "Drag the red dot to the green exit"   # drawLabel(Rect{12,32,296,16}, 2)
    d.text((12 + 296 / 2 - d.textlength(label, font=F2) / 2, 40 - F2.size / 2 - 1),
           label, font=F2, fill=TEXT)
    # drawHud() repaints this band *after* the label, so the label's lower rows
    # are erased on the device. Mirrored here rather than quietly tidied up --
    # a mock-up that looks better than the hardware is not evidence.
    d.rectangle([0, 42, W - 1, 59], fill=BG)
    hud = "Level 4/%d  Moves 0  Best 46" % len(maze_levels())
    d.text((W / 2 - d.textlength(hud, font=F1) / 2, 50 - F1.size / 2 - 1), hud,
           font=F1, fill=TEXT)

    levels = maze_levels()
    grid = levels[3]
    for r, row in enumerate(grid):
        for c, cell in enumerate(row):
            x, y = MX + c * CELL, MY + r * CELL
            fill = WALL_C if cell == "#" else EXIT_C if cell == "E" else PATH_C
            d.rectangle([x, y, x + CELL - 1, y + CELL - 1], fill=fill, outline=GRID_C)

    for r, row in enumerate(grid):                 # drawPlayer, radius 8, at S
        if "S" in row:
            c = row.index("S")
            px, py = MX + c * CELL + CELL // 2, MY + r * CELL + CELL // 2
            d.ellipse([px - 8, py - 8, px + 8, py + 8], fill=PLAYER_C, outline=(0, 0, 0))
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
    """Mirrors SystemInfoApp: strip at TOP_BAR_HEIGHT+2, 28px tall, W/5 each."""
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


def _si_rows(d, rows, top=66, right=300):
    """RowList geometry: labels at x+6, values at max(92, w/2), 16px rows.

    `right` narrows the value column the way RowList does when it is drawing a
    scroll bar, so a mock-up of an overflowing list is not wider than the real
    one. Kind "a" is an Action chip: 22px of row holding an 18px button, drawn
    from the label column like RowList::draw() does."""
    y = top
    for kind, label, value, colour in rows:
        if kind == "s":
            d.text((6, y), label, font=F2, fill=MUTED)
            d.line([(60, y + 8), (right, y + 8)], fill=OUTLINE)
            y += 18
        elif kind == "a":
            button(d, (6, y, min(150, right - 6), 18), label, PANEL, TEXT, F1)
            y += 22
        else:
            d.text((6, y), label, font=F1, fill=MUTED)
            d.text((160, y), value, font=F1, fill=colour or TEXT)
            y += 16
    return y


def _scrollbar(d, rect, total_h, offset=0):
    """RowList::drawScrollBar geometry: 6px wide, 2px in from the right edge."""
    x, y, w, h = rect
    if total_h <= h:
        return
    track = (x + w - 6 - 2, y + 3, 6, h - 6)
    d.rounded_rectangle([track[0], track[1], track[0] + track[2], track[1] + track[3]],
                        3, fill=PANEL, outline=OUTLINE)
    thumb_h = max(18, int(track[3] * h / total_h))
    travel = max(1, track[3] - thumb_h)
    thumb_y = track[1] + int(offset * travel / max(1, total_h - h))
    d.rounded_rectangle([track[0] + 1, thumb_y + 1,
                         track[0] + track[2] - 1, thumb_y + thumb_h - 1],
                        2, fill=(88, 164, 224))


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
        ("r", "Player name", "Not Broadcast", SUCCESS),
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

    Geometry from NearbyApp: a full-width toggle at y=TOP_BAR_HEIGHT+6 with
    30px height, then the RowList below it."""
    im, d = blank(); topbar(d, "Nearby")
    button(d, (8, 36, 304, 30), "Sharing: On", SUCCESS, (12, 20, 14))
    rows = [
        ("s", "You", "", None),
        ("r", "Your tag", "A4F2", None),
        ("r", "Listening", "Yes", SUCCESS),
        # A NAMED peer: the label is the heading and the tag stays as a row,
        # because the tag is what travels and is the only way to spot a label
        # sitting on the wrong device.
        ("s", "RAVI", "", None),
        ("r", "Tag", "7C1B", MUTED),
        ("r", "Distance", "Near", MUTED),
        ("r", "Playing", "Maze", None),
        ("r", "Their best", "9 lvl", WARN),
        ("r", "Your best", "7 lvl", None),
        ("r", "", "They are ahead of you", WARN),
        ("a", "Poke 7C1B", "", None),
        ("a", "Rename 7C1B", "", None),
        # An IDLE peer, deliberately: this one is at its launcher with no game
        # open, and it still gets a Poke chip. The chip was originally added
        # after an early `continue` on this path, so a console sitting at its
        # launcher -- the state you most often want to nudge somebody out of --
        # was the one kind of peer that could not be poked. Keeping an idle
        # peer in the mock-up is what makes that regression visible again.
        ("s", "B930", "", None),
        ("r", "Distance", "Far", MUTED),
        ("r", "Playing", "Choosing a game", MUTED),
        ("a", "Poke B930", "", None),
        ("a", "Name B930", "", None),
    ]
    content = (0, 72, W, H - 72)
    # The value column narrows when a scroll bar is present, as RowList does.
    _si_rows(d, rows, top=76, right=288)
    # Two peers already overflow the panel, which is what the bar is for.
    total = 12 + sum(18 if k == "s" else 22 if k == "a" else 16 for k, _, _, _ in rows)
    _scrollbar(d, content, total)
    return im


def about_radios():
    im, d = blank(); topbar(d, "About")
    d.rounded_rectangle([10, 38, 309, 195], 6, fill=SURFACE, outline=OUTLINE)
    d.text((14, 44), "What the radios do", font=F2, fill=TEXT)
    d.text((14, 70), "Wi-Fi: clock only; sync every 6h.", font=F1, fill=MUTED)
    d.text((14, 84), "Plus one-off time-zone lookup.", font=F1, fill=MUTED)
    d.text((14, 100), "Now: connected", font=F1, fill=TEXT)
    d.text((14, 120), "Bluetooth beacon", font=F2, fill=TEXT)
    d.text((14, 146), "Now: broadcasting Braino-A4F2", font=F1, fill=WARN)
    d.text((14, 162), "Nearby on: game + best score.", font=F1, fill=WARN)
    d.text((14, 176), "No name, no profile, no location.", font=F1, fill=MUTED)
    button(d, (12, 206, 92, 28), "Prev")
    button(d, (216, 206, 92, 28), "Next")
    d.text((W / 2 - 14, 212), "7/9", font=F2, fill=MUTED)
    return im


def about_build():
    im, d = blank(); topbar(d, "About")
    d.rounded_rectangle([10, 38, 309, 195], 6, fill=SURFACE, outline=OUTLINE)
    d.text((14, 44), "This build", font=F2, fill=TEXT)
    d.text((14, 70), "Which firmware is on this device.", font=F1, fill=MUTED)
    d.text((14, 92), "Branch", font=F1, fill=TEXT)
    d.text((14, 104), "dev", font=F2, fill=MUTED)
    d.text((14, 130), "Commit", font=F1, fill=TEXT)
    d.text((14, 142), "403b280", font=F2, fill=MUTED)
    d.text((14, 168), "Built", font=F1, fill=TEXT)
    d.text((14, 182), "Aug 26 2026 14:28", font=F1, fill=MUTED)
    button(d, (12, 206, 92, 28), "Prev")
    button(d, (216, 206, 92, 28), "Next")
    d.text((W / 2 - 14, 212), "9/10", font=F2, fill=MUTED)
    return im


def about_updates():
    """About: is there a newer firmware, and where to get it.

    Drawn in the state that matters -- an update IS available -- because that
    is the state the page exists for and the one nobody sees while developing.
    The version numbers are illustrative; everything on the real page is read
    from the device and from BRAINO_UPDATE_PAGE_URL, which is compiled in.
    """
    im, d = blank(); topbar(d, "About")
    d.rounded_rectangle([10, 38, 309, 195], 6, fill=SURFACE, outline=OUTLINE)
    d.text((14, 44), "Updates", font=F2, fill=TEXT)
    d.text((14, 70), "Braino does not update itself.", font=F1, fill=MUTED)
    d.text((14, 90), "Installed", font=F1, fill=TEXT)
    d.text((14, 102), "5.7.0", font=F2, fill=MUTED)
    d.text((14, 128), "Latest available", font=F1, fill=TEXT)
    d.text((14, 140), "5.8.0", font=F2, fill=WARN)
    d.text((14, 166), "Get it from", font=F1, fill=TEXT)
    d.text((14, 180), "iamankushpandit.github.io/Gume", font=F1, fill=MUTED)
    button(d, (12, 206, 92, 28), "Prev")
    button(d, (216, 206, 92, 28), "Next", PANEL, MUTED)  # last page: disabled
    d.text((W / 2 - 14, 212), "10/10", font=F2, fill=MUTED)
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
    # "Who is playing?" and guest hint. Baselines come from ProfileApp::render:
    # promptY = 32 in landscape, and the hint sits 18px below it.
    d.text((W / 2 - d.textlength("Who is playing?", font=F2) / 2, 32), "Who is playing?", font=F2, fill=TEXT)
    d.text((W / 2 - d.textlength("Guest plays without saving scores", font=F1) / 2, 50),
           "Guest plays without saving scores", font=F1, fill=MUTED)
    # Six rows (five players + Guest) from ProfileApp's rowsTop/rowsPitch.
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


# ---------------------------------------------------------------- keypad
# Mirrors Ui::Keypad exactly: ragged QWERTY rows (10/10/9/7) centred, a
# three-key action row, and the whole block ANCHORED TO THE BOTTOM above the
# caller's reserved footer. Keep this in step with src/ui/Keypad.cpp -- a
# mock-up of a keyboard that is not the keyboard is worse than no mock-up.
KP_KEYS = ["1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM", " <>"]
KP_FOOTER_BUTTON = 34


def _keypad(d, screen_w=None, screen_h=None, reserve=KP_FOOTER_BUTTON):
    sw = screen_w if screen_w is not None else W
    sh = screen_h if screen_h is not None else H
    tall = sh > sw
    gap = 2 if tall else 4
    key_w = (sw - 16 - 9 * gap) // 10
    key_h = 36 if tall else 26
    pitch = 40 if tall else 29
    y0 = sh - reserve - (4 * pitch + key_h)
    for r, row in enumerate(KP_KEYS):
        y = y0 + r * pitch
        if r == 4:
            unit = key_w + gap
            widths = [4 * unit - gap, 3 * unit - gap, 3 * unit - gap]
            x = 8
            for c, ch in enumerate(row):
                label = {"<": "DEL", ">": "OK", " ": "SPACE"}[ch]
                fill = (150, 60, 60) if ch == "<" else GREEN if ch == ">" else PANEL
                tc = WHITE if ch in "<>" else TEXT
                button(d, (x, y, widths[c], key_h), label, fill, tc, F1)
                x += widths[c] + gap
            continue
        n = len(row)
        x0 = (sw - (n * key_w + (n - 1) * gap)) // 2
        for c, ch in enumerate(row):
            button(d, (x0 + c * (key_w + gap), y, key_w, key_h), ch, PANEL, TEXT, F2)
    return y0


def profiles_rename():
    """Name entry, reached from Add Player or from Edit -> Rename.

    Geometry follows ProfileApp: the centred title at y=8, the field at
    ((W - fieldW) / 2, 28, fieldW, 30) with fieldW = min(240, W - 40), the
    QWERTY pad from Ui::Keypad (anchored to the bottom above FOOTER_BUTTON),
    and the Cancel button from renameCancelRect()."""
    im, d = blank()
    lab = "New player"
    d.text((W / 2 - d.textlength(lab, font=F2) / 2, 8), lab, font=F2, fill=TEXT)
    field_w = min(240, W - 40)
    fx = (W - field_w) // 2
    d.rounded_rectangle([fx, 28, fx + field_w, 58], 4, fill=SURFACE, outline=OUTLINE)
    draft = "Ada"
    d.text((W / 2 - d.textlength(draft, font=F4) / 2, 34), draft, font=F4, fill=TEXT)
    # Cancel centred at the bottom of the screen (y=H-30), keyboard shifted up.
    btn_w = 52
    bx = (W - btn_w) // 2
    button(d, (bx, H - 30, btn_w, 22), "Cancel", f=F1)
    _keypad(d)
    return im


def nearby_name():
    """Naming a nearby device, reached from the Name chip in the Nearby list.

    Admin-only, and local: the label never reaches the radio. Geometry follows
    NearbyApp::renderName -- heading at y=8, the field at ((W - fieldW) / 2,
    28, fieldW, 30), Ui::Keypad below it and Cancel from nameCancelRect()."""
    im, d = blank()
    lab = "Name for A4F2"
    d.text((W / 2 - d.textlength(lab, font=F2) / 2, 8), lab, font=F2, fill=MUTED)
    field_w = min(240, W - 40)
    fx = (W - field_w) // 2
    d.rounded_rectangle([fx, 28, fx + field_w, 58], 4, fill=SURFACE, outline=OUTLINE)
    draft = "RAVI"
    d.text((W / 2 - d.textlength(draft, font=F4) / 2, 34), draft, font=F4, fill=TEXT)
    btn_w = 52
    button(d, ((W - btn_w) // 2, H - 30, btn_w, 22), "Cancel", f=F1)
    _keypad(d)
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


# ---- Elements: the periodic table ---------------------------------------
# Geometry and data mirrored from src/games/ElementsGame.cpp and the same
# table the firmware compiles, so the mock-up cannot drift from the device.
from gen_elements import ELEMENTS as ELEM_ROWS, TIER1, category as elem_category, position as elem_position  # noqa: E402

E_X0, E_COL, E_CW = 7, 17, 16
E_Y0, E_ROW, E_CH, E_GAP = 84, 15, 14, 6
E_INK = (20, 26, 36)
E_CAT_COLORS = [
    (255, 122, 92), (255, 190, 84), (166, 182, 204), (126, 206, 194),
    (192, 164, 244), (126, 220, 146), (248, 226, 112), (134, 184, 255),
    (242, 154, 202), (214, 136, 154),
]


def _e_row_y(row):
    y = E_Y0 + (row - 1) * E_ROW
    return y if row <= 7 else y + E_GAP


def _e_cell(col, row):
    return E_X0 + (col - 1) * E_COL, _e_row_y(row)


def _e_tabs(d, active):
    for i, lab in enumerate(["Explore", "Quiz", "Level"]):
        third = W // 3
        x = i * third
        wid = (W - 2 * third) if i == 2 else third
        on = (i == active)
        top, h = (30, 22) if on else (34, 18)
        fill = SURFACE if on else PANEL
        d.rounded_rectangle([x, top, x + wid - 1, top + h], 4, fill=fill, outline=OUTLINE)
        d.text((x + wid / 2 - d.textlength(lab, font=F2) / 2, top + h / 2 - 6), lab,
               font=F2, fill=TEXT if on else MUTED)
    d.line([(0, 52), (319, 52)], fill=OUTLINE)


def _e_strip(d, text, ink=TEXT):
    d.rounded_rectangle([6, 56, W - 7, 82], 5, fill=SURFACE, outline=OUTLINE)
    font = F2 if d.textlength(text, font=F2) <= W - 24 else F1
    d.text((W / 2 - d.textlength(text, font=font) / 2, 69 - (7 if font is F2 else 5)),
           text, font=font, fill=ink)


def _e_table(d, selected=None, pending=None, correct=None, wrong=None):
    """The 118-cell chart. Anything above the Easy level is drawn dimmed, which
    is what the device does at the default Auto level on a fresh profile."""
    for i, (sym, name, _fact) in enumerate(ELEM_ROWS):
        z = i + 1
        col, row = elem_position(z)
        x, y = _e_cell(col, row)
        base = E_CAT_COLORS[elem_category(z, sym)]
        fill, ink = base, E_INK
        if sym not in TIER1:
            ink = shade(base, 155)
            fill = shade(base, 38)
        if sym == correct:
            fill, ink = SUCCESS, E_INK
        elif sym == wrong:
            fill, ink = ERROR, WHITE
        d.rectangle([x, y, x + E_CW - 1, y + E_CH - 1], fill=fill)
        if sym in (selected, pending):
            edge = WARN if sym == pending else TEXT
            d.rectangle([x - 1, y - 1, x + E_CW, y + E_CH], outline=edge, width=2)
        d.text((x + E_CW / 2 - d.textlength(sym, font=F1) / 2, y + E_CH / 2 - 5),
               sym, font=F1, fill=ink)
    for row in (6, 7):
        x, y = _e_cell(3, row)
        d.text((x + E_CW / 2 - 2, y + 2), "*", font=F1, fill=MUTED)


def elements():
    """Elements, Explore tab: the whole chart with Oxygen picked out."""
    im, d = blank(); topbar(d, "Elements")
    _e_tabs(d, 0)
    _e_strip(d, "O  Oxygen  8  -  tap here")
    _e_table(d, selected="O")
    return im


def elements_card():
    """Elements: one element opened up, and the way back into the quiz."""
    im, d = blank(); topbar(d, "Elements")
    fill = E_CAT_COLORS[elem_category(8, "O")]
    d.rounded_rectangle([12, 44, 87, 119], 6, fill=fill)
    d.text((50 - d.textlength("O", font=F4) / 2, 84 - 10), "O", font=F4, fill=E_INK)
    d.text((17, 48), "8", font=F2, fill=E_INK)
    d.text((100, 48), "Oxygen", font=F4, fill=TEXT)
    d.text((100, 78), "Nonmetal", font=F2, fill=MUTED)
    d.text((100, 98), "Gas  -  8 protons", font=F2, fill=MUTED)
    d.text((14, 132), "The part of air that keeps you", font=F2, fill=TEXT)
    d.text((14, 152), "alive.", font=F2, fill=TEXT)
    half = (W - 30) // 2
    d.rounded_rectangle([10, 198, 10 + half, 232], 6, fill=GREEN, outline=OUTLINE)
    d.text((10 + half / 2 - d.textlength("Quiz me", font=F2) / 2, 208), "Quiz me",
           font=F2, fill=WHITE)
    d.rounded_rectangle([W - 10 - half, 198, W - 10, 232], 6, fill=PANEL, outline=OUTLINE)
    d.text((W - 10 - half / 2 - d.textlength("Back", font=F2) / 2, 208), "Back",
           font=F2, fill=TEXT)
    return im


def elements_quiz():
    """Elements, Quiz tab: the one question shape answered on the chart."""
    im, d = blank(); topbar(d, "Elements")
    _e_tabs(d, 1)
    _e_strip(d, "Find Calcium in the table")
    _e_table(d, pending="Ca")
    hint = "tap it again to answer"
    d.text((W / 2 - d.textlength(hint, font=F1) / 2, 228), hint, font=F1, fill=WARN)
    return im


# ------------------------------------------------- portrait system screens
#
# CLAUDE.md requires every system app to work in BOTH orientations, and until
# now this generator rendered exactly one portrait screen -- launcher-tall --
# out of 73. Six of the seven system apps had no portrait mock-up at all, so
# "check the mock-ups in portrait" was not a thing anyone could do, and a
# portrait fault could only be found by flashing a board and looking at it.
#
# Each function below restates its screen's OWN layout arithmetic at 240x320,
# from the same constants the firmware uses, rather than re-imagining the
# screen at a new size. That is the whole value: where the firmware's numbers
# do not survive a 240px-wide panel, the mock-up shows it going wrong instead
# of quietly drawing something that looks fine.
#
# Two different layout strategies are being mirrored, and the difference is
# the point:
#
#   - Settings, About, Scores, System Info and Profiles read tft.width() /
#     tft.height() at render time and lay out against them. PW/PH below are
#     simply fed into their formulas.
#   - Wi-Fi does not. It maps a fixed 320x240 design onto the panel with
#     baseX()/baseY(), scaling each axis independently while text and badges
#     stay their authored pixel size -- so pw()/ph() here reproduce that
#     transform exactly, stretch and all.

PW, PH = 240, 320


def blank_tall():
    return blank(PW, PH)


def pw(x):
    """WifiApp::baseX -- the design's 320 mapped onto the panel's width."""
    return int(x * PW / 320)


def ph(y):
    """WifiApp::baseY -- the design's 240 mapped onto the panel's height.
    Independent of pw() on purpose; that independence is the thing that pulls
    a fixed-size glyph away from the row it belongs to."""
    return int(y * PH / 240)


def prect(x, y, w, h):
    """WifiApp::baseRect."""
    return (pw(x), ph(y), pw(w), ph(h))


def wifi_tall():
    """Wi-Fi (Network & Time) in portrait -- WifiApp::renderIdle through
    baseX/baseY at 240x320.

    The Wi-Fi badge is placed off the MEASURED width of the SSID, which is the
    fix that goes with this commit. It used to be pinned at baseX(296), which
    left it adrift against the right-hand edge with the whole row empty
    between it and the network it describes -- visible in the landscape
    network-time.png too, and reported off the panel as a speaker icon
    hanging in air."""
    im, d = blank_tall()
    topbar(d, "Wi-Fi", w=PW)
    d.text((pw(14), ph(36)), "WI-FI", font=F1, fill=MUTED)
    d.line([(pw(52), ph(41)), (pw(306), ph(41))], fill=OUTLINE)
    ssid = "DextersLab"
    d.text((pw(14), ph(48)), ssid, font=F2, fill=TEXT)
    # Measured, clamped at the right so a long SSID cannot push it off-panel.
    badge_cx = min(int(pw(14) + d.textlength(ssid, font=F2) + 14), PW - 8 - 7)
    wifi_badge(d, badge_cx, ph(48) + 8, 3)
    button(d, prect(14, 64, 140, 30), "Scan Wi-Fi", BLUE, WHITE)
    button(d, prect(166, 64, 140, 30), "Forget")
    d.text((pw(14), ph(102)), "TIME", font=F1, fill=MUTED)
    d.line([(pw(48), ph(109)), (pw(306), ph(109))], fill=OUTLINE)
    # The date drops off this row on a 240px panel: TFT_eSPI stops drawing at
    # the viewport edge, mid-word and unmarked. Shown here as the device shows
    # it rather than wrapped, because wrapping is not what the firmware does.
    stamp = "Tue Aug 11 2026  12:41 AM"
    d.text((pw(14), ph(114)), stamp, font=F2, fill=TEXT)
    sync_badge(d, int(pw(14) + d.textlength(stamp, font=F2) + 12), ph(122), True)
    button(d, prect(14, 132, 140, 30), "Auto time: On", GREEN, WHITE)
    button(d, prect(166, 132, 140, 30), "US Central")
    button(d, prect(14, 172, 140, 30), "Sync now", GREEN, WHITE)
    button(d, prect(166, 172, 140, 30), "Back")
    hint = "Tap the zone to change it"
    d.text((PW / 2 - d.textlength(hint, font=F1) / 2, ph(210)), hint, font=F1, fill=MUTED)
    return im


def settings_tall():
    """Settings (Device tab) in portrait. Mirrors SettingsApp's shared grid:
    pitch = (panelH - 36 - 8 - 58) / 4 floored at 34, row height capped at 44,
    columns (panelW - 8 - 12 - 12) / 2. At 240x320 that is a 54px pitch and
    104px columns, so the controls spread down the taller panel rather than
    staying in a landscape-sized block at the top."""
    im, d = blank_tall()
    topbar(d, "Settings", w=PW)
    each = PW // len(SETTINGS_TABS)
    for i, lab in enumerate(SETTINGS_TABS):
        x = i * each
        bw = (PW - x) if i == len(SETTINGS_TABS) - 1 else each
        active = (i == 0)
        top, h = (30, 22) if active else (34, 18)
        d.rounded_rectangle([x, top, x + bw - 1, top + h], 4,
                            fill=SURFACE if active else PANEL, outline=OUTLINE)
        # Four tabs across 240px give each 60px, so the labels are fitted the
        # way Ui::drawTab fits them instead of overrunning their neighbours.
        s = lab
        while s and d.textlength(s, font=F2) > bw - 8:
            s = s[:-1]
        d.text((x + bw / 2 - d.textlength(s, font=F2) / 2, top + h / 2 - 6), s,
               font=F2, fill=TEXT if active else MUTED)
    d.line([(0, 52), (PW - 1, 52)], fill=OUTLINE)

    pitch = max(34, (PH - 36 - 8 - 58) // 4)
    row_h = min(44, pitch - 4)
    col_w = (PW - 8 - 12 - 12) // 2
    cols = (8, 8 + col_w + 12)
    grid = [("Theme: Dark", PANEL, TEXT), ("Menu: Tall", PANEL, TEXT),
            ("Light: On", PANEL, TEXT), ("Beacon: On", PANEL, TEXT),
            ("Network", BLUE, WHITE), ("Sync: 6h", PANEL, TEXT),
            ("Nearby: On", PANEL, TEXT), ("Reset device", (120, 58, 58), WHITE)]
    for i, (label, fill, tc) in enumerate(grid):
        x = cols[i % 2]
        y = 58 + (i // 2) * pitch
        button(d, (x, y, col_w, row_h), label, fill, tc)

    # brightRect(): the bar is pinned SETTINGS_BOTTOM_RESERVE off the bottom.
    br = (8, PH - 36, PW - 16, 32)
    d.text((8, br[1] - 12), "Brightness", font=F1, fill=MUTED)
    d.text((PW - 8 - d.textlength("80%", font=F1), br[1] - 12), "80%", font=F1, fill=MUTED)
    cy = br[1] + br[3] // 2
    pad, span = 11, br[2] - 22
    fill_w = int((80 - 25) / 75 * span)
    d.rounded_rectangle([br[0] + pad, cy - 4, br[0] + pad + span, cy + 4], 4,
                        fill=PANEL, outline=OUTLINE)
    d.rounded_rectangle([br[0] + pad, cy - 4, br[0] + pad + fill_w, cy + 4], 4, fill=BLUE)
    hx = br[0] + pad + fill_w
    d.ellipse([hx - 10, cy - 10, hx + 10, cy + 10], fill=SURFACE, outline=OUTLINE)
    d.ellipse([hx - 6, cy - 6, hx + 6, cy + 6], fill=BLUE)
    return im


def scores_tall():
    """Scores (Mine tab) in portrait -- ScoresApp::rowRect's own arithmetic:
    rows fill from 56 to screenH - 32, pitch floored at 30, width screenW - 16.

    The best and worst columns do NOT move with the rows: they are drawn right-
    aligned at a hard-coded x = 244 and x = 306 (ScoresApp::render), which on a
    240px-wide panel are both off the edge of the screen. TFT_eSPI silently
    stops drawing there, so in portrait every score is invisible and the rows
    carry a game name and nothing else. That is drawn faithfully here -- the
    empty right-hand side of these rows is the bug, not a gap in the mock-up."""
    im, d = blank_tall()
    topbar(d, "Scores", w=PW)
    for lab, r, active in (("Mine", (8, 34, 64, 18), True),
                           ("Device", (80, 34, 80, 18), False)):
        d.rounded_rectangle([r[0], r[1], r[0] + r[2], r[1] + r[3]], 4,
                            fill=SURFACE if active else PANEL, outline=OUTLINE)
        d.text((r[0] + r[2] / 2 - d.textlength(lab, font=F2) / 2, r[1] + 3), lab,
               font=F2, fill=TEXT if active else MUTED)
    d.line([(8, 52), (160, 52)], fill=OUTLINE)
    d.text((8, 58), "Ava", font=F2, fill=TEXT)

    pitch = max(30, ((PH - 32) - 56 - 4) // 5)
    rows = [("Memory Match", "18 moves"), ("Math", "24 pts"), ("Multiplication", "31 pts"),
            ("Whack A Mole", "27 pts"), ("Microku", "1:42")]
    # Column headings, at the same off-panel x the values use.
    d.text((244, 64), "best", font=F1, fill=MUTED)
    d.text((306, 64), "worst", font=F1, fill=MUTED)
    for i, (name, value) in enumerate(rows):
        y = 56 + i * pitch
        h = pitch - 2
        d.rounded_rectangle([8, y, PW - 9, y + h], 4, fill=SURFACE, outline=OUTLINE)
        d.text((16, y + h / 2 - 7), name, font=F2, fill=TEXT)
        # x = 244 and 306, right-aligned, exactly as the firmware draws them.
        d.text((244 - d.textlength(value, font=F2), y + h / 2 - 7), value,
               font=F2, fill=SUCCESS)
    footer_y = PH - 32
    button(d, (8, footer_y, int(PW * 0.275), 26), "Prev")
    sw = int(PW * 0.35)
    button(d, ((PW - sw) // 2, footer_y, sw, 26), "Switch")
    button(d, (PW - 8 - int(PW * 0.275), footer_y, int(PW * 0.275), 26), "Next")
    return im


def about_tall():
    """About (intro page) in portrait -- AboutApp::panelRect, which is
    PANEL_TOP=38 down to FOOTER_H=44 off the bottom, with every line fitted to
    width - 28. The page's line baselines are fixed (48..190), so on a 320px-
    tall panel the text keeps its landscape spacing and the panel simply has
    more empty room beneath it."""
    im, d = blank_tall()
    topbar(d, "About", w=PW)
    panel = (10, 38, PW - 20, PH - 38 - 44)
    d.rounded_rectangle([panel[0], panel[1], panel[0] + panel[2], panel[1] + panel[3]],
                        6, fill=SURFACE, outline=OUTLINE)

    def line(y, text, f=F2, fill=TEXT):
        s = text
        while s and d.textlength(s, font=f) > PW - 28:
            s = s[:-1]
        if s != text and len(s) > 1:
            s = s[:-1] + "."
        d.text((14, y), s, font=f, fill=fill)

    line(48, PRODUCT)
    line(70, "(C) GoodTime Micro Company", F1, MUTED)
    line(84, "Educational games for the E32R28T-1.", F1, MUTED)
    line(98, "Copyright 2026.", F1, MUTED)
    line(116, "Version 5.11.0-SNAPSHOT")
    line(140, "37 games built in")
    line(162, "195 flags and 50 US states,", F1, MUTED)
    line(176, "all stored on the device.", F1, MUTED)
    line(190, "Up to 5 players, plus a Guest.", F1, MUTED)
    button(d, (12, PH - 34, 92, 28), "Prev", SURFACE, MUTED)
    button(d, (PW - 104, PH - 34, 92, 28), "Next")
    d.text((PW / 2 - d.textlength("1/9", font=F2) / 2, PH - 28), "1/9", font=F2, fill=MUTED)
    return im


def systeminfo_tall():
    """System Info (Board tab) in portrait. SystemInfoApp::tabRect divides the
    live width by TAB_COUNT=5, so on 240px each tab is 48px -- the labels are
    fitted here because that is what the strip has room for, and the content
    below starts at TOP_BAR_HEIGHT + 2 + TAB_STRIP_H."""
    im, d = blank_tall()
    topbar(d, "System Info", w=PW)
    labels = ["Board", "Memory", "Network", "BLE", "App"]
    strip_y, strip_h = 32, 28
    each = PW // len(labels)
    for i, lab in enumerate(labels):
        x = i * each
        bw = (PW - x) if i == len(labels) - 1 else each
        active = (i == 0)
        d.rounded_rectangle([x, strip_y, x + bw - 1, strip_y + strip_h - 1], 4,
                            fill=SURFACE if active else PANEL, outline=OUTLINE)
        s = lab
        while s and d.textlength(s, font=F1) > bw - 6:
            s = s[:-1]
        d.text((x + bw / 2 - d.textlength(s, font=F1) / 2, strip_y + strip_h / 2 - 5), s,
               font=F1, fill=TEXT if active else MUTED)
    top = strip_y + strip_h
    d.line([(0, top), (PW - 1, top)], fill=OUTLINE)
    rows = [("Board", "E32R28T-1"), ("Panel", "ILI9341 320x240"), ("Touch", "XPT2046"),
            ("Device", "R28T-9F3A2C71"), ("Speaker", "GPIO26 (DAC2)"),
            ("RGB LED", "22 / 16 / 17"), ("Battery", "GPIO34, 2.0:1"),
            ("SD slot", "not wired"), ("Flash", "4 MB")]
    for i, (k, v) in enumerate(rows):
        y = top + 8 + i * 22
        d.text((10, y), k, font=F2, fill=MUTED)
        # Values right-aligned to the panel edge, which is how RowList lays a
        # value column out -- against the live width, not a landscape constant.
        d.text((PW - 10 - d.textlength(v, font=F2), y), v, font=F2, fill=TEXT)
    return im


def profiles_tall():
    """Profiles (the picker) in portrait. This screen already branches on
    screenH > screenW -- rowsTop 62, pitch 38, row height 33, menu column 54 --
    so the portrait mock-up is the tall branch of its own geometry, with six
    rows (five players and the Guest) between the prompt and the buttons."""
    im, d = blank_tall()
    d.rectangle([0, 0, PW - 1, 29], fill=SURFACE)
    d.text((10, 6), PRODUCT, font=F4, fill=TEXT)
    d.text((PW - 8 - d.textlength(COPYRIGHT_SHORT, font=F1), 13), COPYRIGHT_SHORT,
           font=F1, fill=MUTED)
    prompt = "Who is playing?"
    d.text((PW / 2 - d.textlength(prompt, font=F2) / 2, 32), prompt, font=F2, fill=TEXT)

    menu_w, top, pitch, row_h = 54, 62, 38, 33
    people = [("Ava", True), ("Ben", False), ("Cora", False),
              ("Dev", False), ("Admin", False), ("Guest", False)]
    for i, (name, active) in enumerate(people):
        y = top + i * pitch
        d.rounded_rectangle([8, y, 8 + (PW - 22 - menu_w) - 1, y + row_h], 4,
                            fill=BLUE if active else SURFACE, outline=OUTLINE)
        d.text((18, y + row_h / 2 - 7), name, font=F2, fill=WHITE if active else TEXT)
        d.rounded_rectangle([PW - 8 - menu_w, y, PW - 9, y + row_h], 4,
                            fill=PANEL, outline=OUTLINE)
        d.text((PW - 8 - menu_w + menu_w / 2 - d.textlength("Edit", font=F1) / 2,
                y + row_h / 2 - 5), "Edit", font=F1, fill=MUTED)
    bw = (PW - 24) // 2
    button(d, (8, PH - 30, bw, 26), "Add")
    button(d, (16 + bw, PH - 30, bw, 26), "Done", GREEN, WHITE)
    return im


PORTRAIT_SCREENS = [
    ("wifi-tall", wifi_tall, "Network & Time, Tall layout"),
    ("settings-tall", settings_tall, "Settings: device, Tall layout"),
    ("scores-tall", scores_tall, "Scores: this player, Tall layout"),
    ("about-tall", about_tall, "About: the intro page, Tall layout"),
    ("systeminfo-tall", systeminfo_tall, "System Info: board, Tall layout"),
    ("profiles-tall", profiles_tall, "Profiles: who is playing, Tall layout"),
]


EXTRA_SCREENS = [
    ("scores-mine", scores_mine, "Scores: this player"),
    ("scores-device", scores_device, "Scores: device best"),
    ("profiles", profiles_pick, "Profiles: who is playing"),
    ("profiles-rename", profiles_rename, "Profiles: the name entry for Add / Rename"),
    ("nearby", nearby, "Nearby: who else is playing"),
    ("nearby-name", nearby_name, "Nearby: naming a device, locally"),
    ("systeminfo-ble", systeminfo_ble, "System Info: what BLE is broadcasting"),
    ("systeminfo-memory", systeminfo_memory, "System Info: heap and CPU"),
    ("about-radios", about_radios, "About: what the radios do"),
    ("about-build", about_build, "About: which build is on the device"),
    ("about-updates", about_updates, "About: whether a newer firmware exists"),
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
    ("elements", elements, "Elements: the periodic table"),
    ("elements-card", elements_card, "Elements: one element up close"),
    ("elements-quiz", elements_quiz, "Elements: find it in the table"),
    ("piano", piano, "Piano: one octave"),
    ("chess", chess, "Chess: legal moves ringed, captures beside the board"),
    ("seabattle", sea_battle, "Sea Battle: hunting the fleet, your sea beside it"),
    ("cursive", cursive, "Cursive: joined-up letters and easy words"),
    ("ludo", ludo, "Ludo: a choice to make, the die beside the board"),
    ("ludo-lobby", ludo_lobby, "Ludo: who sits in each seat"),
    ("ludo-table", ludo_table, "Ludo: inviting consoles in the room"),
    ("backgammon", backgammon, "Backgammon: a checker picked up, where it can go"),
    ("backgammon-lobby", backgammon_lobby, "Backgammon: one console, the computer, or nearby"),
]
SCREENS.extend(EXTRA_SCREENS)
SCREENS.extend(PORTRAIT_SCREENS)


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
