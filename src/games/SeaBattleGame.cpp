// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "SeaBattleGame.h"

#include <esp_system.h>   // esp_random(), for laying out a fleet
#include <string.h>

#include "engine/AppRegistry.h"
#include "ui/Stops.h"

/* Sea Battle: the screen and the rules. Four files against one header --
 *   SeaBattleGame.cpp  this: the fleet, a shot, the game's life, a tap
 *   SeaBattleDraw.cpp  geometry and every pixel
 *   SeaBattleNet.cpp   the lobby and a game against a nearby console
 *   SeaBattleSave.cpp  remembering a game in NVS
 * Split by line range from one 1000-line file; nothing moved changed. */

namespace {

/* Score is null: sinking a fleet is not a number. The field list is positional
 * and tools/app_registry_parser.py reads it that way, so keep comments out
 * from between the fields. */
constexpr AppMetadata SEA_BATTLE_METADATA = {
    "seabattle",
    "Sea Battle",
    nullptr,
    "sink the fleet",
    "Sea Battle",
    "Two players. Hunt the hidden ships.",
    nullptr,
    LauncherIcon::SeaBattle,
    33,
    true,
};

/* One four, one three and two twos: eleven cells hidden in sixty-four. Dense
 * enough that random guessing finds something, sparse enough that hunting is
 * worth doing -- the classic ten-by-ten ratio is seventeen in a hundred, and
 * this is deliberately a little more generous because the players are young. */
constexpr uint8_t SHIP_LEN[SeaBattleGame::SHIP_COUNT] = {4, 3, 2, 2};

}   // namespace

const AppMetadata& seaBattleAppMetadata() {
    return SEA_BATTLE_METADATA;
}

const char* SeaBattleGame::title() const {
    return seaBattleAppMetadata().screenTitle != nullptr
        ? seaBattleAppMetadata().screenTitle
        : seaBattleAppMetadata().title;
}

// ---------------------------------------------------------------- rules

/* Lay a fleet at random.
 *
 * Longest ship first, which matters: a four needs a run of four free squares
 * and gets steadily harder to place as the sea fills up, while a two will
 * almost always find room. Placing the awkward ones while the sea is empty is
 * what keeps the retry loop short.
 *
 * Ships may touch. The variant that forbids it is a harder game and a longer
 * placement, and neither is what this is for.
 *
 * The loop is bounded rather than open: eleven cells in sixty-four is loose
 * enough that a failure is vanishingly unlikely, but an unbounded search on a
 * device with a twelve-second watchdog is a hang waiting for a bad seed. On
 * giving up it starts the whole fleet again, which cannot loop forever because
 * each attempt is independent.
 */
void SeaBattleGame::placeFleet(Side& side) {
    for (uint8_t attempt = 0; attempt < 8; ++attempt) {
        memset(side.shipAt, NO_SHIP, sizeof(side.shipAt));
        bool ok = true;
        for (uint8_t ship = 0; ship < SHIP_COUNT && ok; ++ship) {
            const uint8_t len = SHIP_LEN[ship];
            bool placed = false;
            for (uint16_t tries = 0; tries < 200 && !placed; ++tries) {
                const uint32_t r = esp_random();
                const bool horizontal = (r & 1u) != 0;
                const uint8_t span = static_cast<uint8_t>(GRID - len + 1);
                const uint8_t col = static_cast<uint8_t>(
                    (r >> 1) % (horizontal ? span : GRID));
                const uint8_t row = static_cast<uint8_t>(
                    (r >> 9) % (horizontal ? GRID : span));

                bool free = true;
                for (uint8_t i = 0; i < len && free; ++i) {
                    const uint8_t c = horizontal ? static_cast<uint8_t>(col + i) : col;
                    const uint8_t w = horizontal ? row : static_cast<uint8_t>(row + i);
                    free = side.shipAt[cellOf(c, w)] == NO_SHIP;
                }
                if (!free) continue;

                for (uint8_t i = 0; i < len; ++i) {
                    const uint8_t c = horizontal ? static_cast<uint8_t>(col + i) : col;
                    const uint8_t w = horizontal ? row : static_cast<uint8_t>(row + i);
                    side.shipAt[cellOf(c, w)] = ship;
                }
                placed = true;
            }
            ok = placed;
        }
        if (ok) return;
    }
}

bool SeaBattleGame::shipSunk(const Side& side, uint8_t cell) {
    const uint8_t ship = side.shipAt[cell];
    if (ship == NO_SHIP) return false;
    for (uint8_t c = 0; c < CELLS; ++c) {
        if (side.shipAt[c] == ship && !side.incoming[c]) return false;
    }
    return true;
}

/* Fire at `target`. The damage is recorded on the side being fired AT, which
 * is the only side that knows whether anything was there -- and in a remote
 * game is the only side that has the information at all. */
SeaBattleGame::Shot SeaBattleGame::resolve(Side& target, uint8_t cell) {
    if (target.incoming[cell]) {
        /* Already fired at. Cannot happen from our own board -- a marked
         * square is not tappable -- but a peer's advertisement is not ours to
         * trust, and answering "miss" is the harmless reading. */
        return Shot::Miss;
    }
    target.incoming[cell] = true;
    if (target.shipAt[cell] == NO_SHIP) return Shot::Miss;

    ++target.hitsTaken;
    if (!shipSunk(target, cell)) return Shot::Hit;
    ++target.sunkAgainstUs;
    return Shot::Sunk;
}


// --------------------------------------------------------------- screen

void SeaBattleGame::newGame() {
    watch_.reset();
    pausePainted_ = false;
    for (Side& s : side_) {
        memset(s.shipAt, NO_SHIP, sizeof(s.shipAt));
        memset(s.incoming, 0, sizeof(s.incoming));
        for (Mark& m : s.shot) m = Mark::Unknown;
        s.hitsScored = 0;
        s.hitsTaken = 0;
        s.sunkAgainstUs = 0;
    }
    placeFleet(side_[0]);
    placeFleet(side_[1]);
    placed_[0] = placed_[1] = false;
    turnPlayer_ = 0;
    phase_ = Phase::Placing;
    won_ = lost_ = ended_ = false;
    endedByUs_ = false;
    lastShot_ = Shot::None;
    pendingCell_ = NO_CELL;
    owedReply_ = Shot::None;
    ourPly_ = 0;
    theirPly_ = 0;
    ourShotCell_ = 0;
    ourReplyCode_ = 0;
    overSaid_ = false;
    keptLocal_ = false;
    confirmUntilMs_ = 0;
    dirtyCount_ = 0;
    fullPaint_ = true;
    panelStale_ = true;
    markFullDirty();
}

void SeaBattleGame::begin(AppContext& host) {
    /* A game across consoles is the console's, and this screen is one
     * instance for the life of the device: a nearby game it was in when it
     * was left is still here, finished or not, whoever is holding the
     * console now. Carry on from exactly that. Only what belonged to the
     * last visit's picture is put back. */
    if (mode_ == Mode::Remote) {
        confirmUntilMs_ = 0;
        dirtyCount_ = 0;
        watch_.reset();   // so the card comes back if they are still quiet
        pausePainted_ = false;
        fullPaint_ = true;
        panelStale_ = true;
        markFullDirty();
        return;
    }
    toLobby();
    /* After a restart the same game is in the console's storage, and comes
     * first: the other console is still playing it. Its turn goes back on
     * the air from the next frame. */
    if (restoreNearby(host)) {
        markFullDirty();
        return;
    }
    const bool own = restoreOwn(host);
    /* Not in a nearby game, so nothing of this game's belongs on the air --
     * bar a finished one's last word, which is on its own timer. */
    if (!lastWord_) host.nearbyStop();
    /* Somebody is asking this console for a game right now. Opening straight
     * onto the player's own game would hide that, so the lobby comes up
     * instead, with the way back to that game as its first row. Nothing is
     * written for this: leave without answering and the game is where it
     * was. */
    NearbySeat asking;
    if (own && invitedBy(host, nullptr, asking)) {
        toLobby();
        keptLocal_ = true;
    }
    markFullDirty();
}

void SeaBattleGame::end(AppContext& host) {
    saveGame(host);
    /* Leaving the screen does not end a game played across consoles: only End
     * game does. So a nearby game's turn stays on the air while this console
     * is elsewhere -- the other console may not have heard it yet, and taking
     * it off would strand them waiting for a shot or an answer that had
     * already been sent. It is said again when this screen comes back. */
    if (mode_ == Mode::Remote) {
        /* One that is over has only its last word up, and nobody here to
         * take it down once it is heard: a minute, then it goes. */
        if (gameOver()) host.nearbyStopLater();
        return;
    }
    /* The lobby, an invitation nobody has answered, a game on this console:
     * the radio goes back. A finished nearby game's last word, left behind
     * for the lobby, is already on its minute and is left to it. */
    if (!lastWord_) host.nearbyStop();
}

void SeaBattleGame::toLobby() {
    mode_ = Mode::Lobby;
    opponent_[0] = 0;
    opponentName_[0] = 0;
    session_ = 0;
    seatCount_ = 0;
    seatsAtMs_ = 0;
    watch_.reset();
    pausePainted_ = false;
    newGame();
}

void SeaBattleGame::startLocal() {
    mode_ = Mode::Local;
    opponent_[0] = 0;
    opponentName_[0] = 0;
    newGame();
}

void SeaBattleGame::startRemote(const NearbySeat& seat, uint8_t session,
                                bool weFireFirst) {
    strncpy(opponent_, seat.deviceId, sizeof(opponent_) - 1);
    opponent_[sizeof(opponent_) - 1] = 0;
    strncpy(opponentName_, seat.name, sizeof(opponentName_) - 1);
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = static_cast<uint8_t>(session & 0x3F);
    remoteFiresFirst_ = weFireFirst;
    mode_ = Mode::Remote;
    /* An invitation or this game's first turn takes the place of whatever
     * was on the air, so an old game's last word is no longer ours to stop. */
    lastWord_ = false;
    newGame();
}

/* The sea is a stop for the d-pad only while a press on it would fire. The
 * squares stay drawn through the other player's turn, an unanswered shot, a
 * pause and the result, and none of those takes a press. The curtain and the
 * lobby have controls of their own where the board would be, so they say
 * nothing at all. */
void SeaBattleGame::noteStops() const {
    if (mode_ == Mode::Lobby || phase_ == Phase::Passing) {
        Ui::clearBoardStops();
        return;
    }
    bool live = phase_ == Phase::Firing && !gameOver() && mode_ != Mode::Waiting;
    if (live && mode_ == Mode::Remote) {
        live = ourTurn() && pendingCell_ == NO_CELL && !watch_.paused();
    }
    Ui::setBoardStops(boardRect(), live);
}

/* In a remote game the plies alternate strictly, so whose turn it is falls out
 * of how many shots each side has taken and who was given the first one. */
bool SeaBattleGame::ourTurn() const {
    if (mode_ != Mode::Remote) return true;
    if (remoteFiresFirst_) return ourPly_ == theirPly_;
    return ourPly_ < theirPly_;
}

void SeaBattleGame::markCell(uint8_t cell) {
    if (cell == NO_CELL) return;
    for (uint8_t i = 0; i < dirtyCount_; ++i) {
        if (dirty_[i] == cell) return;
    }
    if (dirtyCount_ < sizeof(dirty_)) dirty_[dirtyCount_++] = cell;
}

/* Fire at one square of the enemy sea.
 *
 * The two modes part company here and nowhere else. Locally the other fleet is
 * in memory, so the shot resolves immediately. Remotely only the opponent
 * knows what is there, so the square is marked Pending and the answer arrives
 * on their next turn -- which is also when we tell them about theirs. One
 * round trip carries both, because a separate answer message is the one that
 * would go missing.
 */
void SeaBattleGame::fireAt(AppContext& host, uint8_t cell) {
    Side& us = side_[viewer()];
    if (us.shot[cell] != Mark::Unknown) return;   // already tried there

    if (mode_ == Mode::Remote) {
        us.shot[cell] = Mark::Pending;
        pendingCell_ = cell;
        ourShotCell_ = cell;
        ourReplyCode_ = static_cast<uint8_t>(owedReply_);
        ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
        owedReply_ = Shot::None;
        /* Written down before it is said. A console that restarts says its
         * saved turn again, so a shot saved and not yet said is only late;
         * one said and not saved would be forgotten here and remembered
         * there, and the two would each wait on the other for good. */
        saveGame(host);
        host.nearbyPublish(session_, ourPly_, ourShotCell_, ourReplyCode_,
                           theirPly_);
        markCell(cell);
        host.playSound(Sound::Whoosh);
        panelStale_ = true;
        markDirty();
        return;
    }

    Side& them = side_[1 - viewer()];
    const Shot result = resolve(them, cell);
    us.shot[cell] = result == Shot::Miss ? Mark::Miss : Mark::Hit;
    if (result != Shot::Miss) ++us.hitsScored;
    lastShot_ = result;
    markCell(cell);
    host.playSound(result == Shot::Sunk  ? Sound::LevelUp
                   : result == Shot::Hit ? Sound::Coin
                                         : Sound::Tap);

    if (us.hitsScored >= FLEET_CELLS) {
        won_ = true;
        host.playSound(Sound::Victory);
    } else {
        /* Hand over. The curtain is what makes two players on one console
         * possible at all: without it the next player sees the sea they are
         * about to be asked to guess. */
        turnPlayer_ = static_cast<uint8_t>(1 - turnPlayer_);
        phase_ = Phase::Passing;
        fullPaint_ = true;
    }
    panelStale_ = true;
    markDirty();
    saveGame(host);
}

void SeaBattleGame::declareEnd(AppContext& host, bool byUs) {
    ended_ = true;
    endedByUs_ = byUs;
    confirmUntilMs_ = 0;
    /* Ended before this fleet was ready -- from the pause card, or by the
     * other console. There is no placing left to do, and the placing screen
     * has Ready and Shuffle where a finished game needs New game. */
    if (phase_ == Phase::Placing) phase_ = Phase::Firing;
    if (mode_ == Mode::Remote && byUs) {
        ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
        host.nearbyEnd(session_, ourPly_, theirPly_);
    }
    panelStale_ = true;
    fullPaint_ = true;
    saveGame(host);
    markFullDirty();
}


// ---------------------------------------------------------------- update

void SeaBattleGame::update(AppContext& host, const TouchPoint& touch) {
    noteStops();
    tickLastWord(host);

    if (mode_ == Mode::Lobby) {
        updateLobby(host, touch);
        return;
    }

    pollOpponent(host);

    if (mode_ == Mode::Waiting) {
        updateWaiting(host, touch);
        return;
    }

    /* Our turn stays on the air, and a finished game says its last. */
    if (mode_ == Mode::Remote) sayTurn(host);

    /* Are they still there? After the poll and the republish, before any
     * tap, so a press through the card is never a shot. */
    if (updatePause(host, touch)) return;

    if (confirmUntilMs_ != 0 && millis() > confirmUntilMs_) {
        confirmUntilMs_ = 0;
        panelStale_ = true;
        markDirty();
    }

    if (!touch.justPressed) return;

    // The curtain: one tap, anywhere, and nothing else on screen.
    if (phase_ == Phase::Passing) {
        phase_ = placed_[0] && placed_[1] ? Phase::Firing : Phase::Placing;
        fullPaint_ = true;
        host.playSound(Sound::Tap);
        markFullDirty();
        return;
    }

    if (phase_ == Phase::Placing) {
        if (readyRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.playSound(Sound::Select);
            placed_[viewer()] = true;
            if (mode_ == Mode::Remote) {
                phase_ = Phase::Firing;
                /* At once, not on the way out: after a power cut the fleet
                 * would otherwise come back as still being placed, with the
                 * other console already firing at it. */
                saveGame(host);
            } else if (placed_[0] && placed_[1]) {
                turnPlayer_ = 0;
                phase_ = Phase::Passing;
            } else {
                turnPlayer_ = static_cast<uint8_t>(1 - turnPlayer_);
                phase_ = Phase::Passing;
            }
            fullPaint_ = true;
            markFullDirty();
            return;
        }
        if (actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.playSound(Sound::Whoosh);
            placeFleet(side_[viewer()]);
            fullPaint_ = true;
            markFullDirty();
        }
        return;
    }

    // Firing.
    if (actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (gameOver()) {
            host.playSound(Sound::Select);
            /* A game on this console is already saved as over, which the
             * next visit reads as nothing; a nearby one is cleared away. */
            if (mode_ == Mode::Remote) finishNearby(host);
            toLobby();
            keptLocal_ = ownGameKept(host);
            markFullDirty();
        } else if (confirmUntilMs_ != 0) {
            declareEnd(host, true);
            host.playSound(Sound::GameOver);
        } else {
            confirmUntilMs_ = millis() + CONFIRM_MS;
            host.playSound(Sound::Tap);
            panelStale_ = true;
            markDirty();
        }
        return;
    }

    if (gameOver()) return;
    if (mode_ == Mode::Remote && !ourTurn()) return;
    if (mode_ == Mode::Remote && pendingCell_ != NO_CELL) return;
    /* Paused is paused, with the card up or after Keep waiting took it away:
     * the status line says who is being waited for, and the sea waits too. */
    if (mode_ == Mode::Remote && watch_.paused()) return;

    const uint8_t cell = cellAt(touch.x, touch.y);
    if (cell == NO_CELL) return;
    fireAt(host, cell);
}

