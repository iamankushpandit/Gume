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
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The font is not committed -- it is 9MB and Braino ships geometry, not fonts.
# Point this at a local copy to regenerate.
FONT = os.environ.get(
    'CURSIVE_FONT',
    r'C:/Users/Ankus/AppData/Local/Temp/FrbAmericanCursiveArrowpath-0WLvr.otf')

# THE BOX THESE GLYPHS ARE AUTHORED IN, and it is not square.
#
# LetterTracer maps a table's box into its canvas with ONE scale for both axes
# and letterboxes the remainder -- so a table authored 200x200 and drawn into a
# 200x156 canvas gets margins left and right and loses a quarter of its width.
# For single letters that is fine. For a joined word, width is the whole point.
#
# So these are authored to the canvas's own shape, 200x156, and the tracer then
# maps them 1:1. The numbers are emitted into the generated header as
# CURSIVE_COORD_W/H so the game passes back exactly what was used here; if
# LetterTracer's DRAW_W or DRAW_H ever change, this should follow them, and if
# it does not the only cost is a letterbox rather than a distortion.
COORD_W = 200
COORD_H = 156
MARGIN = 6            # keeps a stroke's end off the very edge of the box
LIFT_GAP = 80         # font units; within-stroke steps are 36
# Drop dots closer together than this in the OUTPUT box. The dots are ~7px
# apart once scaled and the tracer resamples at its own spacing anyway, so
# keeping every one of them would be flash spent on invisible detail.
MIN_STEP = 4
# Words keep fewer. Drawn a third larger than they were, the font's dots land
# about 4.9px apart, so a 4px step kept every one of them and the word table
# grew from 13KB of flash to 20KB. Measured on the 49 words:
#
#     4px   20.2KB   every dot
#     5px   14.4KB   most dots; loops still round
#     6px   10.4KB   every other dot; the tightest loops, about a 10px radius,
#                    go visibly faceted -- chords stray ~1.3px off the curve
#
# 5 is the step that keeps the shape a child is copying and still gives back
# most of the growth.
WORD_MIN_STEP = 5

# THE WORD LIST, AND WHY IT IS SHORT WORDS ONLY.
#
# The first list was every word on the practice sheets the maintainer supplied
# -- 166 Dolch sight words, most of them four to eight letters -- filtered to
# those under 1900 font units wide. It shipped, and user testing sank it:
# players are as young as five, most have never written joined-up before, and
# they said plainly that the words were too small to follow. They were right.
# Every word shares one scale and the canvas is 200 pixels wide, so the widest
# word sets the size of all of them, and at 1900 units that left an x-height of
# about 25 pixels -- well under the 43 of the same letters on the abc tab.
#
# So the list is now chosen for the child rather than taken from the sheets:
# two- and three-letter words a five-year-old reads or is about to, covering
# the alphabet as far as the font allows. See WORD_WIDTH_CAP for the one letter
# it does not.
KID_WORDS = [
    'at', 'all', 'bee', 'bed', 'cat', 'can', 'car', 'dog', 'dad', 'egg',
    'ear', 'fox', 'fly', 'go', 'hot', 'ice', 'it', 'is', 'jet', 'joy',
    'kit', 'leg', 'log', 'me', 'no', 'net', 'nut', 'on', 'ox', 'one',
    'pet', 'pot', 'rat', 'red', 'see', 'sit', 'sea', 'ten', 'the', 'toy',
    'top', 'up', 'use', 'vet', 'we', 'wet', 'yes', 'you', 'zoo',
]

# THE WIDTH CAP, AND IT IS ARITHMETIC RATHER THAN TASTE.
#
# Every word shares one scale, so the widest word decides how big all of them
# are. Measured in this font, in font units, with the x-height it gives:
#
#     cap 1900   (the old list, 'been', 'apple')   25 px
#     cap 1400   (this list, widest 'kit')         35 px
#     cap 1104   (the abc tab's own scale)         44 px
#
# 1400 is where the kid words live. Going tighter buys a few pixels and loses
# every word with a b, h, k or r in the middle, which is most of the ones a
# five-year-old knows. It costs one letter outright: no word with a cursive q
# is narrower than 'quiz' at 1765, because q always brings its u, so q is
# practised on the abc tab and not here.
#
# A listed word over the cap is an error, not a quiet omission: a word the
# maintainer asked for that silently never appears is a bug nobody sees.
WORD_WIDTH_CAP = 1400

SHUFFLE_SEED = 20260908

MAX_STROKES = 8       # LetterTracer::MAX_STROKES

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
    """A word as ONE unbroken stroke, plus whatever marks sit above it.

    THIS IS THE WHOLE POINT OF CURSIVE and the first version got it wrong. It
    kept one stroke per letter, so the tracer numbered them and asked the child
    to lift between every letter -- which is not cursive, it is print in a
    fancy hand, and it was rightly called out as such.

    What made that look defensible was a bad measurement: the gap between the
    END of one glyph's dot run and the START of the next glyph's is 185 to 408
    font units, which looked like proof the letters do not touch. It is not.
    Those are DRAWING-ORDER endpoints, not the points where the ink meets -- an
    'a' is written from the top right of its oval, so its first dot is nowhere
    near its left edge. Typing a word in this font produces properly joined
    script; the shapes were always connected and only the stroke list was not.

    So the bodies are concatenated in writing order. The connector between two
    letters is the straight run the resampler walks between them, which is what
    a hand does anyway. Marks -- the dot on an i, the crossbar of a t -- stay
    separate, because those genuinely are pen lifts.
    """
    body, marks, x = [], [], 0.0
    for ch in word:
        strokes = char_strokes(gs, cmap, ch, x)
        if strokes:
            body.extend(strokes[0])      # order_strokes puts the letter first
            marks.extend(strokes[1:])
        x += gs[cmap[ord(ch)]].width
    return ([body] if len(body) >= 2 else []) + marks


def normalise(items, min_step=MIN_STEP):
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
    # The WIDEST SINGLE GLYPH, each measured against its own left edge. This
    # was the group's rightmost x minus its leftmost, which is only the same
    # thing when every glyph starts at zero -- and a 'j' or a 'y' reaches left
    # of its origin, so one descender loop made the box look wider than any
    # word in it and shrank the whole set by 7%.
    wide = max(max(x for s in strokes for x, _ in s) -
               min(x for s in strokes for x, _ in s) for _, strokes in items)
    high = max(ys) - min(ys)
    spanX = COORD_W - 2 * MARGIN
    spanY = COORD_H - 2 * MARGIN
    scale = min(spanX / wide, spanY / high) if wide and high else 1.0
    # One baseline for the group, from the group's own top edge.
    top = max(ys)
    # Centred vertically, as a GROUP rather than glyph by glyph. Aspect is
    # preserved, so whichever axis is not the limiting one leaves slack: a
    # word is wide and short and only fills about half the height. Pinning it
    # to the top edge left it visibly high in the canvas. Centring the group
    # rather than each glyph is what keeps the baseline shared across an
    # alphabet, which is the thing that matters when paging through one.
    oy = MARGIN + (spanY - high * scale) / 2.0

    out = []
    for label, strokes in items:
        gx = [x for s in strokes for x, _ in s]
        left = min(gx)
        width = max(gx) - left
        ox = MARGIN + (spanX - width * scale) / 2.0    # centred horizontally
        placed = []
        for s in strokes:
            pts, last = [], None
            for x, y in s:
                p = (int(round(ox + (x - left) * scale)),
                     int(round(oy + (top - y) * scale)))
                if last is None or math.dist(p, last) >= min_step:
                    pts.append(p)
                    last = p
            # The end of a stroke is where the pen stops; never decimate it away.
            end = (int(round(ox + (s[-1][0] - left) * scale)),
                   int(round(oy + (top - s[-1][1]) * scale)))
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
            if not (0 <= x <= COORD_W and 0 <= y <= COORD_H):
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
    reason: adding a word to KID_WORDS should not require anybody to remember to
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
        '/* The box these coordinates live in. Not square: see COORD_W in',
        ' * tools/gen_cursive_glyphs.py. The game hands these to',
        ' * LetterTracer::configure() so the mapping stays uniform. */',
        'constexpr int16_t CURSIVE_COORD_W = %d;' % COORD_W,
        'constexpr int16_t CURSIVE_COORD_H = %d;' % COORD_H,
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
    # CELLS AT THE DEVICE'S ASPECT, which is the whole reason the last round
    # of flatness got past this sheet. The cells were square, so a 200x156 box
    # was drawn 200x200 here and every glyph looked 28% taller on this sheet
    # than on the panel -- the sheet said the words were fine and the panel
    # disagreed. A preview that does not share the target's proportions is not
    # a preview.
    cols = 13
    rows = (len(items) + cols - 1) // cols
    cellW = 104
    cellH = int(round(cellW * COORD_H / COORD_W))
    im = Image.new('RGB', (cols * cellW, rows * cellH), (250, 248, 244))
    d = ImageDraw.Draw(im)
    for i, (label, strokes) in enumerate(items):
        ox, oy = (i % cols) * cellW, (i // cols) * cellH
        d.rectangle([ox, oy, ox + cellW - 1, oy + cellH - 1], outline=(214, 208, 198))
        for si, pts in enumerate(strokes):
            shade = [(24, 40, 96), (176, 64, 32), (32, 120, 60),
                     (140, 40, 140), (180, 140, 20), (60, 60, 60)][si % 6]
            xy = [(ox + 2 + x * (cellW - 4) / COORD_W,
                   oy + 2 + y * (cellH - 4) / COORD_H) for x, y in pts]
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
    # Words share one scale too, for the same reason and more strongly. They
    # were normalised one at a time to win back some size, and that is what put
    # 'way' on screen at half the x-height of 'dog' -- a word with no ascender
    # and no descender is wide and short, so fitting it to the box on its own
    # blows it up horizontally and leaves it flat. One scale and one baseline
    # for the whole set is what a handwriting workbook does, and it is why the
    # word list has to stay narrow: see WORD_WIDTH_CAP.
    letters = normalise(upper) + normalise(lower)

    chosen, too_wide = [], []
    for w in KID_WORDS:
        strokes = word_strokes(gs, cmap, w)
        xs = [x for st in strokes for x, _ in st]
        width = max(xs) - min(xs) if xs else 0
        if width > WORD_WIDTH_CAP:
            too_wide.append((w, width))
        chosen.append((w, strokes))
    if too_wide:
        for w, width in too_wide:
            sys.stderr.write("'%s' is %d font units wide; WORD_WIDTH_CAP is %d\n"
                             % (w, width, WORD_WIDTH_CAP))
        sys.stderr.write('nothing written.\n')
        return 1
    missing = sorted(set('abcdefghijklmnopqrstuvwxyz') -
                     set(c for w in KID_WORDS for c in w))
    print('         %d words; no word contains: %s'
          % (len(chosen), ' '.join(missing) or 'nothing'))

    order = list(chosen)
    random.Random(SHUFFLE_SEED).shuffle(order)
    words = normalise(order, WORD_MIN_STEP)

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
    if words:
        widest = max(max(x for st in ss for x, _ in st) -
                     min(x for st in ss for x, _ in st) for _, ss in words)
        print('         widest word %d of %d box units' % (widest, COORD_W))
        # x-height in device pixels, which is the number that decides whether
        # a child can trace it. LetterTracer maps this box 1:1 when its canvas
        # has the same shape.
        tall = [max(y for st in ss for _, y in st) -
                min(y for st in ss for _, y in st) for _, ss in words]
        print('         word ink height %d..%d px of %d' % (min(tall), max(tall), COORD_H))
    preview(letters, words)
    print('\nLOOK AT THE PREVIEW SHEET. Green dot = stroke start; each stroke')
    print('is a different colour, in writing order.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
