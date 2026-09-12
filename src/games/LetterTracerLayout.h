#pragma once

#include "ui/Ui.h"

/* LetterTracer's screen geometry, shared by its two translation units.
 *
 * LetterTracer.cpp (the tracing logic) and LetterTracerDraw.cpp (the
 * painting) both need the canvas and the control columns, and a rectangle
 * stated twice is a rectangle that will one day be stated two ways. Private
 * to the tracer: nothing outside src/games/LetterTracer*.cpp includes it.
 */
namespace LetterTracerLayout {

/* EVERY CONTROL LIVES IN A SIDE COLUMN, AND NONE ABOVE OR BELOW THE CANVAS.
 *
 * They used to sit in a strip 4px above the tracing area and another 12px
 * below it. A child tracing the top of a letter runs a finger straight off the
 * top edge into the mode tabs and lands on a different alphabet mid-stroke;
 * the same happens at the bottom with Prev and Next. The buttons were sitting
 * in the natural overshoot of the gesture the game exists to teach.
 *
 * THE COLUMNS ARE AS NARROW AS THE LABELS ALLOW, because everything they do
 * not use belongs to the letter. They were 68px wide with 26px buttons, which
 * left a 164px canvas -- fine for a single letter and cramped for a joined
 * word, where the whole point is the run across the page. At 52px they still
 * hold "Words" at font 1 and the canvas grows to 200x162, a fifth more area
 * and most of it in the direction a word needs. Do not shrink them further
 * without checking "Words" still fits and the targets are still finger-sized:
 * 52x22 with TOUCH_HIT_SLOP is about the floor on a resistive panel. */
constexpr int16_t COL_W = 52;
constexpr int16_t BTN_H = 22;
constexpr int16_t COL_L_X = 4;
constexpr int16_t COL_R_X = 264;
constexpr int16_t SET_Y = 52;
constexpr int16_t SET_STEP = 26;
/* Four tabs, since Trace gained Words: 52, 78, 104 and 130. Prev sits a clear
 * gap below the last of them in EVERY tracing game, including Cursive with its
 * three -- a control that moves depending on which game you opened is a
 * control you have to look for. */
constexpr int16_t PREV_Y = 168;
constexpr Rect PREV_BTN{COL_L_X, PREV_Y, COL_W, BTN_H};
static_assert(SET_Y + 3 * SET_STEP + BTN_H < PREV_Y,
              "the fourth tab would run into Prev");
constexpr Rect RETRY_BTN{COL_R_X, 52, COL_W, BTN_H};
constexpr Rect NEXT_BTN{COL_R_X, 78, COL_W, BTN_H};

/* The word or letter, spelled out in ordinary type above the canvas.
 *
 * There was no such thing before -- only a font-1 watermark behind the dots,
 * which at word sizes was illegible, so a child tracing 'quiz' had no way to
 * know that was the word. A label is not a decoration here: the whole task is
 * "write this", and the child has to be able to read what "this" is. */
constexpr int16_t CAPTION_Y = 31;
constexpr int16_t CAPTION_H = 19;

constexpr int16_t DRAW_X = 60;
constexpr int16_t DRAW_Y = 52;
constexpr int16_t DRAW_W = 200;
/* 156 and not 162: a glyph's coordinates run to COORD_MAX exactly, so its
 * lowest point lands on DRAW_Y + DRAW_H, and the start ring drawn on it is a
 * 6px circle. At 162 the 7px badge it replaced touched the progress bar. */
constexpr int16_t DRAW_H = 156;
constexpr int16_t HIT_RADIUS = 16;
constexpr uint32_t PULSE_PERIOD_MS = 500;
constexpr int16_t BAR_Y = 220;

/* Dot sizes, and they are small on purpose.
 *
 * They were radius 3 for a waypoint and 4 or 6 for the next one, which at a
 * single letter's 20px spacing was fine and at a word's 12px spacing merged
 * the letters into a chain of blobs -- reported from the device as not being
 * able to tell that the word was 'quiz'. At radius 2 the shape shows through
 * between the dots.
 *
 * The next dot pulses by CHANGING COLOUR AND NOT SIZE, which is also what
 * makes the incremental repaint below possible: a dot that never grows never
 * has to be erased, so a frame can overdraw it and touch nothing else. */
constexpr int16_t DOT_R = 2;
constexpr int16_t NEXT_R = 3;

/* THE DIRECTION ARROWS, AND WHY THEY ARE BESIDE THE LINE AND NOT ON IT.
 *
 * They used to sit on the path itself, one at a time, just past whichever turn
 * came next, and jump forward as the finger reached it. User testing with
 * five-year-olds said plainly that this confused them: an arrow on the line
 * covers the dots it is pointing along, and one that moves is one more thing
 * on the screen changing while they concentrate.
 *
 * So they are drawn the way every handwriting workbook draws them: a short
 * numbered arrow BESIDE each stroke, outside the letter, all of them visible
 * from the start and none of them moving. See LetterTracerArrows.cpp for how
 * "beside" and "outside" are decided. */
constexpr int16_t ARROW_LEN = 16;        // tail to tip
constexpr int16_t ARROW_HEAD = 5;        // length of the head
constexpr int16_t ARROW_HALF = 4;        // half the head's width
/* How far the arrow stands off the stroke. The nearest reads most clearly as
 * belonging to its stroke; the others are for when it hits something. The
 * widest exists for a short stroke with something poking past it -- the
 * crossbar of a printed t, whose stem stands 9px above it in a word. */
constexpr int16_t ARROW_OFFSETS[] = {9, 12, 15};
/* The stroke number sits this far beyond the arrow's tail, outwards. */
constexpr int16_t ARROW_LABEL_BACK = 3;
constexpr int16_t ARROW_LABEL_OUT = 7;
/* Nothing an arrow draws may come closer than this to any stroke. */
constexpr int16_t ARROW_CLEAR = 5;
/* The ring on the first dot of the stroke being traced -- "start here". */
constexpr int16_t START_RING_R = 6;
/* How far the moving guide arrow stands off the dot it belongs to. Closer
 * than the numbered arrows, because it has to read as belonging to that dot
 * rather than to the letter. */
constexpr int16_t GUIDE_OFFSET = 8;
/* And it is not drawn at all if it cannot keep this much daylight from the
 * strokes. Lower than ARROW_CLEAR: the guide is worth a tighter fit, since
 * it is the answer to "which way now". */
constexpr float GUIDE_CLEAR = 3.0f;
/* Where arrows may go: the canvas, less a margin so a head does not poke into
 * the caption above or the progress bar below. */
constexpr int16_t ARROW_MIN_X = DRAW_X - 2;
constexpr int16_t ARROW_MAX_X = DRAW_X + DRAW_W + 2;
constexpr int16_t ARROW_MIN_Y = DRAW_Y + 3;
constexpr int16_t ARROW_MAX_Y = BAR_Y - 7;
/* A turn earns an arrow of its own only when it is a REVERSAL -- more than
 * about 100 degrees, like the top of an A or each point of an M. A right
 * angle, like the corner of an L, is left to the dots: they show the way round
 * it on their own, and every extra arrow is one more thing for a child to
 * read. Measured on the authored vertices, never the resampled dots, because
 * the resampler can land either side of an apex and split one sharp turn into
 * two gentle ones. */
constexpr float TURN_COS = -0.2f;

inline Rect setTabRect(uint8_t i) {
    return Rect{COL_L_X, static_cast<int16_t>(SET_Y + i * SET_STEP), COL_W, BTN_H};
}

}   // namespace LetterTracerLayout
