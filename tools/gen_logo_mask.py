#!/usr/bin/env python3
"""Generate src/ui/LogoMask.{h,cpp} from tools/braino-badge.svg.

WHY A MASK AND NOT THE SVG.

The badge is the product's own mark -- the brain above the wordmark -- and it
arrives as 50KB of flattened vector paths. Nothing in this firmware can draw a
path: there is no SVG renderer, no font that contains it, and no room to add
either. So it is rasterised once, here, into a one-bit silhouette that costs
under a kilobyte of flash and blits as horizontal runs.

ONE BIT AND NOT FOUR. The artwork is a single black fill on white, so a colour
depth would store nothing but the anti-aliasing -- and anti-aliasing needs a
known background, which this does not have: the screen saver paints the mark in
a shade of the rally colour that changes on every paddle hit. A mask can be
painted in any colour at all, which is why it is a mask.

The SVG's paths are straight segments apart from the circuit nodes on the right
half of the brain, which are pairs of semicircular ARCS -- so the parser
flattens those and rejects anything else. A Bezier would need flattening the
way tools/gen_chess_sprites.py does it; neither wants a rendering library.

Run it, then LOOK AT docs/logo-mask.png. It is generated artwork: the table
compiles and the console draws something whatever comes out, and the only way
to know the brain still reads as a brain at 90 pixels is to look.
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG = os.path.join(ROOT, "tools", "braino-badge.svg")

# HOW TALL THE MARK IS DRAWN, and this is the number that decides the cost.
#
# 90 pixels is what the screen saver has room for between the battery badge at
# the top and the copyright line under it, on the smallest panel Braino
# supports (240 tall in landscape). The aspect comes from the artwork, so the
# width follows; at 90 the mask is 72x90 and costs 810 bytes of flash.
HEIGHT = 90

# Supersampling for the fill. 4x4 is enough that the thin strokes inside the
# brain -- about two pixels wide at this size -- come out even rather than
# dropping a pixel here and there.
SUPERSAMPLE = 4

# A pixel is ink when this much of it is covered. Half is the honest middle;
# lower fattens every stroke, higher breaks the thinnest ones.
THRESHOLD = 0.5


def flatten_arc(x0, y0, rx, ry, rotation, large, sweep, x1, y1, steps=24):
    """An SVG elliptical arc as points, by the spec's own centre parameterisation.

    The badge uses these only for the round nodes on the circuit half of the
    brain -- each a pair of half-circles -- so this handles the general form but
    is only ever asked for circles.
    """
    import math
    if rx == 0 or ry == 0 or (x0 == x1 and y0 == y1):
        return [(x1, y1)]
    phi = math.radians(rotation)
    cos_p, sin_p = math.cos(phi), math.sin(phi)
    dx2, dy2 = (x0 - x1) / 2.0, (y0 - y1) / 2.0
    x1p = cos_p * dx2 + sin_p * dy2
    y1p = -sin_p * dx2 + cos_p * dy2
    rx, ry = abs(rx), abs(ry)
    # Scale the radii up if they are too small to join the endpoints at all.
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1:
        rx *= math.sqrt(lam)
        ry *= math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    factor = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        factor = -factor
    cxp = factor * rx * y1p / ry
    cyp = -factor * ry * x1p / rx
    cx = cos_p * cxp - sin_p * cyp + (x0 + x1) / 2.0
    cy = sin_p * cxp + cos_p * cyp + (y0 + y1) / 2.0

    def angle(ux, uy):
        a = math.atan2(uy, ux)
        return a

    theta0 = angle((x1p - cxp) / rx, (y1p - cyp) / ry)
    theta1 = angle((-x1p - cxp) / rx, (-y1p - cyp) / ry)
    sweep_angle = theta1 - theta0
    if sweep and sweep_angle < 0:
        sweep_angle += 2 * math.pi
    elif not sweep and sweep_angle > 0:
        sweep_angle -= 2 * math.pi
    out = []
    for i in range(1, steps + 1):
        t = theta0 + sweep_angle * i / steps
        ex, ey = rx * math.cos(t), ry * math.sin(t)
        out.append((cos_p * ex - sin_p * ey + cx, sin_p * ex + cos_p * ey + cy))
    return out


def parse_path(d, where):
    """One path's `d` as a list of closed subpaths. M, L, Z and arcs only."""
    tokens = re.findall(r"[MmLlZzAa]|-?\d*\.?\d+(?:[eE][-+]?\d+)?", d)
    subs, cur = [], []
    x = y = 0.0
    i = 0
    cmd = None
    while i < len(tokens):
        t = tokens[i]
        if re.match(r"[A-Za-z]", t):
            cmd = t
            i += 1
            if cmd in "Zz":
                if len(cur) >= 3:
                    subs.append(cur)
                cur = []
                continue
        if cmd is None:
            raise SystemExit("%s: path data starts without a command" % where)
        if cmd in "CcSsQqTt":
            raise SystemExit("%s: Bezier curves are not flattened here -- see "
                             "gen_chess_sprites.py" % where)
        nums = []
        need = 7 if cmd in "Aa" else 2
        while len(nums) < need and i < len(tokens) and not re.match(r"[A-Za-z]", tokens[i]):
            nums.append(float(tokens[i]))
            i += 1
        if len(nums) < need:
            break
        if cmd in "Mm":
            if len(cur) >= 3:
                subs.append(cur)
            x, y = (x + nums[0], y + nums[1]) if cmd == "m" else (nums[0], nums[1])
            cur = [(x, y)]
            cmd = "l" if cmd == "m" else "L"     # implicit line-to, per the spec
        elif cmd in "Ll":
            x, y = (x + nums[0], y + nums[1]) if cmd == "l" else (nums[0], nums[1])
            cur.append((x, y))
        else:   # A or a
            rx, ry, rot, large, sweep, ex, ey = nums
            ex, ey = (x + ex, y + ey) if cmd == "a" else (ex, ey)
            cur.extend(flatten_arc(x, y, rx, ry, rot, int(large), int(sweep), ex, ey))
            x, y = ex, ey
    if len(cur) >= 3:
        subs.append(cur)
    return subs


def load(path):
    """Every filled subpath in the SVG, plus its fill rule and the viewBox."""
    src = io.open(path, encoding="utf-8").read()
    box = re.search(r'viewBox="([-\d.eE ]+)"', src)
    if not box:
        raise SystemExit("no viewBox in %s" % path)
    view = [float(v) for v in box.group(1).split()]
    paths = []
    for n, m in enumerate(re.finditer(r"<path([^>]*?)d=\"([^\"]+)\"", src, re.S)):
        attrs, d = m.group(1), m.group(2)
        subs = parse_path(d, "%s path %d" % (os.path.basename(path), n))
        if subs:
            paths.append((subs, "evenodd" if "evenodd" in attrs else "nonzero"))
    return view, paths


def rasterise(view, paths, height, ss):
    """Coverage per pixel, 0..1, `height` tall and at the artwork's aspect."""
    import numpy as np
    vx, vy, vw, vh = view
    width = int(round(vw * height / vh))
    scale = (height * ss) / vh
    W, H = width * ss, height * ss
    ink = np.zeros((H, W), bool)

    for subs, rule in paths:
        wind = np.zeros((H, W), np.int32)
        for pts in subs:
            p = np.array(pts, float)
            p[:, 0] = (p[:, 0] - vx) * scale
            p[:, 1] = (p[:, 1] - vy) * scale
            for (x0, y0), (x1, y1) in zip(p, np.roll(p, -1, axis=0)):
                if y0 == y1:
                    continue
                step = 1 if y1 > y0 else -1
                lo, hi = (y0, y1) if y1 > y0 else (y1, y0)
                j0 = max(0, int(np.ceil(lo - 0.5)))
                j1 = min(H - 1, int(np.floor(hi - 0.5)))
                if j1 < j0:
                    continue
                rows = np.arange(j0, j1 + 1)
                t = (rows + 0.5 - y0) / (y1 - y0)
                xs = np.clip(np.ceil(x0 + (x1 - x0) * t - 0.5).astype(int), 0, W)
                for row, x in zip(rows, xs):
                    if x < W:
                        wind[row, x:] += step
        ink |= (wind % 2 != 0) if rule == "evenodd" else (wind != 0)

    return ink.reshape(height, ss, width, ss).mean(axis=(1, 3))


def emit(mask, width, height):
    per_row = (width + 7) // 8
    lines = [
        "/* GENERATED by tools/gen_logo_mask.py from tools/braino-badge.svg.",
        " * Do not edit -- a fix typed in here is lost the next time anybody",
        " * runs it, and the preview sheet is the only way to see the result. */",
        "",
        '#include "LogoMask.h"',
        "",
        "const uint8_t LogoMask::BITS[LogoMask::HEIGHT][LogoMask::BYTES_PER_ROW] = {",
    ]
    for y in range(height):
        row = []
        for b in range(per_row):
            byte = 0
            for bit in range(8):
                x = b * 8 + bit
                if x < width and mask[y][x]:
                    byte |= 0x80 >> bit
            row.append("0x%02X" % byte)
        lines.append("    {%s}," % ", ".join(row))
    lines.append("};")
    lines.append("")
    header = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "/* The Braino! badge -- GENERATED by tools/gen_logo_mask.py from",
        " * tools/braino-badge.svg. Do not edit.",
        " *",
        " * A one-bit silhouette of the product mark, bit 7 of each byte",
        " * leftmost, so it can be painted in any colour the caller likes --",
        " * which the screen saver does, in a shade that changes with the rally.",
        " * Ui::drawLogo() is the way to draw it. */",
        "namespace LogoMask {",
        "",
        "constexpr int16_t WIDTH = %d;" % width,
        "constexpr int16_t HEIGHT = %d;" % height,
        "constexpr int16_t BYTES_PER_ROW = %d;" % per_row,
        "",
        "extern const uint8_t BITS[HEIGHT][BYTES_PER_ROW];",
        "",
        "}   // namespace LogoMask",
        "",
    ]
    return "\n".join(lines), "\n".join(header)


def preview(mask, width, height):
    try:
        from PIL import Image
    except ImportError:
        sys.stderr.write("Pillow not installed; skipping the preview sheet.\n")
        return
    import numpy as np
    scale = 4
    ink = np.array(mask, bool)
    # Left: the mark as the panel draws it, light on the saver's black. Right:
    # magnified, so a broken stroke is visible rather than merely small.
    small = Image.fromarray(np.where(ink, 235, 12).astype(np.uint8)).convert("RGB")
    big = small.resize((width * scale, height * scale), Image.NEAREST)
    sheet = Image.new("RGB", (width + big.width + 40, max(height, big.height) + 20), (12, 12, 12))
    sheet.paste(small, (10, 10))
    sheet.paste(big, (width + 30, 10))
    out = os.path.join(ROOT, "docs", "logo-mask.png")
    sheet.save(out)
    print("preview  %s" % out)


def main():
    if not os.path.exists(SVG):
        sys.stderr.write("%s not found.\n" % SVG)
        return 1
    view, paths = load(SVG)
    cov = rasterise(view, paths, HEIGHT, SUPERSAMPLE)
    mask = cov >= THRESHOLD
    height, width = mask.shape
    body, header = emit(mask, width, height)
    io.open(os.path.join(ROOT, "src", "ui", "LogoMask.cpp"), "w",
            encoding="utf-8", newline="\n").write(body)
    io.open(os.path.join(ROOT, "src", "ui", "LogoMask.h"), "w",
            encoding="utf-8", newline="\n").write(header)
    per_row = (width + 7) // 8
    print("wrote    src/ui/LogoMask.{h,cpp}")
    print("         %dx%d from %d path(s), %.0f%% ink, %d bytes of flash"
          % (width, height, len(paths), 100.0 * mask.mean(), per_row * height))
    preview(mask, width, height)
    print("\nLOOK AT THE PREVIEW. A mark that has lost a stroke still compiles.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
