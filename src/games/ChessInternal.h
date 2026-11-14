// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* Shared by the Chess*.cpp files and nobody else. ChessGame.cpp was one file
 * until it passed the size the modularity rule allows; the handful of helpers
 * more than one of the pieces needs live here rather than being stated twice.
 * The public surface is ChessGame.h. */

#include "ChessGame.h"

/* idx(), fileOf(), rankOf() and BACK_RANK moved into ChessRules.h when the
 * rules were made pure -- the engine needs them and may not include a screen.
 * These declarations keep the call sites reading the way they always did,
 * which is why this file still exists rather than every .cpp growing a Ch::
 * prefix on arithmetic that was never ambiguous.
 *
 * Deliberately four names rather than `using namespace Ch;`. The whole point
 * of the namespace is that a file has to say what it is taking from it. */
using Ch::idx;
using Ch::fileOf;
using Ch::rankOf;
using Ch::BACK_RANK;

/* Where a game is kept. One name for both places it can be: the player's own
 * slot, for a game against the computer or across this one console and for
 * the lobby's choices, and the console's slot, for a game against a nearby
 * console -- see ChessSave.cpp. Chess used to keep its game under "game", and
 * so did four other games, each writing over the last; that name is still
 * read once, for a save made before this, and never written. */
constexpr char CHESS_SAVE_KEY[] = "chess_game";
constexpr char CHESS_LEGACY_KEY[] = "game";
