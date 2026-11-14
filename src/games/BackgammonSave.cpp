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
 * length changed, and `version` covers one that kept its size.
 *
 * ------------------------------------------------------------------------
 * Two places, and which game goes where
 * ------------------------------------------------------------------------
 * A game on one console -- two people passing it, or against the computer --
 * is the player's, and is saved with that player's own things. A game across
 * consoles is the CONSOLE's: the other console is playing this one, whoever
 * is holding it. So it is kept in the console's own storage and nowhere else.
 * That is what lets it carry on after a change of player, keeps it for Guest
 * (whose own saves are dropped) through a power cut, and stops it ever taking
 * the place of somebody's game against the computer. */

namespace {

/* The nearby games once all saved under this one key, each over the last.
 * It is still read, for a game on one console that was left there, and never
 * written again. */
constexpr char OLD_KEY[] = "game";

}   // namespace

void BackgammonGame::saveGame(AppContext& host) const {
    const bool nearby = mode_ == Mode::Remote;
    if (!nearby && mode_ != Mode::Local && mode_ != Mode::Computer) {
        return;   // the lobby and an invitation are nothing to come back to
    }
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
    if (nearby) {
        host.saveConsoleBlob(SAVE_KEY, &out, sizeof(out));
    } else {
        host.saveBlob(SAVE_KEY, &out, sizeof(out));
    }
}

void BackgammonGame::forgetGame(AppContext& host) const {
    if (mode_ == Mode::Remote) {
        host.clearConsoleBlob(SAVE_KEY);
    } else if (mode_ == Mode::Local || mode_ == Mode::Computer) {
        /* A blob that says "no game" rather than no blob at all: with nothing
         * under this key the old shared key is looked in again, and a game
         * ended long ago would come back from it. */
        Saved out{};
        out.magic = SAVE_MAGIC;
        out.version = SAVE_VERSION;
        out.mode = static_cast<uint8_t>(Mode::Lobby);
        host.saveBlob(SAVE_KEY, &out, sizeof(out));
    }
}

bool BackgammonGame::valid(const Saved& in, bool nearby) {
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) {
        return false;
    }
    /* Each place holds only its own kind of game. The player's never holds a
     * nearby one, so one found there -- under the old key -- is not taken: a
     * game across consoles is not any one player's to bring back. */
    const Mode mode = static_cast<Mode>(in.mode);
    if (nearby ? mode != Mode::Remote : (mode != Mode::Local && mode != Mode::Computer)) {
        return false;
    }
    if (nearby && in.opponent[0] == 0) {
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
    if (static_cast<Phase>(in.phase) == Phase::Over && !(nearby && in.outboxCount > 0)) {
        /* A finished game is not worth reopening -- except a nearby one whose
         * winning moves had not all gone out: the other console is still
         * waiting for them and does not know it lost. */
        return false;
    }
    return true;
}

void BackgammonGame::take(const Saved& in) {
    mode_ = static_cast<Mode>(in.mode);
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
    lastAcked_ = false;         // not known until the other console says
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
}

void BackgammonGame::readPlayerSlot(AppContext& host, Saved& in) {
    in = Saved{};
    host.loadBlob(SAVE_KEY, &in, sizeof(in));
    if (in.magic == SAVE_MAGIC && in.version == SAVE_VERSION) {
        return;   // ours, even when all it says is "no game"
    }
    /* Nothing of Backgammon's own yet: the old shared key, which may hold a
     * Backgammon game, another game's blob or nothing. valid() tells them
     * apart, and the first save from here goes under Backgammon's key. */
    in = Saved{};
    host.loadBlob(OLD_KEY, &in, sizeof(in));
}

bool BackgammonGame::loadPlayerGame(AppContext& host) {
    Saved in{};
    readPlayerSlot(host, in);
    if (!valid(in, false)) {
        return false;
    }
    take(in);
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

BackgammonGame::Mode BackgammonGame::savedMode(AppContext& host) const {
    Saved in{};
    readPlayerSlot(host, in);
    return valid(in, false) ? static_cast<Mode>(in.mode) : Mode::Lobby;
}

void BackgammonGame::enterLobby(AppContext& host) {
    mode_ = Mode::Lobby;
    /* A nearby game is never saved over the player's own, so that one may
     * still be there: the lobby offers it rather than starting over it. */
    savedBehind_ = savedMode(host);
    refreshSeats(host);   // the room as it is now, not as the last visit left it
    lobbyStale_ = true;
    markFullDirty();
}

void BackgammonGame::openGame(AppContext& host) {
    savedBehind_ = Mode::Lobby;
    if (mode_ == Mode::Remote) {
        /* A nearby game is still in this instance from before the screen was
         * left, finished or not, and it is the one to carry on with -- the
         * other console is still in it, whoever is holding this one now. Only
         * what belonged to the last visit's screen is put away; the pause
         * card comes back by itself if the other console is still quiet. */
        selected_ = Bg::NO_POINT;
        watch_.reset();
        pausePainted_ = false;
        setPhaseMessage();
        return;
    }

    /* A nearby game the console was switched off in the middle of is in the
     * console's storage. It is carried on with, whoever the player is now,
     * and our word is said again: nothing has been on the air since. */
    Saved kept{};
    host.loadConsoleBlob(SAVE_KEY, &kept, sizeof(kept));
    if (valid(kept, true)) {
        take(kept);
        host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
        return;
    }
    host.clearConsoleBlob(SAVE_KEY);   // a game that is over, or nothing to read

    if (!loadPlayerGame(host)) {
        mode_ = Mode::Lobby;
    }
    /* Nothing of this game's belongs on the air now, except a last word that
     * is still serving its minute. */
    if (!lastWordStanding()) {
        host.nearbyStop();
    }
    /* A console is asking this one to play: the lobby, where that can be
     * seen and accepted, in front of the player's own game. That game is
     * left exactly as saved -- leaving without accepting brings it back. */
    NearbySeat seat;
    if (mode_ != Mode::Lobby && invitationHere(host, nullptr, seat)) {
        savedBehind_ = mode_;
        mode_ = Mode::Lobby;
    }
    if (mode_ == Mode::Lobby) {
        refreshSeats(host);   // the room as it is now, for the first paint
    }
}
