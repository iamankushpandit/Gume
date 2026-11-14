// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "BackgammonGame.h"

/* Remembering a game, like Chess: after every move, on Done and on the way
 * out, so a console put down comes back to the same position -- the dice
 * still to play, this turn's undo stack, and a nearby game's place in the
 * conversation included. Fixed layout: loadBlob() refuses a blob whose
 * length changed, and `version` covers one that kept its size. */

void BackgammonGame::saveGame(AppContext& host) const {
    Saved out{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    out.mode = static_cast<uint8_t>(mode_);
    out.phase = static_cast<uint8_t>(phase_);
    out.toMove = toMove_;
    out.winner = winner_;
    out.result = static_cast<uint8_t>(result_);
    out.pos = pos_;
    out.dice = dice_;
    out.rolled = rolled_;
    out.rolls = rolls_;
    out.seed = seed_;
    out.turnCount = turnCount_;
    for (uint8_t i = 0; i < Bg::MAX_DICE; ++i) {
        out.undoPos[i] = undoPos_[i];
        out.undoDice[i] = undoDice_[i];
        out.turnMoves[i] = turnMoves_[i];
        out.outbox[i] = outbox_[i];
    }
    out.ourWhite = ourWhite_ ? 1 : 0;
    memcpy(out.opponent, opponent_, sizeof(out.opponent));
    memcpy(out.opponentName, opponentName_, sizeof(out.opponentName));
    out.session = session_;
    out.applied = applied_;
    out.myPly = myPly_;
    out.myFrom = myFrom_;
    out.myTo = myTo_;
    out.outboxCount = outboxCount_;
    host.saveBlob("game", &out, sizeof(out));
}

bool BackgammonGame::restoreGame(AppContext& host) {
    Saved in{};
    host.loadBlob("game", &in, sizeof(in));
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) {
        return false;
    }
    /* Nothing to come back to from the lobby, and an invitation left waiting
     * has long since expired on the air. */
    const Mode mode = static_cast<Mode>(in.mode);
    if (mode != Mode::Local && mode != Mode::Computer && mode != Mode::Remote) {
        return false;
    }
    if (in.phase > static_cast<uint8_t>(Phase::Over) || in.toMove > Bg::BLACK ||
        in.winner > Bg::BLACK || in.result > static_cast<uint8_t>(Bg::Result::Backgammon) ||
        in.dice.count > Bg::MAX_DICE || in.rolled.count > Bg::MAX_DICE ||
        in.turnCount > Bg::MAX_DICE || in.outboxCount > Bg::MAX_DICE) {
        return false;
    }
    /* Refuse a position a game could not have reached rather than drawing
     * sixteen checkers. */
    if (Bg::checkerTotal(in.pos, Bg::WHITE) != Bg::CHECKERS ||
        Bg::checkerTotal(in.pos, Bg::BLACK) != Bg::CHECKERS) {
        return false;
    }
    if (static_cast<Phase>(in.phase) == Phase::Over &&
        !(mode == Mode::Remote && in.outboxCount > 0)) {
        /* A finished game is not worth reopening -- except a nearby one whose
         * winning moves had not all gone out: the other console is still
         * waiting for them and does not know it lost. */
        return false;
    }

    mode_ = mode;
    phase_ = static_cast<Phase>(in.phase);
    toMove_ = in.toMove;
    winner_ = in.winner;
    result_ = static_cast<Bg::Result>(in.result);
    pos_ = in.pos;
    dice_ = in.dice;
    rolled_ = in.rolled;
    rolls_ = in.rolls;
    seed_ = in.seed;
    turnCount_ = in.turnCount;
    for (uint8_t i = 0; i < Bg::MAX_DICE; ++i) {
        undoPos_[i] = in.undoPos[i];
        undoDice_[i] = in.undoDice[i];
        turnMoves_[i] = in.turnMoves[i];
        outbox_[i] = in.outbox[i];
    }
    ourWhite_ = in.ourWhite != 0;
    memcpy(opponent_, in.opponent, sizeof(opponent_));
    opponent_[sizeof(opponent_) - 1] = 0;
    memcpy(opponentName_, in.opponentName, sizeof(opponentName_));
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = in.session;
    applied_ = in.applied;
    myPly_ = in.myPly;
    myFrom_ = in.myFrom;
    myTo_ = in.myTo;
    outboxCount_ = in.outboxCount;
    ended_ = false;
    lastWordOnAir_ = false;
    away_ = false;
    lastAcked_ = false;         // not known until the other console says
    lastHeardMs_ = millis();    // and it gets a whole grace to say it in
    watch_.reset();
    pausePainted_ = false;
    selected_ = Bg::NO_POINT;
    planned_ = false;   // the computer re-plans from the position it finds
    timerMs_ = millis();
    if (phase_ == Phase::Over) {
        turnCount_ = 0;     // the board takes no more moves
        legalCount_ = 0;
    } else {
        refreshLegal();
    }
    setPhaseMessage();
    if (mode_ == Mode::Remote) {
        /* Say the same thing again: our word went when the screen was left. */
        host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
    }
    return true;
}

/* Leaving a game in memory as well as in NVS. Guest's saves are dropped, so
 * for Guest this is the only way a game survives Home at all.
 *
 * Kept quietly, because End is right there on the panel. Not kept: the
 * lobby, an invitation still waiting for an answer, a game somebody has won,
 * and a local game whose opening roll has not been thrown. A nearby game is
 * kept from the start, because the other console is already in it. */
Game::Keep BackgammonGame::keepsGame() const {
    if (mode_ != Mode::Local && mode_ != Mode::Computer && mode_ != Mode::Remote) {
        return Keep::No;
    }
    if (phase_ == Phase::Over) return Keep::No;
    if (mode_ != Mode::Remote && phase_ == Phase::Opening && rolls_ == 0) {
        return Keep::No;
    }
    return Keep::Quietly;
}

/* The board is exactly as end() left it, the computer's plan and the turn
 * half played included; end() only wrote it out and, away from a nearby
 * game, gave the radio back, which nothing here needs again. A nearby turn
 * was left on the air and the poll goes on republishing it.
 *
 * One clock paces the computer's thinking and each checker it moves, the
 * other console's checkers as they arrive, and "No moves" before the turn
 * passes on; it starts again where it was rather than having run out while
 * nobody was looking. A half-pressed End is closed: a "Sure?" waiting
 * for the first tap after a break is a game ended by accident. */
void BackgammonGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    if (phase_ == Phase::Computer || phase_ == Phase::Remote) movedOn(timerMs_, awayMs);
    confirmUntilMs_ = 0;
}
