// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "GoGame.h"

/* What Go's drawing files share: the constants every rectangle is derived
 * from, and the board's own colours. Internal to GoLayout.cpp, GoDraw.cpp and
 * GoPanel.cpp; nothing else includes it. */

namespace GoArt {

// ---- the board: every rectangle below is derived from these ----------------
inline constexpr int16_t BX = 5;
inline constexpr int16_t BY = 34;
inline constexpr int16_t STEP_SMALL = 23;    // 9x9: eight gaps
inline constexpr int16_t MARGIN_SMALL = 9;
inline constexpr int16_t STEP_BIG = 10;      // 19x19: eighteen gaps
inline constexpr int16_t MARGIN_BIG = 6;
inline constexpr int16_t PANEL_GAP = 4;
inline constexpr int16_t BOX_SMALL = 8 * STEP_SMALL + 2 * MARGIN_SMALL;
inline constexpr int16_t BOX_BIG = 18 * STEP_BIG + 2 * MARGIN_BIG;
static_assert(BY + BOX_SMALL <= GAME_CANVAS_HEIGHT, "the 9x9 board falls off the canvas");
static_assert(BY + BOX_BIG <= GAME_CANVAS_HEIGHT, "the 19x19 board falls off the canvas");
static_assert(GAME_CANVAS_WIDTH - (BX + BOX_SMALL + PANEL_GAP) - 4 >= 100,
              "the 9x9 panel is too narrow for Captured");
static_assert(GAME_CANVAS_WIDTH - (BX + BOX_BIG + PANEL_GAP) - 4 >= 100,
              "the 19x19 panel is too narrow for the magnifier");

/* The 9x9 panel, top to bottom. */
inline constexpr int16_t TURN_Y = 34, TURN_H = 22;
inline constexpr int16_t CAPT_Y = 60, CAPT_H = 32;
inline constexpr int16_t INFO_Y = 96, INFO_H = 34;
/* The info box's message: up to two lines of font 1 (8px tall), 12px apart,
 * inside the box's 1px border with daylight above and below. */
inline constexpr uint8_t INFO_LINES = 2;
inline constexpr int16_t INFO_FONT_H = 8, INFO_LINE_H = 12, INFO_PAD_X = 6;
static_assert((INFO_LINES - 1) * INFO_LINE_H + INFO_FONT_H <= INFO_H - 2 * 4,
              "the info box's two lines do not fit inside its border");
inline constexpr int16_t PASS_Y = 136, PASS_H = 26;
inline constexpr int16_t UNDO_Y = 166, UNDO_H = 24;
inline constexpr int16_t ACTION_Y = 194, ACTION_H = 26;
static_assert(ACTION_Y + ACTION_H + Ui::BUTTON_SHADOW_DY <= GAME_CANVAS_HEIGHT,
              "End's shadow falls off the canvas");

/* The 19x19 panel: the magnifier takes the captures' and the info box's
 * room, Place is a button of its own, Pass and Undo share a row. */
inline constexpr int16_t BTURN_Y = 34, BTURN_H = 18;
inline constexpr int16_t MAG_Y = 56, MAG_CELL = 18, MAG_CELLS = 5, MAG_H = MAG_CELL * MAG_CELLS;
inline constexpr int16_t PLACE_Y = 150, PLACE_H = 22;
inline constexpr int16_t BPASS_Y = 176, BPASS_H = 20;
inline constexpr int16_t BACTION_Y = 200, BACTION_H = 22;
static_assert(MAG_Y + MAG_H <= PLACE_Y, "the magnifier runs into Place");
static_assert(BACTION_Y + BACTION_H + Ui::BUTTON_SHADOW_DY <= GAME_CANVAS_HEIGHT,
              "End's shadow falls off the canvas on 19x19");

/* The lobby. */
inline constexpr int16_t CHIP_Y = 36, CHIP_H = 26;
inline constexpr int16_t ROW_Y = 70, ROW_H = 31, ROW_PITCH = 36;

/* The result card, over the board. */
inline constexpr Rect RESULT_RECT{30, 46, 260, 176};

// ---- colours: board art, fixed in every theme like Chess's squares ----------
inline uint16_t wood()      { return Ui::rgb(196, 152, 88); }
inline uint16_t grid()      { return Ui::rgb(92, 66, 34); }
inline uint16_t blackFill() { return Ui::rgb(38, 38, 48); }
inline uint16_t blackEdge() { return Ui::rgb(96, 96, 112); }
inline uint16_t whiteFill() { return Ui::rgb(244, 241, 232); }
inline uint16_t whiteEdge() { return Ui::rgb(150, 145, 132); }
inline uint16_t marker()    { return Ui::rgb(247, 61, 82); }
/* The ghost's ring: cyan, which is none of the board's own colours. */
inline uint16_t hilite()    { return Ui::rgb(120, 230, 255); }

inline uint16_t mix(uint16_t a, uint16_t b) {
    /* Halfway between two RGB565 colours, channel by channel. */
    const uint16_t r = static_cast<uint16_t>((((a >> 11) & 0x1F) + ((b >> 11) & 0x1F)) / 2);
    const uint16_t g = static_cast<uint16_t>((((a >> 5) & 0x3F) + ((b >> 5) & 0x3F)) / 2);
    const uint16_t bl = static_cast<uint16_t>(((a & 0x1F) + (b & 0x1F)) / 2);
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

inline constexpr char COLS[] = "ABCDEFGHJKLMNOPQRST";   // no I, as on a real board

}   // namespace GoArt
