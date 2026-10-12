// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "hal/TouchTypes.h"
#include "ui/Ui.h"

/* NumberNest's screen geometry, shared by its two translation units.
 *
 * NumberNestGame.cpp (which turns a touch into an event) and
 * NumberNestDraw.cpp (which puts a Nest::View on the panel) both need every
 * rectangle here, and a rectangle stated twice is one that will be stated two
 * ways: the hit test and the painter of a button must read the same Rect.
 * Private to the screen -- nothing outside src/games/NumberNest*.cpp includes
 * it, and the rules know nothing about pixels.
 *
 * THE LAYOUT, WRITTEN ONCE. Canvas units, on the fixed 320x240 landscape
 * canvas every catalogue game draws on; the runtime scales positions and
 * radii up on a larger panel and leaves text at its own size. Everything
 * below is derived from the row above it, and every rectangle that is ever
 * cleared is one of these -- never a number typed in beside a painter. Rows
 * keep a few pixels of daylight because on a scaled panel positions and sizes
 * round independently.
 *
 * Nothing starts above y = 40: the top bar is 30 pixels, and the runtime takes
 * taps below it before this screen sees them -- down to y = 38 for Home and
 * to 32 for Lock, Mute and the gear, each widened by the touch slop.
 *
 * Buttons that sit side by side are two slops apart. Every button is hit
 * tested with the slop round it, so at any smaller gap the first one tested
 * would own the leading edge of the second: at six pixels apart, the two
 * leftmost columns of Multiply selected Add. */
namespace NumberNestLayout {

constexpr int16_t EDGE = 8;
constexpr int16_t ROW_GAP = 4;

constexpr int16_t CONTROL_Y = 40;
constexpr int16_t CONTROL_H = 24;
constexpr Rect ADD_RECT{EDGE, CONTROL_Y, 64, CONTROL_H};
constexpr Rect MUL_RECT{ADD_RECT.x + ADD_RECT.w + 2 * TOUCH_HIT_SLOP, CONTROL_Y, 96,
                        CONTROL_H};
constexpr Rect LEVEL_RECT{GAME_CANVAS_WIDTH - EDGE - 80, CONTROL_Y, 80, CONTROL_H};

constexpr int16_t STATUS_Y = CONTROL_Y + CONTROL_H + ROW_GAP;
constexpr int16_t STATUS_H = 18;
constexpr Rect STATUS_RECT{EDGE, STATUS_Y, GAME_CANVAS_WIDTH - 2 * EDGE, STATUS_H};
/* Feedback is written on a filled pill, not in coloured text on the ground.
 * The success and error roles are checked for contrast as FILLS against the
 * background, not as ink on it: on Silver and Pocket a line of text in the
 * success colour all but disappears, which the theme sheets showed before a
 * panel did. A fill with Ui::onFill() ink can be read on all nine. Wide
 * enough for the longest message at font 2: "Nest complete! 255 slips on the
 * way" is 225 pixels, measured with the library's own width table. */
constexpr Rect STATUS_PILL{(GAME_CANVAS_WIDTH - 240) / 2, STATUS_Y, 240, STATUS_H};

/* Five circles across the full width. Radius 28 with a two-pixel rim is what
 * holds three digits at font 4: the opaque text cell is 42 wide and 26 tall,
 * so its corner is 24.7 from the centre, inside the fill's 26. */
constexpr int16_t NUMBER_R = 28;
constexpr int16_t NUMBER_RIM = 2;
constexpr int16_t NUMBER_PITCH = 64;
constexpr int16_t NUMBER_CX0 = 32;
constexpr int16_t NUMBER_CY = STATUS_Y + STATUS_H + ROW_GAP + NUMBER_R;
/* How far outside its circle a press still counts. LESS than half the gap
 * between two neighbours, so that not even the point midway between them
 * belongs to both. */
constexpr int16_t NUMBER_SLOP = 3;

constexpr int16_t CAPTION_Y = NUMBER_CY + NUMBER_R + ROW_GAP + 1;
constexpr int16_t CAPTION_H = 12;
constexpr Rect CAPTION_RECT{EDGE, CAPTION_Y, GAME_CANVAS_WIDTH - 2 * EDGE, CAPTION_H};
/* The final group announces itself on a band in the warning colour, for the
 * reason the status pill exists: warning-coloured text on a light ground is
 * not there to be read. The longest caption is 270 pixels at font 1. */
constexpr Rect CAPTION_BAND{(GAME_CANVAS_WIDTH - 288) / 2, CAPTION_Y, 288, CAPTION_H};

/* The nest: a tray with five holes, each directly under the number circle
 * with the same index. That alignment is the point -- the final group is the
 * nest's own numbers, shown in the circles above in the same order. */
constexpr int16_t SLOT_R = 19;
constexpr int16_t SLOT_RIM = 1;
constexpr int16_t SLOT_CY = CAPTION_Y + CAPTION_H + ROW_GAP + SLOT_R;
constexpr Rect NEST_RECT{EDGE - 2, SLOT_CY - SLOT_R - 2, GAME_CANVAS_WIDTH - 2 * (EDGE - 2),
                         2 * SLOT_R + 4};

constexpr int16_t FOOTER_Y = SLOT_CY + SLOT_R + ROW_GAP + 1;
constexpr int16_t FOOTER_H = 26;
constexpr Rect RULES_RECT{EDGE, FOOTER_Y, 64, FOOTER_H};
constexpr Rect HOME_RECT{GAME_CANVAS_WIDTH - EDGE - 72, FOOTER_Y, 72, FOOTER_H};
constexpr Rect AGAIN_RECT{HOME_RECT.x - 2 * TOUCH_HIT_SLOP - 104, FOOTER_Y, 104, FOOTER_H};
/* What the footer's two end-of-nest buttons occupy, and so what is cleared
 * when they go: from a little left of the first to the right-hand edge. */
constexpr Rect FOOTER_CLEAR{AGAIN_RECT.x - 3, FOOTER_Y - 1,
                            GAME_CANVAS_WIDTH - (AGAIN_RECT.x - 3), FOOTER_H + 2};

/* The rules page's example discs: five to a row, between the operation's
 * name on the left and the equation on the right. */
constexpr int16_t EXAMPLE_R = 13;
constexpr int16_t EXAMPLE_PITCH = 28;
constexpr int16_t EXAMPLE_CX0 = 106;
constexpr int16_t EXAMPLE_TEXT_X = 240;
static_assert(EXAMPLE_PITCH > 2 * EXAMPLE_R &&
              EXAMPLE_CX0 + 4 * EXAMPLE_PITCH + EXAMPLE_R + 4 <= EXAMPLE_TEXT_X,
              "the example discs touch each other or run into their equation");

/* The rules page's one button. The whole page closes it, so this is where it
 * is drawn rather than where it is pressed. */
constexpr Rect PLAY_RECT{(GAME_CANVAS_WIDTH - 100) / 2, 208, 100, 26};

static_assert(ADD_RECT.x + ADD_RECT.w + 2 * TOUCH_HIT_SLOP <= MUL_RECT.x &&
              MUL_RECT.x + MUL_RECT.w + 2 * TOUCH_HIT_SLOP <= LEVEL_RECT.x &&
              RULES_RECT.x + RULES_RECT.w + 2 * TOUCH_HIT_SLOP <= AGAIN_RECT.x &&
              AGAIN_RECT.x + AGAIN_RECT.w + 2 * TOUCH_HIT_SLOP <= HOME_RECT.x,
              "two buttons are close enough that one press could belong to both");
static_assert(CONTROL_Y + CONTROL_H + TOUCH_HIT_SLOP <= NUMBER_CY - NUMBER_R - NUMBER_SLOP &&
              NUMBER_CY + NUMBER_R + NUMBER_SLOP <= FOOTER_Y - TOUCH_HIT_SLOP,
              "a press on a number could also belong to a button above or below it");
static_assert(NUMBER_CX0 - NUMBER_R >= 2 &&
              NUMBER_CX0 + 4 * NUMBER_PITCH + NUMBER_R <= GAME_CANVAS_WIDTH - 2,
              "the five number circles do not fit across the canvas");
static_assert(2 * (NUMBER_R + NUMBER_SLOP) < NUMBER_PITCH,
              "two number circles would both claim the same press");
static_assert(STATUS_PILL.x >= STATUS_RECT.x &&
              STATUS_PILL.x + STATUS_PILL.w <= STATUS_RECT.x + STATUS_RECT.w &&
              CAPTION_BAND.x >= CAPTION_RECT.x &&
              CAPTION_BAND.x + CAPTION_BAND.w <= CAPTION_RECT.x + CAPTION_RECT.w,
              "a pill is wider than the strip that clears it");
static_assert(STATUS_Y + STATUS_H + 2 <= NUMBER_CY - NUMBER_R,
              "clearing the status line would bite the number circles");
static_assert(CAPTION_Y >= NUMBER_CY + NUMBER_R + 2 &&
              CAPTION_Y + CAPTION_H + 1 <= NEST_RECT.y,
              "clearing the caption would bite the circles or the nest");
static_assert(FOOTER_CLEAR.y >= NEST_RECT.y + NEST_RECT.h + 1,
              "clearing the footer would bite the nest");
static_assert(RULES_RECT.x + RULES_RECT.w + 2 <= FOOTER_CLEAR.x,
              "clearing the footer would bite the Rules button");
static_assert(FOOTER_Y + FOOTER_H <= GAME_CANVAS_HEIGHT - 2 &&
              PLAY_RECT.y + PLAY_RECT.h <= GAME_CANVAS_HEIGHT - 2,
              "the footer runs off the bottom of the canvas");

constexpr int16_t numberCx(uint8_t index) {
    return static_cast<int16_t>(NUMBER_CX0 + index * NUMBER_PITCH);
}

/* Circles are hit-tested as circles: the corner of a circle's bounding box is
 * eleven pixels from the circle, and a press there is a miss. */
inline bool insideCircle(int16_t cx, int16_t cy, int16_t reach, const TouchPoint& touch) {
    const int32_t dx = static_cast<int32_t>(touch.x) - cx;
    const int32_t dy = static_cast<int32_t>(touch.y) - cy;
    return dx * dx + dy * dy <= static_cast<int32_t>(reach) * reach;
}

}  // namespace NumberNestLayout
