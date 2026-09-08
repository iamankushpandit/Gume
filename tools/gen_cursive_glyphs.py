#!/usr/bin/env python3
"""Generate src/games/CursiveGlyphData.cpp from a dotted cursive font.

WHERE THE LETTERFORMS COME FROM, AND WHY NOT FROM HERE

The first version of this script held 52 hand-authored Bezier chains. It was
not good enough and it was not close: almost every capital came out as a print
letter with rounded corners, 'n' read as 'm', 'u' read as 'm', and 'a' read as
'or'. Cursive is a real hand with real proportions, and guessing control points
for it blind does not work.

So the shapes are extracted from FRB American Cursive ArrowPath, by Fredrick R.
Brennan, which is GPLv3 -- the same licence as Braino, which is why it can be
used here at all. Credit it in README's credits table; do not remove it.

WHY THAT FONT IN PARTICULAR. It is a teaching font whose glyphs are drawn as a
line of evenly spaced DOTS along the stroke path. That is exactly the thing a
tracing game needs and exactly the thing a normal font cannot give: an ordinary
cursive font's glyph is the OUTLINE of a thick stroke, so following it traces
around the letter rather than along it. Here the dot centres, in the order the
font stores them, ARE the centreline. Measured: consecutive dots are 36 units
apart to the unit, all the way through every glyph.

A PEN LIFT IS A BIG GAP. Within a stroke the step is 36. Where the hand lifts
-- the dot on an i, the crossbar of a t, the second stroke of an x -- the step
jumps to between 190 and 600. Anything over LIFT_GAP is a new stroke. That is
measurement, not guesswork, and it is why the strokes come out in writing order
without anything here knowing what a letter is.

TWO MODES, TWO SCALES. Single letters are normalised as a group so that 'a' and
'A' are the same size when you switch tabs; words are normalised as their own
group, because a three-letter word is twice as wide as it is tall and sharing a
scale with the letters would make it unreadably small. Both are emitted in the
same 0..200 box that LetterTracer maps onto its canvas.

Run it, then LOOK AT docs/cursive-sheet.png. Generated artwork is the one thing
nothing else will catch: the table compiles, the game runs, the dots appear,
and the letter is simply not the letter.
"""

import io
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The font is not committed -- it is 9MB and Braino ships geometry, not fonts.
# Point this at a local copy to regenerate.
FONT = os.environ.get(
    'CURSIVE_FONT',
    r'C:/Users/Ankus/AppData/Local/Temp/FrbAmericanCursiveArrowpath-0WLvr.otf')

COORD_MAX = 200
MARGIN = 6            # keeps a stroke's end off the very edge of the box
LIFT_GAP = 80         # font units; within-stroke steps are 36
# Drop dots closer together than this in the OUTPUT box. The dots are ~7px
# apart once scaled and the tracer resamples at its own spacing anyway, so
# keeping every one of them would be flash spent on invisible detail.
MIN_STEP = 4

WORDS = ['cat', 'dog', 'sun', 'bed', 'cup', 'hat', 'pig', 'run', 'top', 'man']

MAX_STROKES = 6       # LetterTracer::MAX_STROKES

# A stroke this short is a mark rather than a letter -- the dot on an i, the
# crossbar of a t. See order_strokes().
MARK_DOTS = 8


def load():
    from fontTools.ttLib import TTFont
    if not os.path.exists(FONT):
        sys.stderr.write(
            'Font not found: %s\nSet CURSIVE_FONT to a copy of FRB American '
            'Cursive ArrowPath (GPLv3).\n' % FONT)
        return None, None, None
    f = TTFont(FONT, lazy=True)
    return f, f.getGlyphSet(), f.getBestCmap()


def dot_centres(gs, cmap, ch):
    """Every dot of one character, as (x, y) centres in font units, in order."""
    from fontTools.pens.recordingPen import RecordingPen
    pen = RecordingPen()
    gs[cmap[ord(ch)]].draw(pen)
    contours, cur = [], []
    for op, args in pen.value:
        if op == 'moveTo':
            if cur:
                contours.append(cur)
            cur = [args[0]]
        elif op == 'lineTo':
            cur.append(args[0])
        elif op == 'curveTo':
            cur.extend(args)
        elif op == 'qCurveTo':
            cur.extend([a for a in args if a])
        elif op == 'closePath':
            if cur:
                contours.append(cur)
                cur = []
    if cur:
        contours.append(cur)
    return [(sum(p[0] for p in c) / len(c), sum(p[1] for p in c) / len(c))
            for c in contours]


def split_strokes(pts):
    """Break the dot run wherever the hand lifted. See LIFT_GAP."""
    if not pts:
        return []
    out, cur = [], [pts[0]]
    for a, b in zip(pts, pts[1:]):
        if math.dist(a, b) > LIFT_GAP:
            out.append(cur)
            cur = [b]
        else:
            cur.append(b)
    out.append(cur)
    return [s for s in out if len(s) >= 2]


def order_strokes(strokes):
    """Put the letter before the marks that decorate it.

    THE FONT STORES THEM THE OTHER WAY ROUND. Measured: 'i' is a 3-dot stroke
    at y=414 followed by a 26-dot body; 't' is a 5-dot crossbar then the body;
    same for 'j' and capital 'F'. Drawn in that order a child is asked to place
    the dot in mid-air and then hang a stem under it, which is not how anybody
    writes and not what the numbered badges should teach.

    Only genuinely short strokes are moved. The two strokes of H, K and X are
    both parts of the letter -- 19 and 44 dots, 37 and 19, 32 and 20 -- and
    reordering those on a length heuristic would be inventing a stroke order
    the font did not give.
    """
    body = [s for s in strokes if len(s) > MARK_DOTS]
    marks = [s for s in strokes if len(s) <= MARK_DOTS]
    return body + marks


def char_strokes(gs, cmap, ch, dx=0.0):
    """Strokes of one character, shifted right by dx font units."""
    strokes = order_strokes(split_strokes(dot_centres(gs, cmap, ch)))
    return [[(x + dx, y) for x, y in s] for s in strokes]


def word_strokes(gs, cmap, word):
    """Strokes of a whole word, letters placed at their own advance widths.

    ONE STROKE PER LETTER, NOT ONE PER WORD, and that is measurement rather
    than preference. The obvious thing is to join the letters into a single
    unbroken stroke -- that is what cursive is -- but the exit of one glyph
    does not actually land on the entry of the next in this font: measured
    across cat, dog and top, the gap is 185 to 408 font units where a step
    within a stroke is 36. Concatenating them would draw a straight line
    across that gap and teach a join that is not there.

    Sliding each letter left until it touched the one before would close the
    gap and wreck the spacing, so the letters keep their advance widths and the
    tracer numbers them. A child still traces the word left to right, in
    writing order, one letter at a time.
    """
    out, x = [], 0.0
    for ch in word:
        out.extend(char_strokes(gs, cmap, ch, x))
        x += gs[cmap[ord(ch)]].width
    return out


def normalise(items):
    """Fit a group of glyphs into the 0..200 box on one shared scale.

    Shared, because 'a' and 'A' switching size when you change tabs looks like
    a bug, and because a common baseline is what makes the writing lines mean
    anything. Aspect is preserved: a word ends up wide and short inside the
    box, which is what the near-square canvas then draws correctly.
    """
    xs = [x for _, strokes in items for s in strokes for x, _ in s]
    ys = [y for _, strokes in items for s in strokes for _, y in s]
    if not xs:
        return []
    wide = max(x for _, strokes in items for s in strokes for x, _ in s) - min(xs)
    high = max(ys) - min(ys)
    span = COORD_MAX - 2 * MARGIN
    scale = min(span / wide, span / high) if wide and high else 1.0
    # One baseline for the group, from the group's own top edge.
    top = max(ys)

    out = []
    for label, strokes in items:
        gx = [x for s in strokes for x, _ in s]
        left = min(gx)
        width = max(gx) - left
        ox = MARGIN + (span - width * scale) / 2.0     # centred horizontally
        placed = []
        for s in strokes:
            pts, last = [], None
            for x, y in s:
                p = (int(round(ox + (x - left) * scale)),
                     int(round(MARGIN + (top - y) * scale)))
                if last is None or math.dist(p, last) >= MIN_STEP:
                    pts.append(p)
                    last = p
            # The end of a stroke is where the pen stops; never decimate it away.
            end = (int(round(ox + (s[-1][0] - left) * scale)),
                   int(round(MARGIN + (top - s[-1][1]) * scale)))
            if not pts or pts[-1] != end:
                pts.append(end)
            if len(pts) >= 2:
                placed.append(pts)
        out.append((label, placed))
    return out


def check(label, strokes):
    problems = []
    if len(strokes) > MAX_STROKES:
        problems.append('%d strokes, LetterTracer allows %d'
                        % (len(strokes), MAX_STROKES))
    for i, s in enumerate(strokes):
        for x, y in s:
            if not (0 <= x <= COORD_MAX and 0 <= y <= COORD_MAX):
                problems.append('stroke %d leaves the box at (%d,%d)' % (i, x, y))
                break
    return problems


def cname(label):
    if len(label) == 1:
        return ('U_' if label.isupper() else 'L_') + label.upper()
    return 'W_' + label.upper()


def emit(letters, words):
    lines = [
        '/* GENERATED by tools/gen_cursive_glyphs.py -- do not edit.',
        ' *',
        ' * Cursive letterforms and words, taken from the dot centres of',
        ' * FRB American Cursive ArrowPath by Fredrick R. Brennan (GPLv3).',
        ' * Edit the script and regenerate; a fix typed in here is lost the',
        ' * next time anybody runs it, and the preview sheet the script writes',
        ' * is the only way to see whether a letter looks like the letter.',
        ' */',
        '',
        '#include "CursiveGlyphData.h"',
        '',
    ]
    rows = []
    for label, strokes in letters + words:
        tag = cname(label)
        for si, pts in enumerate(strokes):
            flat = ', '.join('%d,%d' % (x, y) for x, y in pts)
            lines.append('static const int16_t %s_s%d[] = {%s};' % (tag, si, flat))
        joined = ','.join('{%s_s%d,%d}' % (tag, si, len(pts))
                          for si, pts in enumerate(strokes))
        lines.append('static const CursiveGame::Stroke %s_strokes[] = {%s};'
                     % (tag, joined))
        lines.append('')
        rows.append((label, tag, len(strokes)))

    lines.append('const CursiveGame::Glyph CURSIVE_GLYPHS[] = {')
    for label, tag, n in rows:
        # A word's label is the word; a letter's is the character. Glyph::label
        # is one char, so a word carries its first letter and the game draws
        # the full word from CURSIVE_WORDS instead.
        lines.append("    {'%s', %s_strokes, %d},   // %s"
                     % (label[0], tag, n, label))
    lines.append('};')
    lines.append('')
    lines.append('')
    lines.append('/* The words, in the same order as the word glyphs above, so')
    lines.append(' * the game can print what it is asking for. */')
    lines.append('const char* const CURSIVE_WORDS[] = {')
    for label, _, _ in rows:
        if len(label) > 1:
            lines.append('    "%s",' % label)
    lines.append('};')
    lines.append('')
    return '\n'.join(lines)


def emit_header(letters, words):
    """The counts, as compile-time constants.

    CursiveGame builds its Set table from these and that table is constexpr, so
    an `extern const uint8_t` in another translation unit will not do -- the
    compiler cannot see the value. Generated rather than typed for the usual
    reason: adding a word to WORDS should not require anybody to remember to
    change a number in a header.
    """
    return '\n'.join([
        '#pragma once',
        '',
        '/* GENERATED by tools/gen_cursive_glyphs.py -- do not edit. */',
        '',
        '#include "CursiveGame.h"',
        '',
        'extern const CursiveGame::Glyph CURSIVE_GLYPHS[];',
        '',
        '/* The word-tracing set lives after all the letters in CURSIVE_GLYPHS.',
        ' * CURSIVE_WORDS is the text of each, so the screen can print what it',
        ' * is asking for -- Glyph::label holds one character and a word needs',
        ' * more than that. */',
        'extern const char* const CURSIVE_WORDS[];',
        '',
        'constexpr uint8_t CURSIVE_GLYPH_COUNT = %d;' % (len(letters) + len(words)),
        'constexpr uint8_t CURSIVE_WORD_FIRST = %d;' % len(letters),
        'constexpr uint8_t CURSIVE_WORD_COUNT = %d;' % len(words),
        '',
    ])


def preview(letters, words):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        sys.stderr.write('Pillow not installed; skipping the preview sheet.\n')
        return
    items = letters + words
    cols = 13
    rows = (len(items) + cols - 1) // cols
    cell = 104
    im = Image.new('RGB', (cols * cell, rows * cell), (250, 248, 244))
    d = ImageDraw.Draw(im)
    for i, (label, strokes) in enumerate(items):
        ox, oy = (i % cols) * cell, (i // cols) * cell
        d.rectangle([ox, oy, ox + cell - 1, oy + cell - 1], outline=(214, 208, 198))
        for si, pts in enumerate(strokes):
            shade = [(24, 40, 96), (176, 64, 32), (32, 120, 60),
                     (140, 40, 140), (180, 140, 20), (60, 60, 60)][si % 6]
            xy = [(ox + 2 + x * (cell - 4) / COORD_MAX,
                   oy + 2 + y * (cell - 4) / COORD_MAX) for x, y in pts]
            d.line(xy, fill=shade, width=2)
            d.ellipse([xy[0][0] - 2, xy[0][1] - 2, xy[0][0] + 2, xy[0][1] + 2],
                      fill=(0, 160, 60))
        d.text((ox + 3, oy + 3), label, fill=(120, 120, 120))
    out = os.path.join(ROOT, 'docs', 'cursive-sheet.png')
    im.save(out)
    print('preview  %s' % out)


def main():
    f, gs, cmap = load()
    if f is None:
        return 1

    upper = [(c, char_strokes(gs, cmap, c))
             for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ']
    lower = [(c, char_strokes(gs, cmap, c))
             for c in 'abcdefghijklmnopqrstuvwxyz']

    # NORMALISED PER SET, NOT ALL TOGETHER, AND THIS IS A MEASUREMENT.
    #
    # One scale across all 52 letters is the tidy answer and it is the wrong
    # one: the group's box is set by the tallest capital and the deepest
    # descender together, 908 font units, so a lowercase 'a' at 258 units came
    # out 43 pixels tall on the canvas. At the tracer's 20-pixel dot spacing
    # that is barely three dots for a whole letter.
    #
    # Capitals and lowercase live on separate tabs and are never on screen
    # together, so nothing is lost by giving each its own scale, and each then
    # fills the canvas. Within a set the baseline is still shared, which is
    # the part that matters when you page through an alphabet.
    #
    # Words go further and are normalised ONE AT A TIME. As a group the scale
    # is set by the widest word and by the ascender-plus-descender range, so
    # 'cat' -- which has neither a descender nor much width -- rendered at
    # about a third of the canvas. Each word filling its own box is worth more
    # here than the words matching each other.
    letters = normalise(upper) + normalise(lower)
    words = [normalise([(w, word_strokes(gs, cmap, w))])[0] for w in WORDS]

    bad = 0
    for label, strokes in letters + words:
        for problem in check(label, strokes):
            sys.stderr.write("'%s': %s\n" % (label, problem))
            bad += 1
    if bad:
        sys.stderr.write('nothing written.\n')
        return 1

    body = emit(letters, words)
    out = os.path.join(ROOT, 'src', 'games', 'CursiveGlyphData.cpp')
    io.open(out, 'w', encoding='utf-8', newline='\n').write(body)
    hdr = os.path.join(ROOT, 'src', 'games', 'CursiveGlyphData.h')
    io.open(hdr, 'w', encoding='utf-8', newline='\n').write(
        emit_header(letters, words))
    pts = sum(len(s) for _, ss in letters + words for s in ss)
    print('wrote    %s' % out)
    print('         %d letters, %d words, %d points, %d strokes max'
          % (len(letters), len(words), pts,
             max(len(ss) for _, ss in letters + words)))
    preview(letters, words)
    print('\nLOOK AT THE PREVIEW SHEET. Green dot = stroke start; each stroke')
    print('is a different colour, in writing order.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
