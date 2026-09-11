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
constexpr Rect PREV_BTN{COL_L_X, 142, COL_W, BTN_H};
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
 * lowest point lands on DRAW_Y + DRAW_H, and the numbered badge drawn on it
 * is a 7px circle. At 162 that circle touched the progress bar. */
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

/* HOW SHARP A TURN HAS TO BE TO EARN AN ARROW.
 *
 * A waypoint on a gentle curve turns by roughly step/radius radians -- at 10px
 * spacing, a 50px radius bends 11 degrees per dot and wants no arrow, while
 * the 15px radius at the bottom of a cursive undercurve bends nearly 40. So
 * the threshold separates "keep going round" from "now go the other way",
 * which is exactly the distinction a child needs pointing out.
 *
 * Too low and a curve sprouts an arrow every few dots; too high and the sharp
 * turn inside a cursive 'k' gets nothing. */
constexpr float CORNER_COS = 0.70f;      // ~46 degrees
/* And a turn cannot be marked within this many dots of the last one.
 *
 * Without it a tight curve fires on three or four consecutive waypoints,
 * because each of them individually bends past the threshold. Measured on the
 * real tables: cursive 'o' produced seven arrows in eighteen dots and print
 * 'S' four in twenty-two, which marks "you are on a curve" rather than "now
 * turn". With a three-dot gap the same letters get two and two, and print 'A'
 * gets exactly the three that matter -- the start, the apex, and the
 * crossbar. */
constexpr uint8_t CORNER_GAP = 3;
constexpr int16_t ARROW_LEN = 11;        // tip, measured from the waypoint
constexpr int16_t ARROW_HALF = 4;        // half the base width

inline Rect setTabRect(uint8_t i) {
    return Rect{COL_L_X, static_cast<int16_t>(SET_Y + i * SET_STEP), COL_W, BTN_H};
}

}   // namespace LetterTracerLayout
