// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "Ui.h"

/* What a screen tells the d-pad's selector about its board.
 *
 * The selector finds its stops in what is drawn: a fill and an outline of one
 * rectangle. That is right for a button, which is pressable for as long as it
 * is on the panel, and wrong for a board, whose squares stay drawn while the
 * other player thinks. A game says here whether its board takes a press just
 * now; nothing is drawn or repainted for it.
 *
 *   Live    every square in the rectangle is a stop, however few there are.
 *           (Beside buttons the selector otherwise takes one to three plain
 *           squares for something to look at, which left a Ludo endgame with
 *           two tokens and a die unplayable from the keys.)
 *   Quiet   nothing in the rectangle is a stop: their turn, the computer
 *           thinking, a game that is over.
 *
 * A card put up over the board -- NearbyWatch's "Waiting for" -- is an
 * overlay: while one is up, only what is inside it is a stop. The header's
 * own controls are never affected. The runtime clears both on every screen
 * change, so a screen that says nothing gets the selector as it always was. */
namespace Ui {

enum class StopRule : uint8_t { Unstated, Live, Quiet };

/* `also` is a second rectangle under the same rule, for a board whose other
 * on-turn control stands apart from it -- Ludo's die, beside the board and
 * above a button that always works. */
void setBoardStops(const Rect& board, bool live, const Rect& also = Rect{0, 0, 0, 0});
void clearBoardStops();
void setOverlayStops(const Rect& overlay);
void clearOverlayStops();

/** The rule for a control whose middle is at (cx, cy), in screen coordinates. */
StopRule stopRuleAt(int16_t cx, int16_t cy);

}   // namespace Ui
