#!/usr/bin/env python3
"""Generate the chess piece sprites from tools/chess_pieces.svg.

Why sprites, and why generated
------------------------------
The first version of the board drew a letter on each square. Letters are a
literacy test rather than a chess game -- useless to the youngest players this
console is for, and to anyone whose sight makes a 26px glyph hard to resolve.
Silhouettes are what a person recognises, and a knight has to look like a horse
at 26 pixels on a resistive panel.

Hand-drawing those from triangles at render time is possible and it is the
wrong place to do it: you cannot see what you are making. Here the artwork is
traced once, rasterised to 1-bit masks, and a preview PNG is written beside
them -- so "is that a knight?" is answered by looking, which is the only way it
can be answered.

One mask per piece, not one per colour. Colour comes from the blit: the mask is
painted offset in an edge colour and then in a fill colour, giving every piece
a rim that separates it from either square shade and from all nine themes.
White pieces are light with a dark rim, black pieces dark with a light rim,
which is how a real set reads. Using a single silhouette also means the two
sides register exactly, which two separately-traced drawings would not.

Provenance
----------
tools/chess_pieces.svg came from https://svgsilh.com/image/26774.html, which
releases everything under Creative Commons CC0 -- a public domain dedication,
so no attribution is required and nothing about it restricts this firmware. It
is credited in the README's licensing table anyway: the obligation is nil, but
"we checked, and here is where it came from" is worth more to whoever reads
this in two years than silence. If you replace the sheet, check the new one's
licence before you do, and update that row.

    python tools/gen_chess_sprites.py

Writes src/games/ChessSprites.{h,cpp} and docs/chess-sprites.png.
Regenerating rewrites all three; edit this file or the SVG, never the output.
"""
import os
import re

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG = os.path.join(ROOT, "tools", "chess_pieces.svg")

# 32x32 is four bytes a row, 128 bytes a piece, 768 bytes for the set. Against
# the ~760KB of flag artwork already in the image this is free, and it is
# enough resolution that the shapes survive being scaled down to a 26px square.
SIZE = 32

# The source sheet is two rows of six: filled glyphs above, outlined below.
# Only the filled row is used -- see the note about registration above.
COLS, ROWS = 6, 2
# Left to right in the sheet, then reordered to the piece codes in ChessGame.h.
SHEET_ORDER = ["KING", "QUEEN", "ROOK", "BISHOP", "KNIGHT", "PAWN"]
EMIT_ORDER = ["PAWN", "KNIGHT", "BISHOP", "ROOK", "QUEEN", "KING"]


def parse_transform(text):
    """potrace emits translate(tx,ty) scale(sx,sy); sy is negative."""
    m = re.search(r"translate\(([-\d.]+),([-\d.]+)\)\s*scale\(([-\d.]+),([-\d.]+)\)", text)
    if not m:
        return 0.0, 0.0, 1.0, 1.0
    return (float(m.group(1)), float(m.group(2)),
            float(m.group(3)), float(m.group(4)))


def flatten_cubic(p0, p1, p2, p3, steps=12):
    """Cubic bezier to points. Twelve steps is well past what 32px can show."""
    out = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1.0 - t
        x = (u * u * u * p0[0] + 3 * u * u * t * p1[0]
             + 3 * u * t * t * p2[0] + t * t * t * p3[0])
        y = (u * u * u * p0[1] + 3 * u * u * t * p1[1]
             + 3 * u * t * t * p2[1] + t * t * t * p3[1])
        out.append((x, y))
    return out


def parse_path(d):
    """The subset potrace emits: absolute M, relative m/c/l/v/h, and z.

    Returns a list of closed subpaths, each a list of points. Handled by hand
    rather than with a library because the grammar in use here is six commands
    wide and adding a dependency to read six commands is a poor trade.
    """
    tokens = re.findall(r"[MmZzLlHhVvCcSsQqTtAa]|-?\d*\.?\d+", d)
    subpaths, cur = [], []
    x = y = 0.0
    start = (0.0, 0.0)
    i, cmd = 0, None
    while i < len(tokens):
        t = tokens[i]
        if re.match(r"[A-Za-z]", t):
            cmd = t
            i += 1
            if cmd in "Zz":
                if len(cur) > 2:
                    subpaths.append(cur)
                cur = []
                x, y = start
                continue
        rel = cmd.islower()

        def num():
            nonlocal i
            v = float(tokens[i])
            i += 1
            return v

        if cmd in "Mm":
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            if len(cur) > 2:
                subpaths.append(cur)
            cur = [(x, y)]
            start = (x, y)
            cmd = "l" if rel else "L"      # implicit lineto after moveto
        elif cmd in "Ll":
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            cur.append((x, y))
        elif cmd in "Hh":
            nx = num()
            x = x + nx if rel else nx
            cur.append((x, y))
        elif cmd in "Vv":
            ny = num()
            y = y + ny if rel else ny
            cur.append((x, y))
        elif cmd in "Cc":
            c1 = (num(), num())
            c2 = (num(), num())
            e = (num(), num())
            if rel:
                c1 = (x + c1[0], y + c1[1])
                c2 = (x + c2[0], y + c2[1])
                e = (x + e[0], y + e[1])
            cur.extend(flatten_cubic((x, y), c1, c2, e))
            x, y = e
        else:
            raise SystemExit("unsupported SVG path command %r -- this sheet "
                             "uses something the parser was not written for" % cmd)
    if len(cur) > 2:
        subpaths.append(cur)
    return subpaths


def main():
    if not os.path.exists(SVG):
        raise SystemExit("missing %s -- the traced piece sheet is the input to "
                         "this generator and belongs in the repo beside it" % SVG)
    text = open(SVG, encoding="utf-8").read()
    tx, ty, sx, sy = parse_transform(text)

    polys = []
    for d in re.findall(r'<path[^>]*\bd="([^"]+)"', text):
        for sub in parse_path(d):
            polys.append([(px * sx + tx, py * sy + ty) for px, py in sub])

    width = float(re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', text).group(1))
    height = float(re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', text).group(2))
    cellW, cellH = width / COLS, height / ROWS

    # Bucket each subpath into a sheet cell by its centroid, then keep the top
    # row only. A glyph is several subpaths (a crown has holes), so grouping by
    # cell is what reassembles them.
    buckets = {}
    for poly in polys:
        cx = sum(p[0] for p in poly) / len(poly)
        cy = sum(p[1] for p in poly) / len(poly)
        col, row = int(cx // cellW), int(cy // cellH)
        if row != 0:
            continue                      # filled row only
        buckets.setdefault(min(col, COLS - 1), []).append(poly)

    masks = {}
    for col, group in buckets.items():
        xs = [p[0] for poly in group for p in poly]
        ys = [p[1] for poly in group for p in poly]
        minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
        span = max(maxx - minx, maxy - miny)
        pad = SIZE * 0.06
        scale = (SIZE - pad * 2) / span
        ox = (SIZE - (maxx - minx) * scale) / 2
        oy = (SIZE - (maxy - miny) * scale) / 2

        # Even-odd fill: rasterise each subpath and XOR it in, so the holes in
        # a crown or a rook stay holes without needing winding rules.
        acc = Image.new("1", (SIZE, SIZE), 0)
        for poly in group:
            layer = Image.new("1", (SIZE, SIZE), 0)
            pts = [((px - minx) * scale + ox, (py - miny) * scale + oy)
                   for px, py in poly]
            ImageDraw.Draw(layer).polygon(pts, fill=1)
            # mode "1" packs 8 pixels per byte, so a byte-wise XOR is a pixel-wise one
            acc = Image.frombytes("1", (SIZE, SIZE),
                                  bytes(a ^ b for a, b in
                                        zip(acc.tobytes(), layer.tobytes())))
        masks[SHEET_ORDER[col]] = acc

    missing = [n for n in EMIT_ORDER if n not in masks]
    if missing:
        raise SystemExit("no glyph found for %s -- check SHEET_ORDER against "
                         "the preview" % ", ".join(missing))

    # Preview: every piece, both colours, on both square shades. Look at it.
    cell = SIZE * 3
    pad = 6
    prev = Image.new("RGB", (len(EMIT_ORDER) * (cell + pad) + pad,
                             cell * 2 + pad * 3), (60, 60, 66))
    pd = ImageDraw.Draw(prev)
    for i, name in enumerate(EMIT_ORDER):
        big = masks[name].resize((cell, cell), Image.NEAREST)
        for row, (sqcol, pcol) in enumerate((((222, 210, 180), (20, 20, 26)),
                                             ((120, 96, 72), (248, 248, 244)))):
            x0 = pad + i * (cell + pad)
            y0 = pad + row * (cell + pad)
            pd.rectangle([x0, y0, x0 + cell - 1, y0 + cell - 1], fill=sqcol)
            prev.paste(Image.new("RGB", (cell, cell), pcol), (x0, y0), big)
    # docs/ and not docs/screens/: gen_site.py validates that every still in
    # docs/screens is referenced by the site and dies on any that is not. This
    # is a generator preview for whoever edits the sprites, not a screenshot of
    # the product.
    os.makedirs(os.path.join(ROOT, "docs"), exist_ok=True)
    prev.save(os.path.join(ROOT, "docs", "chess-sprites.png"))

    header = '''#pragma once

#include <stdint.h>

/* Chess piece sprites -- GENERATED by tools/gen_chess_sprites.py from
 * tools/chess_pieces.svg. Do not edit.
 *
 * One %dx%d silhouette per piece, as rows of uint32 with bit 31 leftmost.
 * Colour is not stored: ChessGame::drawPiece() paints the same mask twice, once
 * offset in an edge colour and once in a fill colour, so one set of shapes
 * serves both sides and every theme -- and the two sides register exactly,
 * which separate drawings would not. */
namespace ChessSprites {

constexpr uint8_t SIZE = %d;

/** Indexed by piece kind - 1, matching PAWN..KING in ChessGame.h. */
extern const uint32_t MASK[6][SIZE];

}   // namespace ChessSprites
''' % (SIZE, SIZE, SIZE)

    body = ['#include "ChessSprites.h"', '',
            '/* GENERATED by tools/gen_chess_sprites.py. Do not edit. */', '',
            'namespace ChessSprites {', '',
            'const uint32_t MASK[6][SIZE] = {']
    for name in EMIT_ORDER:
        px = masks[name].load()
        body.append('    {   // %s' % name)
        vals = []
        for y in range(SIZE):
            bits = 0
            for x in range(SIZE):
                if px[x, y]:
                    bits |= 1 << (31 - x)
            vals.append(bits)
        for i in range(0, SIZE, 4):
            body.append('        ' + ' '.join('0x%08X,' % v for v in vals[i:i + 4]))
        body.append('    },')
    body += ['};', '', '}   // namespace ChessSprites', '']

    open(os.path.join(ROOT, "src", "games", "ChessSprites.h"), "w",
         encoding="utf-8", newline="").write(header)
    open(os.path.join(ROOT, "src", "games", "ChessSprites.cpp"), "w",
         encoding="utf-8", newline="").write("\n".join(body))
    print("wrote src/games/ChessSprites.{h,cpp} and docs/chess-sprites.png")


if __name__ == "__main__":
    main()
