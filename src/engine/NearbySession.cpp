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
    out.inviting = k.inviting;
    /* An invitation states its game (see BleBeacon::invitePeer), and while it
     * is on air that is the game the peer advertises -- so this is the one
     * comparison that stops a lobby accepting somebody else's game. */
    out.forThisGame = k.inviting && k.lastGame == activeGameIndex_ &&
                      activeGameIndex_ != BleBeacon::GAME_NONE;
    out.session = static_cast<uint8_t>(k.inviteSession & SESSION_MASK);
    out.weMoveFirst = (k.inviteSession & SIDE_BIT) != 0;
}
}   // namespace

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
    return BleBeacon::invitePeer(deviceId, payload, activeGameIndex_);
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
    BleBeacon::setTurn(session, ply, from, to, ack);
}

void publishEnd(uint8_t session, uint8_t ply, uint8_t ack) {
    if (!sessionsAllowed()) {
        return;
    }
    /* Left on the air like any other turn rather than sent once. The other
     * seat may be anywhere in its scan cycle, and "I am stopping" is precisely
     * the message you cannot ask somebody to repeat. */
    BleBeacon::setTurn(session, ply, END_SQUARE, END_SQUARE, ack);
}

void stopTurns() {
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
