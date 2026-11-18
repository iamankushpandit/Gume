// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "GoGame.h"

#include <string.h>

#include "engine/Entropy.h"
#include "engine/NearbyPlay.h"

/* Remembering a game, like Chess: after every move and on the way out, so a
 * console put down comes back to the same position. The board is packed to
 * two bits a point, so a 19x19 game is a hundred bytes rather than four
 * hundred; the undo copy and the computer's working state are not saved --
 * a game put down loses its undo, which is the lesser thing to lose. Fixed
 * layout: loadBlob() refuses a blob whose length changed, and `version`
 * covers one that kept its size.
 *
 * WHOSE GAME IT IS decides where it is kept.
 *
 *   A game passed between two people, or against the computer, is the
 *   player's: saveBlob(), which is theirs alone and which Guest does not
 *   have. The lobby's three chips are kept there too.
 *
 *   A game against another console is the CONSOLE's: saveConsoleBlob(). The
 *   other console is playing this one, whoever picks it up, so the game
 *   carries on through a change of player, is kept for Guest through a power
 *   cut, and never overwrites anybody's own game -- that one is still in the
 *   player's save, and is there again when the nearby game is over.
 *
 * Opening the screen and leaving it are here as well, because both are mostly
 * a matter of which of those two there is to come back to; and so is a nearby
 * game ending, which is the console's game being forgotten. */

namespace {

/* The blob as it was while every nearby game saved under one key, member for
 * member, so a game left there can still be read. Never written. */
constexpr char OLD_KEY[] = "game";
constexpr uint8_t OLD_VERSION = 2;
struct SavedV2 {
    uint16_t magic;
    uint8_t version;
    uint8_t mode;
    uint8_t phase;
    uint8_t boardSize;
    uint8_t rules;
    uint8_t level;
    uint8_t n;
    uint8_t at[(Go::MAX_POINTS + 3) / 4];
    uint8_t toMove;
    uint16_t ko;
    uint16_t captured[2];
    uint8_t passes;
    uint16_t moves;
    uint16_t last;
    uint8_t over;
    uint8_t winner;
    uint8_t humanColour;
    uint8_t agreed;
    uint8_t dead[(Go::MAX_POINTS + 7) / 8];
    char opponent[5];
    char opponentName[11];
    uint8_t session;
    uint8_t ourPly;
    uint8_t theirPly;
    uint8_t ourFrom;
    uint8_t ourTo;
    uint8_t ended;
};

/* Is anybody asking this console for a game of Go? Every console in the room
 * is looked at: the first one found asking may be asking for another game. */
bool invitedHere(AppContext& host) {
    const uint8_t n = host.nearbySeatCount();
    for (uint8_t i = 0; i < n; ++i) {
        NearbySeat seat;
        if (host.nearbySeatAt(i, seat) && seat.inviting && seat.forThisGame) {
            return true;
        }
    }
    return false;
}

}   // namespace

// ---- opening and leaving ----------------------------------------------------------

void GoGame::begin(AppContext& host) {
    confirmUntilMs_ = 0;
    confirmShown_ = false;
    ghost_ = Go::NO_POINT;
    haveUndo_ = false;
    watch_.reset();
    pausePainted_ = false;
    pauseHold_ = false;
    lobbyHold_ = false;
    resultOverlay_ = false;
    rng_.x = Entropy::below(0xFFFFFFFFU) | 1U;
    lobbyStale_ = true;
    turnStale_ = capturesStale_ = infoStale_ = buttonsStale_ = resultStale_ = true;
    seatCount_ = 0;
    seatsAtMs_ = 0;

    /* This screen is one instance for the life of the console, so a nearby
     * game it was left in is still here, finished or not, and is kept exactly
     * as it is: leaving the screen did not end it, and neither does whoever
     * is holding the console now. */
    if (mode_ == Mode::Remote) {
        return;
    }

    /* The chips are the player's, whatever else comes back. */
    Saved own{};
    const bool haveOwn = loadOwn(host, own);
    if (haveOwn) {
        takeChips(own);
    }
    shelved_ = Mode::Lobby;

    /* The console's own game first: one against another console, left in the
     * middle before the console was switched off. A finished one is only a
     * result card nobody came back to, and is forgotten. */
    Saved kept{};
    host.loadConsoleBlob(SAVE_KEY, &kept, sizeof(kept));
    if (kept.magic == SAVE_MAGIC) {
        if (kept.version == SAVE_VERSION && kept.phase != static_cast<uint8_t>(Phase::Over) &&
            applySaved(kept, true)) {
            lastWordAtMs_ = 0;
            publishOurs(host);   // our last word again, for the console still waiting on it
            return;
        }
        host.clearConsoleBlob(SAVE_KEY);
    }

    /* Then the player's own game, or the lobby. */
    if (!haveOwn || !applySaved(own, false)) {
        mode_ = Mode::Lobby;
    }
    /* Not a nearby game: nothing of this screen's belongs on the air, bar a
     * finished game's last word, which is on its own clock. */
    if (!lastWordUp()) {
        host.nearbyStop();
    }
    /* Somebody is asking this console for a game of Go. The lobby is where
     * that is seen and answered, so it comes up instead of the player's own
     * game -- which is not touched: the lobby offers to carry it on, and it
     * is back next time if the invitation is left unanswered. */
    if ((mode_ == Mode::Local || mode_ == Mode::Computer) && invitedHere(host)) {
        if (phase_ != Phase::Over) {
            shelved_ = mode_;
        }
        mode_ = Mode::Lobby;
    }
    /* The lobby opens on the room as it is, not on an empty list that fills
     * in a frame later. */
    if (mode_ == Mode::Lobby) {
        refreshSeats(host);
    }
}

void GoGame::end(AppContext& host) {
    /* Leaving the screen does not end a game played across consoles: only
     * End game does. Its last move stays on the air while this console is
     * elsewhere -- the other console may not have heard it yet, and taking it
     * off would strand them waiting for a move that had been made -- and is
     * said again when this screen comes back. A nearby game that is over has
     * only its last word left to be heard, and that is given a minute. */
    if (mode_ == Mode::Remote) {
        saveGame(host);
        if (phase_ == Phase::Over) {
            host.nearbyStopLater();
        }
        return;
    }
    if (mode_ == Mode::Local || mode_ == Mode::Computer) {
        saveGame(host);
    }
    /* Everything else hands the radio back, an unanswered invitation
     * included: nobody is here to play the game it offers. The last word of
     * a game ended a moment ago is the one thing left its minute. */
    if (lastWordUp()) {
        host.nearbyStopLater();
    } else {
        host.nearbyStop();
    }
}

// ---- writing ----------------------------------------------------------------------

void GoGame::fillSaved(Saved& out) const {
    out = Saved{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    out.mode = static_cast<uint8_t>(mode_);
    out.phase = static_cast<uint8_t>(phase_ == Phase::Thinking ? Phase::Play : phase_);
    out.boardSize = boardSize_;
    out.rules = static_cast<uint8_t>(rules_);
    out.level = static_cast<uint8_t>(level_);
    out.n = state_.n;
    out.gameRules = static_cast<uint8_t>(state_.rules);
    const uint16_t total = Go::points(state_.n);
    for (uint16_t p = 0; p < total; ++p) {
        out.at[p >> 2] = static_cast<uint8_t>(out.at[p >> 2] | ((state_.at[p] & 3) << ((p & 3) * 2)));
        if (dead_[p] != 0) out.dead[p >> 3] = static_cast<uint8_t>(out.dead[p >> 3] | (1U << (p & 7)));
    }
    out.toMove = state_.toMove;
    out.ko = state_.ko;
    out.captured[0] = state_.captured[Go::BLACK];
    out.captured[1] = state_.captured[Go::WHITE];
    out.passes = state_.passes;
    out.moves = state_.moves;
    out.last = state_.last;
    out.over = state_.over ? 1 : 0;
    out.winner = state_.winner;
    out.humanColour = humanColour_;
    out.agreed = agreed_;
    memcpy(out.opponent, opponent_, sizeof(out.opponent));
    memcpy(out.opponentName, opponentName_, sizeof(out.opponentName));
    out.session = session_;
    out.ourPly = ourPly_;
    out.theirPly = theirPly_;
    out.ourFrom = ourFrom_;
    out.ourTo = ourTo_;
}

void GoGame::saveGame(AppContext& host) const {
    /* An invitation not yet answered is not a game. Nothing of it is kept,
     * and above all it is not written over the player's own save. */
    if (mode_ == Mode::Waiting) {
        return;
    }
    Saved out{};
    fillSaved(out);
    if (mode_ == Mode::Remote) {
        host.saveConsoleBlob(SAVE_KEY, &out, sizeof(out));
    } else {
        host.saveBlob(SAVE_KEY, &out, sizeof(out));
    }
}

void GoGame::saveChips(AppContext& host) const {
    /* The lobby can be up over a game still in the player's save -- an
     * invitation was waiting, or a nearby game has just ended -- and a chip
     * is not a reason to lose it. So the chips are changed in what is saved
     * and the rest is written back as it was read. */
    Saved out{};
    if (!loadOwn(host, out)) {
        fillSaved(out);
        out.mode = static_cast<uint8_t>(Mode::Lobby);
    }
    out.boardSize = boardSize_;
    out.rules = static_cast<uint8_t>(rules_);
    out.level = static_cast<uint8_t>(level_);
    host.saveBlob(SAVE_KEY, &out, sizeof(out));
}

// ---- reading ----------------------------------------------------------------------

bool GoGame::loadOwn(AppContext& host, Saved& out) const {
    out = Saved{};
    host.loadBlob(SAVE_KEY, &out, sizeof(out));
    if (out.magic == SAVE_MAGIC && out.version == SAVE_VERSION) {
        return true;
    }
    /* Nothing under Go's own key yet: a game may be waiting under the one
     * all five nearby games shared. It is taken only if it is Go's, and a
     * game against another console is not taken from it at all -- that key
     * was the player's, and such a game is the console's. Its chips are. */
    SavedV2 old{};
    host.loadBlob(OLD_KEY, &old, sizeof(old));
    if (old.magic != SAVE_MAGIC || old.version != OLD_VERSION) {
        return false;
    }
    out = Saved{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    const Mode mode = static_cast<Mode>(old.mode);
    out.mode = (mode == Mode::Local || mode == Mode::Computer) ? old.mode
                                                               : static_cast<uint8_t>(Mode::Lobby);
    out.phase = old.phase;
    out.boardSize = old.boardSize;
    out.rules = old.rules;
    out.level = old.level;
    out.n = old.n;
    out.gameRules = old.rules;   // a game was played on the chip, then
    memcpy(out.at, old.at, sizeof(out.at));
    out.toMove = old.toMove;
    out.ko = old.ko;
    out.captured[0] = old.captured[0];
    out.captured[1] = old.captured[1];
    out.passes = old.passes;
    out.moves = old.moves;
    out.last = old.last;
    out.over = old.over;
    out.winner = old.winner;
    out.humanColour = old.humanColour;
    out.agreed = old.agreed;
    memcpy(out.dead, old.dead, sizeof(out.dead));
    return true;
}

void GoGame::takeChips(const Saved& in) {
    if (in.rules < Go::RULES_COUNT) rules_ = static_cast<Go::Rules>(in.rules);
    level_ = in.level != 0 ? Go::Level::Medium : Go::Level::Easy;
    boardSize_ = (in.boardSize > 9 && BIG_BOARD_AVAILABLE) ? 19 : 9;
}

bool GoGame::applySaved(const Saved& in, bool nearby) {
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) {
        return false;
    }
    /* The console's slot holds a game against another console and nothing
     * else; the player's holds a game played here, or only the lobby's
     * chips, which is nothing to come back to. */
    const Mode mode = static_cast<Mode>(in.mode);
    if (nearby ? (mode != Mode::Remote || in.opponent[0] == 0)
               : (mode != Mode::Local && mode != Mode::Computer)) {
        return false;
    }
    if (in.phase > static_cast<uint8_t>(Phase::Over) || (in.n != 9 && in.n != 19) ||
        (in.n == 19 && !BIG_BOARD_AVAILABLE) || in.gameRules >= Go::RULES_COUNT ||
        in.toMove < Go::BLACK || in.toMove > Go::WHITE || in.winner > Go::WHITE || in.passes > 2 ||
        in.humanColour < Go::BLACK || in.humanColour > Go::WHITE) {
        return false;
    }
    Go::reset(state_, in.n, static_cast<Go::Rules>(in.gameRules));
    const uint16_t total = Go::points(state_.n);
    for (uint16_t p = 0; p < total; ++p) {
        const uint8_t v = static_cast<uint8_t>((in.at[p >> 2] >> ((p & 3) * 2)) & 3);
        if (v > Go::WHITE) return false;
        state_.at[p] = v;
        dead_[p] = (in.dead[p >> 3] >> (p & 7)) & 1;
    }
    if (in.ko != Go::NO_POINT && in.ko >= total) return false;
    if (in.last != Go::NO_POINT && in.last >= total) return false;
    state_.toMove = in.toMove;
    state_.ko = in.ko;
    state_.captured[Go::BLACK] = in.captured[0];
    state_.captured[Go::WHITE] = in.captured[1];
    state_.passes = in.passes;
    state_.moves = in.moves;
    state_.last = in.last;
    state_.over = in.over != 0;
    state_.winner = in.winner;
    /* Refuse a board a game could not have reached rather than drawing it:
     * a stone with no liberties is not a position. */
    for (uint16_t p = 0; p < total; ++p) {
        if (state_.at[p] != Go::EMPTY && Go::liberties(state_, p) == 0) return false;
    }
    mode_ = mode;
    phase_ = static_cast<Phase>(in.phase);
    humanColour_ = in.humanColour;
    agreed_ = in.agreed;
    memcpy(opponent_, in.opponent, sizeof(opponent_));
    opponent_[sizeof(opponent_) - 1] = 0;
    memcpy(opponentName_, in.opponentName, sizeof(opponentName_));
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = in.session;
    ourPly_ = in.ourPly;
    theirPly_ = in.theirPly;
    ourFrom_ = in.ourFrom;
    ourTo_ = in.ourTo;
    /* What the other console has heard is not saved: nothing more is said in
     * the marking until it has acked again, and an agreement of theirs counts
     * only once it has seen everything we said before the console was put
     * down. */
    theirAck_ = 0;
    haveAck_ = false;
    lastTogglePly_ = ourPly_;
    toggledThisMarking_ = mode_ == Mode::Remote && phase_ == Phase::Marking;
    ghost_ = Go::NO_POINT;
    haveUndo_ = false;
    deadPlayoutsLeft_ = 0;
    confirmUntilMs_ = 0;
    if (phase_ == Phase::Over) {
        Go::score(state_, state_.rules == Go::Rules::Territory ? dead_ : nullptr, score_);
    }
    if (phase_ == Phase::Marking || phase_ == Phase::Over) {
        Go::ownership(state_, dead_, own_);
    } else {
        memset(own_, 0, sizeof(own_));
    }
    /* A computer to move comes back thinking. */
    if (mode_ == Mode::Computer && phase_ == Phase::Play && !state_.over && !humanTurn()) {
        phase_ = Phase::Thinking;
        timerMs_ = millis() + 400;
        if (level_ == Go::Level::Medium) Go::beginSearch(search_, state_, rng_.next());
    }
    setMessage(phase_ == Phase::Marking ? "Tap groups that cannot live"
               : humanTurn()             ? "Tap a point"
                                         : "");
    return true;
}

/* Leaving a game in memory as well as in NVS, which keeps what a save
 * cannot: the undo, and the computer's opinion of the dead stones part-way
 * through. Guest's saves are dropped, so for Guest this is the only way a
 * game survives Home at all.
 *
 * Kept quietly, because End game is right there beside the board. Not kept:
 * the lobby, an invitation still waiting for an answer, a game that has been
 * scored, and a local board with no stone on it yet. A nearby game is kept
 * from the start, because the other console is already in it. */
Game::Keep GoGame::keepsGame() const {
    if (mode_ != Mode::Local && mode_ != Mode::Computer && mode_ != Mode::Remote) {
        return Keep::No;
    }
    if (phase_ == Phase::Over) return Keep::No;
    if (mode_ != Mode::Remote && state_.moves == 0) return Keep::No;
    return Keep::Quietly;
}

/* The board is exactly as end() left it; end() only wrote it out and, away
 * from a nearby game, gave the radio back, which nothing here needs again. A
 * nearby move was left on the air and update() goes on republishing it.
 * The computer's pause before it plays starts again where it was, rather
 * than having run out while nobody was looking, and a half-pressed End game
 * is closed: a "Sure?" waiting for the first tap after a break is a game
 * ended by accident. */
void GoGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    if (phase_ == Phase::Thinking) movedOn(timerMs_, awayMs);
    confirmUntilMs_ = 0;
}

// ---- a nearby game ending: its last word, and the way back to the lobby ------------

void GoGame::holdLastWord(AppContext& host) {
    /* What is on the air now is the last this game will say. The other
     * console may not have heard it yet, so it stays -- for the service's
     * minute, and then the service takes it down wherever this console has
     * got to by then. Nothing says it again: that would stop the clock. */
    host.nearbyStopLater();
    lastWordAtMs_ = millis();
    if (lastWordAtMs_ == 0) lastWordAtMs_ = 1;
}

void GoGame::sayLastWord(AppContext& host) {
    publishOurs(host);   // once more, carrying our ack of the move that ended it
    holdLastWord(host);
}

bool GoGame::lastWordUp() const {
    return lastWordAtMs_ != 0 && millis() - lastWordAtMs_ < NearbyPlay::LAST_WORD_MS;
}

void GoGame::lobbyAfterNearby(AppContext& host) {
    /* Finished with: it is not the console's game to come back to any more. */
    host.clearConsoleBlob(SAVE_KEY);
    watch_.reset();
    pausePainted_ = false;
    pauseHold_ = false;
    confirmUntilMs_ = 0;
    mode_ = Mode::Lobby;
    /* The lobby is the player's: their chips, and their own game if one was
     * saved before the nearby game began -- it was never written over, and
     * the lobby offers to carry it on. */
    shelved_ = Mode::Lobby;
    Saved own{};
    if (loadOwn(host, own)) {
        takeChips(own);
        const Mode kind = static_cast<Mode>(own.mode);
        if ((kind == Mode::Local || kind == Mode::Computer) &&
            own.phase != static_cast<uint8_t>(Phase::Over)) {
            shelved_ = kind;
        }
    }
    lobbyStale_ = true;
    refreshSeats(host);   // the room as it is now, in the one repaint
    markFullDirty();
}

void GoGame::leaveRemote(AppContext& host, const char* note) {
    if (note != nullptr) {
        snprintf(lobbyNote_, sizeof(lobbyNote_), "%s", note);
    }
    /* They ended it, so there is nobody left to hear anything of ours. */
    host.nearbyStop();
    lastWordAtMs_ = 0;
    host.playSound(Sound::GameOver);
    lobbyAfterNearby(host);
}

void GoGame::endRemoteByUs(AppContext& host) {
    /* The service's own ending, which stays on the air after we leave so the
     * other console still hears it; it goes back to its lobby too
     * (pollRemote). Used by End game and by the pause card alike. */
    host.nearbyEnd(session_, static_cast<uint8_t>((ourPly_ + 1) & 0x7F), theirPly_);
    holdLastWord(host);
    host.playSound(Sound::Select);
    lobbyAfterNearby(host);
}

void GoGame::resumeShelved(AppContext& host) {
    const Mode kind = shelved_;
    shelved_ = Mode::Lobby;
    host.playSound(Sound::Select);
    Saved own{};
    if (loadOwn(host, own) && applySaved(own, false)) {
        watch_.reset();
        pausePainted_ = false;
        turnStale_ = capturesStale_ = infoStale_ = buttonsStale_ = resultStale_ = true;
        markFullDirty();
        return;
    }
    /* Not there to pick up after all: a new one of the same kind. */
    newGame(kind);
    saveGame(host);
}
