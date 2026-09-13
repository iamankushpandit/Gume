// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ChessGame.h"

#include <string.h>

/* Remembering a game. Written after every move and on the way out; see the
 * Saved struct in ChessGame.h for what is kept and why the layout is fixed. */

void ChessGame::saveGame(AppContext& host) const {
    Saved out{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    memcpy(out.sq, pos_.sq, sizeof(out.sq));
    out.whiteToMove = pos_.whiteToMove ? 1 : 0;
    for (uint8_t i = 0; i < 4; ++i) out.castle[i] = pos_.castle[i] ? 1 : 0;
    out.epSquare = pos_.epSquare;
    out.halfmove = pos_.halfmove;
    out.status = static_cast<uint8_t>(status_);
    out.mode = static_cast<uint8_t>(mode_);
    out.remoteIsWhite = remoteIsWhite_ ? 1 : 0;
    out.endedByUs = endedByUs_ ? 1 : 0;
    memcpy(out.opponent, opponent_, sizeof(out.opponent));
    memcpy(out.opponentName, opponentName_, sizeof(out.opponentName));
    out.session = session_;
    out.ourPly = ourPly_;
    out.theirPly = theirPly_;
    out.ourFrom = ourFrom_;
    out.ourTo = ourTo_;
    out.level = static_cast<uint8_t>(level_);
    out.sideChoice = static_cast<uint8_t>(sideChoice_);
    out.humanIsWhite = humanIsWhite_ ? 1 : 0;
    out.takenCount[0] = takenCount_[0];
    out.takenCount[1] = takenCount_[1];
    memcpy(out.taken, taken_, sizeof(out.taken));
    host.saveBlob("game", &out, sizeof(out));
}

bool ChessGame::restoreGame(AppContext& host) {
    Saved in{};
    host.loadBlob("game", &in, sizeof(in));
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) return false;

    /* Only a game that is still going is worth coming back to. A finished one
     * is restored as nothing, so the screen opens at the lobby rather than at
     * a checkmate somebody already read. */
    /* The chips come back whatever else does. They are settings rather than
     * part of a game, so a finished or unrestorable board should still leave
     * the lobby saying what it said last time. Restored BEFORE any of the
     * early returns below, deliberately. */
    if (in.level <= static_cast<uint8_t>(Ch::Level::Medium)) {
        level_ = static_cast<Ch::Level>(in.level);
    }
    if (in.sideChoice <= static_cast<uint8_t>(Side::Random)) {
        sideChoice_ = static_cast<Side>(in.sideChoice);
    }

    const Mode m = static_cast<Mode>(in.mode);
    if (m != Mode::Local && m != Mode::Computer && m != Mode::Remote) {
        return false;
    }
    /* Only an unfinished game is worth coming back to. Every terminal status
     * is listed rather than tested for "not Playing", so adding a new way for
     * a game to end forces a decision here instead of silently restoring a
     * finished board. */
    const Status st = static_cast<Status>(in.status);
    if (st == Status::Checkmate || st == Status::Stalemate ||
        st == Status::DrawMaterial || st == Status::DrawFifty ||
        st == Status::Ended) {
        return false;
    }
    if (in.takenCount[0] > MAX_TAKEN || in.takenCount[1] > MAX_TAKEN) {
        return false;
    }

    memcpy(pos_.sq, in.sq, sizeof(pos_.sq));
    pos_.whiteToMove = in.whiteToMove != 0;
    for (uint8_t i = 0; i < 4; ++i) pos_.castle[i] = in.castle[i] != 0;
    pos_.epSquare = in.epSquare;
    pos_.halfmove = in.halfmove;

    /* Both kings, or it is not a chess position. This is the only validation
     * worth doing: the blob is our own NVS rather than anything that came off
     * the air, so the realistic failure is a layout change that slipped past
     * the length and version checks, not an attack. Restoring a board with no
     * king would divide by zero in kingSquare()'s callers' assumptions and
     * would be far harder to diagnose than starting over. */
    if (kingSquare(pos_, true) == NO_SQ || kingSquare(pos_, false) == NO_SQ) {
        return false;
    }

    mode_ = m;
    humanIsWhite_ = in.humanIsWhite != 0;
    remoteIsWhite_ = in.remoteIsWhite != 0;
    endedByUs_ = in.endedByUs != 0;
    memcpy(opponent_, in.opponent, sizeof(opponent_));
    opponent_[sizeof(opponent_) - 1] = 0;
    memcpy(opponentName_, in.opponentName, sizeof(opponentName_));
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = in.session;
    ourPly_ = in.ourPly;
    theirPly_ = in.theirPly;
    ourFrom_ = in.ourFrom;
    ourTo_ = in.ourTo;
    takenCount_[0] = in.takenCount[0];
    takenCount_[1] = in.takenCount[1];
    memcpy(taken_, in.taken, sizeof(taken_));

    /* Recomputed rather than restored. Check and mate are functions of the
     * position, so storing them would be storing a fact twice and inviting the
     * two copies to disagree -- and refreshStatus() is cheap enough to run
     * once on the way in. */
    refreshStatus();
    /* A computer to move comes back thinking. Without this the board restores
     * with the computer's move owed and nothing armed to make it, and the game
     * sits there until the player taps something -- which they cannot, because
     * it is not their turn. Go restores mid-think for the same reason. */
    if (mode_ == Mode::Computer && !gameOver() && !humanTurn()) {
        beginThinking();
    }
    return true;
}
