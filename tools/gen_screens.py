"""
Render preview images of the UI for the README.

    python tools/gen_screens.py

These are MOCK-UPS, not device photos: they redraw each view in Python using
the exact rectangles and fonts from the C++ source, so they show real layout
and real artwork but are generated on the host. The CYD's display cannot be
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


def geo_country():
    im, d = blank(); topbar(d, "Guess the Country")
    d.text((8, 32), "2/4", font=F2, fill=TEXT)
    button(d, (132, 31, 56, 16), "Easy", f=F1)
    d.rectangle([110, 46, 209, 169], fill=WHITE, outline=OUTLINE)
    m = tinted("mnf_map_it", INK)
    if m: im.paste(m, (110 + (100 - m.width) // 2, 46 + (124 - m.height) // 2), m)
    q = "Which country is this?"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 169), q, font=F2, fill=TEXT)
    for i, lab in enumerate(["Italy", "Greece", "Spain", "Turkey"]):
        button(d, (6 + (i % 2) * 158, 186 + (i // 2) * 27, 150, 25), lab)
    return im


def geo_continent():
    im, d = blank(); topbar(d, "Guess the Country")
    d.text((8, 32), "5/7", font=F2, fill=TEXT)
    button(d, (132, 31, 56, 16), "Hard", f=F1)
    d.text((W - 8 - d.textlength("Kenya", font=F2), 32), "Kenya", font=F2, fill=MUTED)
    d.rectangle([110, 46, 209, 169], fill=WHITE, outline=OUTLINE)
    m = tinted("mnf_map_ke", INK)
    if m: im.paste(m, (110 + (100 - m.width) // 2, 46 + (124 - m.height) // 2), m)
    q = "Which continent is it in?"
    d.text((W / 2 - d.textlength(q, font=F2) / 2, 169), q, font=F2, fill=TEXT)
    for i, lab in enumerate(["Africa", "Asia", "Europe", "N. America", "S. America", "Oceania"]):
        button(d, (6 + (i % 3) * 104, 186 + (i // 3) * 27, 100, 25), lab,
               SUCCESS if i == 0 else PANEL, (0, 0, 0) if i == 0 else TEXT)
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


def simon():
    im = Image.new("RGB", (W, H), (245, 245, 248)); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W - 1, 29], fill=SURFACE)
    d.text((48, 8), "Simon Says", font=F2, fill=TEXT)
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
    im, d = blank(); d.rectangle([0, 0, W - 1, 43], fill=SURFACE)
    d.line([(0, 0), (W, 0)], fill=shade(SURFACE, 145))
    d.line([(0, 43), (W, 43)], fill=shade(SURFACE, 60))
    d.text((10, 6), "GoodTime Kids!", font=F4, fill=TEXT)
    d.text((10, 30), "(C) GoodTime Micro", font=F1, fill=MUTED)
    t = "12:41 AM"
    d.text((W - 68 - d.textlength(t, font=F2), 5), t, font=F2, fill=TEXT)
    sync_badge(d, W - 58, 11); wifi_badge(d, W - 36, 11)
    tiles = [("Number Line", "jump to number"), ("Countries", "maps & continents"),
             ("Flags", "guess the flag"), ("Shape Arith", "add & subtract"),
             ("Fingers", "count on hands"), ("Calendar", "days & months")]
    cols = [BLUE, GREEN, RED]
    for slot, (title, sub) in enumerate(tiles):
        x, y = 10 + (slot % 2) * 155, 48 + (slot // 2) * 56
        fill = cols[slot % 3]
        d.rounded_rectangle([x + 2, y + 3, x + 146, y + 50], 6, fill=SHADOW)
        d.rounded_rectangle([x, y, x + 144, y + 47], 6, fill=fill)
        d.line([(x + 4, y + 1), (x + 140, y + 1)], fill=shade(fill, 138))
        d.ellipse([x + 10, y + 8, x + 38, y + 36], fill=(120, 200, 255), outline=WHITE)
        d.text((x + 48, y + 10), title, font=F2, fill=WHITE)
        d.text((x + 48, y + 30), sub, font=F1, fill=(235, 245, 255))
    button(d, (8, 212, 74, 24), "Prev"); button(d, (W - 82, 212, 74, 24), "Next")
    d.text((W / 2 - 12, 217), "3/5", font=F2, fill=TEXT)
    return im


def launcher_tall():
    im = Image.new("RGB", (240, 320), BG); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 239, 77], fill=SURFACE)
    t = "GoodTime Kids!"
    d.text((120 - d.textlength(t, font=F4) / 2, 6), t, font=F4, fill=TEXT)
    d.line([(8, 30), (232, 30)], fill=shade(SURFACE, 150))
    d.text((8, 36), "(C) GoodTime Micro", font=F1, fill=MUTED)
    d.text((8, 53), "12:41 AM", font=F2, fill=TEXT)
    d.ellipse([208, 48, 232, 72], outline=TEXT)
    tiles = [("Number Line", "jump to number", BLUE), ("Countries", "maps & continents", GREEN),
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


def settings_device():
    im, d = blank(); topbar(d, "Settings")
    button(d, (4, 32, 150, 28), "Device / Wi-Fi", BLUE, WHITE)
    button(d, (162, 32, 150, 28), "Games")
    button(d, (8, 70, 144, 40), "Theme: Dark")
    button(d, (164, 70, 144, 40), "Menu: Tall")
    button(d, (8, 120, 144, 40), "Saver: 60s")
    button(d, (164, 120, 144, 40), "Light: On")
    button(d, (8, 170, 304, 40), "Network & Time", BLUE, WHITE)
    d.text((8, 222), "Menu layout applies to the home screen only", font=F1, fill=MUTED)
    return im


def settings_games():
    im, d = blank(); topbar(d, "Settings")
    button(d, (4, 32, 150, 28), "Device / Wi-Fi")
    button(d, (162, 32, 150, 28), "Games", BLUE, WHITE)
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
    ("countries-outline", geo_country, "Countries: name the outline"),
    ("countries-continent", geo_continent, "Countries: which continent"),
    ("fingers-count", fingers_count, "Finger Counting: count them"),
    ("fingers-show", fingers_show, "Finger Counting: show me N"),
    ("simon", simon, "Simon Says"),
    ("settings-device", settings_device, "Settings: device"),
    ("settings-games", settings_games, "Settings: which games appear"),
    ("network-time", network_time, "Network & Time"),
    ("timezone", timezone_picker, "Time zone picker"),
    ("screensaver", screensaver, "Pong screen saver"),
]


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
