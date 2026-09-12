#!/usr/bin/env python3
"""Check every theme's palette for readable contrast, and say where it fails.

WHY THIS EXISTS

Nine palettes were chosen by eye, and by eye is how a pairing like grey-on-grey
survives: each colour looks right on its own and the combination is never
measured. The console is read by children, through a resistive overlay that
diffuses everything behind it, so a pairing that is merely "fine" on a monitor
is not fine here.

So the palette is measured, with the same arithmetic the web uses -- WCAG 2
relative luminance and contrast ratio. It is not a perfect model of a diffused
TFT, but it is a floor, it is objective, and it catches the failures that
matter: text that disappears into what it sits on.

THE THRESHOLDS, AND WHY THEY ARE NOT ALL 4.5

  TEXT    4.5:1  body text, the WCAG AA figure for normal-size type
  LARGE   3.0:1  text drawn at font 4 and bigger -- titles, the clock
  GRAPHIC 3.0:1  something you have to SEE but not read: badges, bars, icons
  HAIRLINE 1.6:1 an outline. Its job is to separate two areas that are already
                 distinct; holding it to 3:1 would forbid a subtle border,
                 which is a real design, and the panel shows a 1.6 edge.

Run it after touching PALETTES in src/ui/Ui.cpp, and read the failures rather
than tuning until the number passes: "muted is 3.9 on surface" usually means
the theme wants a different muted, not a nudged one.
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI = os.path.join(ROOT, "src", "ui", "Ui.cpp")

TEXT = 4.5
LARGE = 3.0
GRAPHIC = 3.0
HAIRLINE = 1.6
# Ui::onFillSoft()'s mix, restated. Change it in both or the check lies.
SOFT_MIX = 88


def rgb565_to_rgb(value):
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    # Replicate the high bits into the low ones, which is what the panel does.
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def luminance(rgb):
    def channel(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (channel(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(a, b):
    la, lb = luminance(a), luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def on_fill(rgb):
    """Ui::onFill(): black or white, whichever can be read on this fill."""
    return (0, 0, 0) if luminance(rgb) > 0.179 else (255, 255, 255)


def on_fill_soft(rgb):
    """Ui::onFillSoft(): SOFT_MIX of the way towards that ink -- unless the
    fill is too mid-tone to allow it, in which case the full ink."""
    ink = on_fill(rgb)
    soft = tuple(int(f + (i - f) * SOFT_MIX / 100) for f, i in zip(rgb, ink))
    return soft if ratio(soft, rgb) >= TEXT else ink


def parse_palettes():
    """The PALETTES table, as dicts of role -> RGB565, in enum order."""
    src = io.open(UI, encoding="utf-8").read()
    consts = {m.group(1): int(m.group(2), 16) for m in
              re.finditer(r"constexpr uint16_t (\w+)\s*=\s*(0x[0-9A-Fa-f]+)", src)}
    tiles = re.search(r"#define TILES_RGB \{([^}]*)\}", src)
    shared_tiles = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", tiles.group(1))]

    block = re.search(r"constexpr Palette PALETTES\[[^\]]*\] = \{(.*?)\n\};", src, re.S)
    if not block:
        raise SystemExit("could not find PALETTES in src/ui/Ui.cpp")
    body = re.sub(r"/\*.*?\*/", "", block.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)

    names = theme_names(src)
    rows, depth, cur = [], 0, ""
    for ch in body:
        if ch == "{":
            depth += 1
            if depth == 1:
                cur = ""
                continue
        if ch == "}":
            depth -= 1
            if depth == 0:
                rows.append(cur)
                continue
        if depth >= 1:
            cur += ch

    ROLES = ["bg", "bar", "barText", "surface", "panel", "text", "muted",
             "outline", "success", "error", "warning"]
    out = []
    for i, row in enumerate(rows):
        tokens = [t.strip() for t in re.split(r",(?![^{]*\})", row) if t.strip()]
        flat = []
        for t in tokens:
            if t.startswith("{"):
                flat.append([int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", t)])
            elif t.startswith("TILES_RGB"):
                flat.append(list(shared_tiles))
            elif t.startswith("0x"):
                flat.append(int(t, 16))
            elif t in consts:
                flat.append(consts[t])
            elif t.isdigit():
                flat.append(int(t))
            else:
                raise SystemExit("theme %d: cannot read palette entry %r" % (i, t))
        pal = {role: flat[n] for n, role in enumerate(ROLES)}
        pal["tile"] = flat[len(ROLES)]
        pal["radius"] = flat[len(ROLES) + 1]
        pal["name"] = names[i] if i < len(names) else "theme %d" % i
        out.append(pal)
    return out


def theme_names(src):
    """Theme names, from the comment at the head of each palette row."""
    block = re.search(r"constexpr Palette PALETTES\[[^\]]*\] = \{(.*?)\n\};", src, re.S)
    names = []
    for m in re.finditer(r"(?://\s*|/\*\s*)([A-Z][A-Za-z ]*?)\s*--", block.group(1)):
        names.append(m.group(1).strip())
    return names


def pairs(p):
    """Every pairing the firmware actually draws, with what it has to be.

    Keep this list honest: a pairing that is not here is not checked, and a
    pairing that is here but never drawn makes the check lie in the other
    direction. Each entry is (what, on what, floor, why).
    """
    return [
        ("text", "bg", TEXT, "body text on the ground"),
        ("text", "surface", TEXT, "body text on a card"),
        ("text", "panel", TEXT, "text on a control"),
        # muted on the bare ground is held to the LARGE floor rather than the
        # body-text one. Body text on the ground is Ui::text(); what is drawn
        # muted there is short hints at font 2 and up, and holding this pair to
        # 4.5 would force the period themes' desktops to give up the very
        # colour they are (a mid grey, a teal) or flatten muted into text. On a
        # card or a control -- where About and System Info put paragraphs of it
        # -- it is held to 4.5 with no exception.
        ("muted", "bg", LARGE, "secondary text on the ground"),
        ("muted", "surface", TEXT, "secondary text on a card -- About, System Info"),
        ("muted", "panel", TEXT, "secondary text on a control"),
        ("barText", "bar", TEXT, "the top bar's title and glyphs"),
        ("success", "bg", GRAPHIC, "a right answer has to read as one"),
        ("success", "surface", GRAPHIC, "same, on a card"),
        ("error", "bg", GRAPHIC, "a wrong answer has to read as one"),
        ("error", "surface", GRAPHIC, "same, on a card"),
        ("warning", "bg", GRAPHIC, "warnings and the tracing guide arrow"),
        ("warning", "surface", GRAPHIC, "same, on a card"),
        ("outline", "bg", HAIRLINE, "hairlines on the ground"),
        ("outline", "surface", HAIRLINE, "hairlines on a card"),
    ]


# A card on a ground, and a control on a card, are allowed to be subtle: every
# one of them is drawn with an outline round it, and a raised bevel besides. So
# the rule is that the EDGE has to be findable -- either the fills differ, or
# the outline differs from both. Holding the fills themselves to a contrast
# ratio would forbid the quiet card-on-ground look every one of these themes
# is built from, and would be measuring the wrong thing.
EDGE_FILL = 1.25
EDGE_LINE = 1.6


def edges(p):
    return [("surface", "bg", "a card has to be findable on the ground"),
            ("panel", "surface", "a control has to be findable on a card")]


def check(palettes):
    problems = []
    print("%-13s %-26s %6s  %s" % ("theme", "pairing", "ratio", ""))
    for p in palettes:
        rows = []
        for what, on, floor, why in pairs(p):
            r = ratio(rgb565_to_rgb(p[what]), rgb565_to_rgb(p[on]))
            if r < floor:
                rows.append((what, on, r, floor, why))
                problems.append("%s: %s on %s is %.1f:1, needs %.1f -- %s"
                                % (p["name"], what, on, r, floor, why))
        for what, on, why in edges(p):
            fills = ratio(rgb565_to_rgb(p[what]), rgb565_to_rgb(p[on]))
            line = min(ratio(rgb565_to_rgb(p["outline"]), rgb565_to_rgb(p[what])),
                       ratio(rgb565_to_rgb(p["outline"]), rgb565_to_rgb(p[on])))
            if fills < EDGE_FILL and line < EDGE_LINE:
                rows.append((what, on, fills, EDGE_FILL,
                             "%s (outline only %.1f:1)" % (why, line)))
                problems.append("%s: %s on %s is %.1f:1 and the outline between "
                                "them only %.1f:1 -- %s"
                                % (p["name"], what, on, fills, line, why))
        # A launcher tile's label and subtitle take their ink from the fill --
        # Ui::onFill() / Ui::onFillSoft(), restated here. They used to be a
        # fixed white and a fixed near-white, which is what put a white label
        # on Classic's light grey tile at 1.3:1; if either of those ever goes
        # back to a constant, this check goes on passing while the panel stops
        # being readable, so keep the two in step.
        for i, tile in enumerate(p["tile"]):
            fill = rgb565_to_rgb(tile)
            for what, colour in (("label", on_fill(fill)),
                                 ("subtitle", on_fill_soft(fill))):
                r = ratio(colour, fill)
                if r < TEXT:
                    rows.append(("tile %s" % what, "tile %d" % i, r, TEXT,
                                 "launcher tile %s" % what))
                    problems.append("%s: the tile %s is %.1f:1 on tile %d, needs "
                                    "%.1f -- launcher tile %s"
                                    % (p["name"], what, r, i, TEXT, what))
        status = "clean" if not rows else "%d problem(s)" % len(rows)
        print("%-13s %s" % (p["name"], status))
        for what, on, r, floor, why in rows:
            print("%-13s   %-24s %5.1f  (needs %.1f) %s" % ("", "%s on %s" % (what, on), r, floor, why))
    return problems


def main():
    palettes = parse_palettes()
    print("Contrast check -- %d themes\n" % len(palettes))
    problems = check(palettes)
    if problems:
        print("\n%d problem(s). Fix the palette in src/ui/Ui.cpp, or the "
              "drawing code that pairs them." % len(problems))
        return 1
    print("\nContrast check: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
