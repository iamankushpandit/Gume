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

#include "ChessInternal.h"

/* Remembering a game. Written after every move and on the way out; see the
 * Saved struct in ChessGame.h for what is kept and why the layout is fixed.
 *
 * There are two places, and which one is decided by who the game belongs to.
 * A game against the computer, or across this one console, is the player's:
 * it goes in their own slot with the lobby's choices, and Guest's is dropped
 * like everything else of Guest's. A game against a nearby console is the
 * CONSOLE's: the other console is playing this one, whoever picks it up, so
 * it is kept apart from every player -- it survives a change of player and a
 * power cut under Guest, and it never writes over anybody's own game. */

void ChessGame::saveGame(AppContext& host) const {
    /* An invitation nobody has answered is not a game yet. */
    if (mode_ == Mode::Waiting) return;

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
    if (mode_ == Mode::Remote) {
        host.saveConsoleBlob(CHESS_SAVE_KEY, &out, sizeof(out));
    } else {
        host.saveBlob(CHESS_SAVE_KEY, &out, sizeof(out));
    }
}

bool ChessGame::readable(const Saved& in) {
    return in.magic == SAVE_MAGIC && in.version == SAVE_VERSION;
}

/* Only an unfinished game is worth coming back to. A finished one is restored
 * as nothing, so the screen opens at the lobby rather than at a checkmate
 * somebody already read. */
bool ChessGame::inPlay(const Saved& in) {
    const Mode m = static_cast<Mode>(in.mode);
    if (m != Mode::Local && m != Mode::Computer && m != Mode::Remote) {
        return false;
    }
    /* A game across consoles with nobody named at the other end can be
     * neither played nor ended properly. */
    if (m == Mode::Remote && in.opponent[0] == 0) return false;
    /* Every terminal status is listed rather than tested for "not Playing",
     * so adding a new way for a game to end forces a decision here instead of
     * silently restoring a finished board. */
    const Status st = static_cast<Status>(in.status);
    if (st == Status::Checkmate || st == Status::Stalemate ||
        st == Status::DrawMaterial || st == Status::DrawFifty ||
        st == Status::Ended) {
        return false;
    }
    if (in.takenCount[0] > MAX_TAKEN || in.takenCount[1] > MAX_TAKEN) {
        return false;
    }
    /* Both kings, or it is not a chess position. This is the only validation
     * worth doing: the blob is our own NVS rather than anything that came off
     * the air, so the realistic failure is a layout change that slipped past
     * the length and version checks, not an attack. Restoring a board with no
     * king would divide by zero in kingSquare()'s callers' assumptions and
     * would be far harder to diagnose than starting over. */
    bool white = false;
    bool black = false;
    for (int8_t piece : in.sq) {
        if (piece == KING) white = true;
        if (piece == -KING) black = true;
    }
    return white && black;
}

/* The chips. They are settings rather than part of a game, so a finished or
 * unrestorable board should still leave the lobby saying what it said last
 * time. */
void ChessGame::takeChoices(const Saved& in) {
    if (in.level <= static_cast<uint8_t>(Ch::Level::Medium)) {
        level_ = static_cast<Ch::Level>(in.level);
    }
    if (in.sideChoice <= static_cast<uint8_t>(Side::Random)) {
        sideChoice_ = static_cast<Side>(in.sideChoice);
    }
}

/* Put a saved game on the board. The caller has asked inPlay() first. */
void ChessGame::takeGame(const Saved& in) {
    memcpy(pos_.sq, in.sq, sizeof(pos_.sq));
    pos_.whiteToMove = in.whiteToMove != 0;
    for (uint8_t i = 0; i < 4; ++i) pos_.castle[i] = in.castle[i] != 0;
    pos_.epSquare = in.epSquare;
    pos_.halfmove = in.halfmove;

    mode_ = static_cast<Mode>(in.mode);
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
    panelStale_ = true;

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
}

/* What the active player has of their own. False when there is nothing. */
bool ChessGame::loadPlayerSave(AppContext& host, Saved& in) const {
    in = Saved{};
    host.loadBlob(CHESS_SAVE_KEY, &in, sizeof(in));
    if (!readable(in)) {
        /* Nothing under this game's own name: look once under the name all
         * five nearby games used to share. Whatever is there is taken only if
         * it is a Chess save -- another game's has another size or another
         * magic -- and it goes under the new name the first time this game
         * saves. */
        in = Saved{};
        host.loadBlob(CHESS_LEGACY_KEY, &in, sizeof(in));
        if (!readable(in)) return false;
    }
    /* A game across consoles is never the player's. The old name could hold
     * one; the console it was against stopped waiting for it long ago. */
    return static_cast<Mode>(in.mode) != Mode::Remote;
}

/* The player's own save: the lobby's choices always, and their unfinished
 * game when `resume` says to put it on the board. Left off the board it is
 * held instead: the lobby offers it, and nothing is written over it. */
void ChessGame::openPlayerSave(AppContext& host, bool resume) {
    heldMode_ = Mode::Lobby;
    Saved mine{};
    if (!loadPlayerSave(host, mine)) return;
    takeChoices(mine);
    if (!inPlay(mine)) return;
    if (resume) {
        takeGame(mine);
    } else {
        heldMode_ = static_cast<Mode>(mine.mode);
    }
}

/* What begin() finds when this console is not already in a nearby game. The
 * order is the rule: the console's game first, because somebody else is
 * waiting on it; then the player's own. */
void ChessGame::openSaved(AppContext& host) {
    Saved kept{};
    host.loadConsoleBlob(CHESS_SAVE_KEY, &kept, sizeof(kept));
    const bool nearby = readable(kept) &&
                        static_cast<Mode>(kept.mode) == Mode::Remote && inPlay(kept);
    if (nearby) {
        /* Still being played: the frame loop says its turn again from what
         * was just restored, and the other console need never know this one
         * was away. */
        takeGame(kept);
        farewellUntilMs_ = 0;
        openPlayerSave(host, false);   // for the lobby's choices only
        return;
    }
    /* Over, or not a game this build can read: finished with. */
    host.clearConsoleBlob(CHESS_SAVE_KEY);

    /* No nearby game to show, so nothing of this game's may stay on the air:
     * an invitation nobody is waiting on any more, or a move from a game this
     * screen can no longer bring back. The one exception is the last word of
     * a game just finished, which tickFarewell() is still looking after. */
    if (farewellUntilMs_ == 0) host.nearbyStop();

    /* Somebody is asking this console for a game of chess. The player's own
     * game would cover the only screen that can say so, so the lobby is shown
     * in front of it. Nothing is saved or cleared for that: the game is still
     * theirs, one row away. */
    NearbySeat asking;
    openPlayerSave(host, !inviterNow(host, nullptr, asking));
}

/* Leaving a game in memory as well as in NVS. Guest's saves are dropped, so
 * for Guest this is the only way a game survives Home at all.
 *
 * Kept quietly, because End game is right there on the panel. Not kept: the
 * lobby, an invitation still waiting for an answer, a game that is over, and
 * a board nobody has moved on yet -- coming back to that is the same
 * as starting again. A nearby game is kept even from the first position,
 * because the other console is already in it. */
Game::Keep ChessGame::keepsGame() const {
    if (mode_ != Mode::Local && mode_ != Mode::Computer && mode_ != Mode::Remote) {
        return Keep::No;
    }
    if (gameOver()) return Keep::No;
    if (mode_ != Mode::Remote && pos_.whiteToMove) {
        bool untouched = true;
        for (int8_t f = 0; f < 8 && untouched; ++f) {
            untouched = pos_.sq[Ch::idx(f, 0)] == Ch::BACK_RANK[f] &&
                        pos_.sq[Ch::idx(f, 1)] == PAWN &&
                        pos_.sq[Ch::idx(f, 6)] == -PAWN &&
                        pos_.sq[Ch::idx(f, 7)] == -Ch::BACK_RANK[f];
        }
        if (untouched) return Keep::No;
    }
    return Keep::Quietly;
}

/* The board is exactly as end() left it; end() only wrote it out and, away
 * from a nearby game, gave the radio back, which nothing here needs again. A
 * nearby move was left on the air and update() goes on republishing it.
 * The computer's pause for thought starts again where it was, rather than
 * having run out while nobody was looking, and a half-pressed End game is
 * closed: a "Sure?" waiting for the first tap after a break is a game ended
 * by accident. */
void ChessGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    if (thinking_) movedOn(thinkUntilMs_, awayMs);
    confirmUntilMs_ = 0;
}
