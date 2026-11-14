// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "NearbyPlay.h"
#include "NearbyPlayState.h"

#include <esp_system.h>   // esp_random(), for the who-moves-first toss
#include <string.h>

#include "hal/BleBeacon.h"
#include "hal/Board.h"

/* The two-player session half of NearbyPlay: seats, invitations, turns and the
 * reserved ending. Split from NearbyPlay.cpp, which keeps the score exchange
 * and the notifications; both read the peer table in NearbyPlayState.h. */

namespace NearbyPlay {

using namespace detail;

/* ---- two-player sessions ------------------------------------------------
 *
 * The gate is re-derived on every call rather than cached, for the same
 * reason tick() re-derives it: an ordering contract with Settings is a thing
 * that can be got wrong once and then stays wrong. */
namespace {
bool sessionsAllowed() {
    return enabled_ && BleBeacon::active();
}

/* When this console's turn on the air last changed session -- a game began,
 * or one came back to the air. A peer cannot be missed from a session before
 * the session had started. */
uint32_t sessionSinceMs_ = 0;

void noteOurSession(uint8_t session) {
    const BleBeacon::Advertisement& adv = BleBeacon::configured();
    if (!adv.hasTurn || adv.turnSession != (session & 0x3F)) {
        sessionSinceMs_ = millis();
    }
}

/* When a finished game's last word is to leave the air, or 0. See
 * stopTurnsLater(). */
uint32_t stopAtMs_ = 0;

/* Which game put the turn that is on the air there. A game left in the
 * middle keeps its last move on the air, so the turn can belong to a game
 * that is not the one on screen -- and that other game's nearbyStop(), on
 * its own way out, must not take it down. GAME_NONE: nobody's. */
uint8_t turnOwner_ = BleBeacon::GAME_NONE;

/* How an invitation's one payload byte is divided.
 *
 * Six bits of session id -- the app's own name for the game about to happen,
 * and the width the move block has room for -- plus one bit saying which of
 * the two consoles moves first. The top bit is spare.
 *
 * The side bit is here rather than in the game because it is the same question
 * every two-player game asks, and because the console doing the asking is
 * exactly the one that should not be answering it. */
constexpr uint8_t SESSION_MASK = 0x3F;
constexpr uint8_t SIDE_BIT = 0x40;      // set: the INVITED console moves first

/* A turn that means "I am stopping", rather than a move.
 *
 * `from == to` is not a move in any game that moves a thing from somewhere to
 * somewhere else, so it is safe to reserve -- but 0,0 is what a console
 * publishes at ply 0 to answer an invitation, and that is a presence and not
 * an ending. Requiring BOTH squares to be 63 separates the two without
 * depending on anybody checking the ply first. A game never sees this: it sees
 * NearbyTurn::ended. */
constexpr uint8_t END_SQUARE = 63;
bool encodesEnd(uint8_t from, uint8_t to) {
    return from == END_SQUARE && to == END_SQUARE;
}
}   // namespace

uint8_t seatCount() {
    return sessionsAllowed() ? knownCount_ : 0;
}

namespace {
/* Is this peer saying a move now? The move is kept once heard, so this asks
 * how lately it was on the air. */
bool sayingTurn(const Known& k) {
    return k.hasTurn && millis() - k.lastTurnMs < PEER_IN_GAME_MS;
}

/* One place that turns a Known into the seat a game is handed, so the lobby
 * and the invitation path cannot describe the same peer differently. */
void fillSeat(Board& board, const Known& k, NearbySeat& out) {
    strncpy(out.deviceId, k.deviceId, sizeof(out.deviceId) - 1);
    out.deviceId[sizeof(out.deviceId) - 1] = 0;
    const char* label = board.peerName(k.deviceId);
    if (label != nullptr) {
        strncpy(out.name, label, sizeof(out.name) - 1);
        out.name[sizeof(out.name) - 1] = 0;
    } else {
        out.name[0] = 0;
    }
    /* An invitation is a broadcast, heard by every console in the room, and
     * it names the one it is for. A lobby is told only about the ones for
     * this console: shown the others, a third console could accept a game
     * nobody was offering it and sit in it for ever. */
    const char* mine = BleBeacon::configured().deviceId;
    const bool forUs = k.inviting && mine[0] != 0 &&
                       strncmp(k.inviteTarget, mine, sizeof(k.inviteTarget)) == 0;
    out.inviting = forUs;
    /* An invitation states its game (see BleBeacon::invitePeer), and while it
     * is on air that is the game the peer advertises -- so this is the one
     * comparison that stops a lobby accepting somebody else's game. */
    out.forThisGame = forUs && k.lastGame == activeGameIndex_ &&
                      activeGameIndex_ != BleBeacon::GAME_NONE;
    out.session = static_cast<uint8_t>(k.inviteSession & SESSION_MASK);
    out.weMoveFirst = (k.inviteSession & SIDE_BIT) != 0;
    out.silentMs = millis() - k.lastSeenMs;
    /* Asking somebody else to play the app open here. */
    out.asking = k.inviting && !forUs && k.lastGame == activeGameIndex_ &&
                 activeGameIndex_ != BleBeacon::GAME_NONE;

    /* In a game: a move heard from it lately that is not an ending. "Lately"
     * rather than "on the last advertisement", because a poke takes the
     * move's place for a few seconds and a scan misses some. */
    out.inGame = sayingTurn(k) && !encodesEnd(k.turnFrom, k.turnTo);
    out.partner[0] = 0;
    out.partnerName[0] = 0;
    out.withUs = false;
    if (out.inGame) {
        const BleBeacon::Advertisement& adv = BleBeacon::configured();
        out.withUs = adv.hasTurn && adv.turnSession == k.turnSession;
        for (uint8_t i = 0; i < knownCount_ && !out.withUs; ++i) {
            const Known& o = known_[i];
            if (&o == &k || !sayingTurn(o) || o.turnSession != k.turnSession) continue;
            strncpy(out.partner, o.deviceId, sizeof(out.partner) - 1);
            out.partner[sizeof(out.partner) - 1] = 0;
            const char* partnerLabel = board.peerName(o.deviceId);
            if (partnerLabel != nullptr) {
                strncpy(out.partnerName, partnerLabel, sizeof(out.partnerName) - 1);
                out.partnerName[sizeof(out.partnerName) - 1] = 0;
            }
            break;
        }
    } else if (out.asking) {
        /* Who it asked, in the same two fields: the one it is waiting on. */
        strncpy(out.partner, k.inviteTarget, sizeof(out.partner) - 1);
        out.partner[sizeof(out.partner) - 1] = 0;
        const char* askedLabel = board.peerName(k.inviteTarget);
        if (askedLabel != nullptr) {
            strncpy(out.partnerName, askedLabel, sizeof(out.partnerName) - 1);
            out.partnerName[sizeof(out.partnerName) - 1] = 0;
        }
    }
}
}   // namespace

uint32_t peerAwayMs(const char* deviceId) {
    if (!sessionsAllowed() || deviceId == nullptr) {
        return 0;
    }
    /* Our session is the one our own turn is in. With none on the air there
     * is nothing for the peer to be away from. */
    const BleBeacon::Advertisement& adv = BleBeacon::configured();
    if (!adv.hasTurn) {
        return 0;
    }
    for (uint8_t i = 0; i < knownCount_; ++i) {
        const Known& k = known_[i];
        if (strncmp(k.deviceId, deviceId, sizeof(k.deviceId)) != 0) continue;
        /* Counted from the later of its last move in our session and the
         * moment our session went on the air: the console that accepts an
         * invitation says its first turn before the other has said any. */
        uint32_t since = sessionSinceMs_;
        if (k.hasTurn && k.turnSession == adv.turnSession &&
            static_cast<int32_t>(k.lastTurnMs - since) > 0) {
            since = k.lastTurnMs;
        }
        return millis() - since;
    }
    /* Not heard at all: that is silence, which peerSilentMs() reports. */
    return 0;
}

uint32_t peerSilentMs(const char* deviceId) {
    if (!sessionsAllowed() || deviceId == nullptr) {
        return PEER_SILENT_UNKNOWN;
    }
    for (uint8_t i = 0; i < knownCount_; ++i) {
        const Known& k = known_[i];
        if (strncmp(k.deviceId, deviceId, sizeof(k.deviceId)) == 0) {
            return millis() - k.lastSeenMs;
        }
    }
    /* Not in the table: BleScan dropped it at SIGHTING_TTL_MS and reconcile()
     * forgot it, or it was never here. Either way we know nothing current. */
    return PEER_SILENT_UNKNOWN;
}

bool seatAt(Board& board, uint8_t index, NearbySeat& out) {
    if (!sessionsAllowed() || index >= knownCount_) {
        return false;
    }
    fillSeat(board, known_[index], out);
    return true;
}

bool invite(const char* deviceId, uint8_t session, bool& weMoveFirst) {
    weMoveFirst = false;
    if (!sessionsAllowed()) {
        return false;
    }
    /* The coin, flipped here so that no game has to remember to be fair and
     * so that two games cannot be fair in two different ways. esp_random() is
     * the hardware generator: this does not need to be unpredictable to an
     * attacker, but it does need to not be the same every time, which is
     * exactly what a seeded PRNG on a device with no clock would give. */
    weMoveFirst = (esp_random() & 1u) != 0;
    const uint8_t payload = static_cast<uint8_t>(
        (session & SESSION_MASK) | (weMoveFirst ? 0 : SIDE_BIT));
    /* Which game, stated by the service rather than left for the caller to
     * remember: the app asking is the app that is running, this module already
     * tracks that, and an invitation the receiver cannot name is not much of
     * an invitation. */
    /* An invitation takes the turn's place on the air (the payload has room
     * for one or the other), so whatever turn was there is nobody's now. */
    turnOwner_ = BleBeacon::GAME_NONE;
    return BleBeacon::invitePeer(deviceId, payload, activeGameIndex_);
}

uint8_t freshSession(uint8_t lowBits, uint8_t lowBitCount) {
    /* Six bits from the clock, then stepped past every session this console
     * still holds a turn for. A peer's last turn is kept for as long as the
     * peer is in the room, and ours may still be on the air from a game left
     * in the middle: a new game given either number would read that old turn
     * -- or an old ending -- as its own. A number a peer is offering somebody
     * now is passed over too. Seventeen numbers at most are taken; with four
     * low bits fixed only four are left to choose from, and if every one of
     * them is taken the last is used -- a clash then needs a remembered turn
     * with the same low bits as well, and each game still refuses a move
     * that is not the ply it expects or is not legal where it stands. */
    if (lowBitCount > 4) lowBitCount = 4;
    const uint8_t step = static_cast<uint8_t>(1U << lowBitCount);
    const uint8_t low = static_cast<uint8_t>(lowBits & (step - 1));
    uint8_t session = static_cast<uint8_t>(
        ((((millis() >> 3) & SESSION_MASK) & ~(step - 1)) | low) & SESSION_MASK);
    for (uint8_t tries = 0; tries < (SESSION_MASK + 1) / step; ++tries) {
        bool taken = false;
        const BleBeacon::Advertisement& adv = BleBeacon::configured();
        if (adv.hasTurn && adv.turnSession == session) taken = true;
        for (uint8_t i = 0; i < knownCount_ && !taken; ++i) {
            const Known& k = known_[i];
            if (k.hasTurn && k.turnSession == session) taken = true;
            if (k.inviting && (k.inviteSession & SESSION_MASK) == session) taken = true;
        }
        if (!taken) break;
        session = static_cast<uint8_t>((session + step) & SESSION_MASK);
    }
    return session;
}

bool inviteForUs(Board& board, NearbySeat& out) {
    if (!sessionsAllowed()) {
        return false;
    }
    const char* mine = BleBeacon::configured().deviceId;
    for (uint8_t i = 0; i < knownCount_; ++i) {
        const Known& k = known_[i];
        if (!k.inviting) continue;
        if (strncmp(k.inviteTarget, mine, sizeof(k.inviteTarget)) != 0) continue;
        fillSeat(board, k, out);
        return true;
    }
    return false;
}

void publishTurn(uint8_t session, uint8_t ply, uint8_t from, uint8_t to,
                 uint8_t ack) {
    if (!sessionsAllowed()) {
        return;
    }
    noteOurSession(session);
    BleBeacon::setTurn(session, ply, from, to, ack);
    turnOwner_ = activeGameIndex_;
    stopAtMs_ = 0;
}

void publishEnd(uint8_t session, uint8_t ply, uint8_t ack) {
    if (!sessionsAllowed()) {
        return;
    }
    /* Left on the air like any other turn rather than sent once. The other
     * seat may be anywhere in its scan cycle, and "I am stopping" is precisely
     * the message you cannot ask somebody to repeat. */
    noteOurSession(session);
    BleBeacon::setTurn(session, ply, END_SQUARE, END_SQUARE, ack);
    turnOwner_ = activeGameIndex_;
    stopAtMs_ = 0;
}

void stopTurns() {
    /* An invitation on the air is always the caller's: only the game on
     * screen can send one. Left up, it could be accepted after the game that
     * sent it had gone, and the peer would wait in a game nobody is in. */
    BleBeacon::clearInvite();
    /* The turn is taken down only by the game that put it up. */
    if (turnOwner_ != BleBeacon::GAME_NONE && turnOwner_ != activeGameIndex_) {
        return;
    }
    turnOwner_ = BleBeacon::GAME_NONE;
    stopAtMs_ = 0;
    BleBeacon::clearTurn();
}

void stopTurnsLater() {
    /* Only the game whose turn it is may set it going. */
    if (turnOwner_ != activeGameIndex_ || !BleBeacon::configured().hasTurn) {
        return;
    }
    stopAtMs_ = millis() + LAST_WORD_MS;
    if (stopAtMs_ == 0) stopAtMs_ = 1;
}

void tickSessions() {
    if (stopAtMs_ == 0 || static_cast<int32_t>(millis() - stopAtMs_) < 0) {
        return;
    }
    stopAtMs_ = 0;
    turnOwner_ = BleBeacon::GAME_NONE;
    BleBeacon::clearTurn();
}

void dropTurnsForOff() {
    stopAtMs_ = 0;
    /* Nearby or the beacon was switched off. Sessions are over, whichever
     * game they belonged to: a turn left in the payload would keep its flag
     * on the air with sharing off, and come back, stale, when it was switched
     * on again. A game still in play says its turn again once it may. */
    turnOwner_ = BleBeacon::GAME_NONE;
    BleBeacon::clearInvite();
    BleBeacon::clearTurn();
}

bool turnFrom(const char* deviceId, uint8_t session, NearbyTurn& out) {
    if (!sessionsAllowed() || deviceId == nullptr) {
        return false;
    }
    for (uint8_t i = 0; i < knownCount_; ++i) {
        const Known& k = known_[i];
        if (!k.hasTurn) continue;
        if (strncmp(k.deviceId, deviceId, sizeof(k.deviceId)) != 0) continue;
        if (k.turnSession != (session & SESSION_MASK)) continue;
        out.session = k.turnSession;
        out.ply = k.turnPly;
        out.from = k.turnFrom;
        out.to = k.turnTo;
        out.ack = k.turnAck;
        out.ended = encodesEnd(k.turnFrom, k.turnTo);
        return true;
    }
    return false;
}

const char* selfId() {
    return sessionsAllowed() ? BleBeacon::configured().deviceId : "";
}

}   // namespace NearbyPlay
