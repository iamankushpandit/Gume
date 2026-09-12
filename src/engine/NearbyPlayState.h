#pragma once

/* NearbyPlay's shared state, for the two files that implement it and nobody
 * else.
 *
 * NearbyPlay.cpp is the score exchange and the notifications; NearbySession.cpp
 * is the two-player session half. They were one file until it passed the size
 * the modularity rule allows, and both read the same peer table, so the table
 * moved here from an anonymous namespace rather than being duplicated or
 * reached through accessors that exist only to cross a file boundary.
 *
 * Nothing outside src/engine/NearbyPlay*.cpp and NearbySession.cpp may include
 * this. The public surface is NearbyPlay.h. */

#include <Arduino.h>

#include "hal/BleBeacon.h"
#include "hal/BleScanner.h"

namespace NearbyPlay {
namespace detail {

/* What we have already told the owner about each peer, so a beacon repeating
 * itself once a second does not repeat its notification once a second. */
struct Known {
    char deviceId[5] = {0};
    uint8_t lastGame = BleBeacon::GAME_NONE;
    uint32_t lastScore = 0;
    bool announcedBeat = false;
    uint8_t announcedGame = BleBeacon::GAME_NONE;
    uint32_t announcedScore = 0;

    /* The last poke from this peer that we acted on. A poke is transmitted
     * repeatedly for several seconds so a scan window cannot miss it, so
     * "have I already reacted to this one?" is the whole of what makes it an
     * event rather than a six-second alarm. sawPoke distinguishes "no poke
     * yet" from "the last one happened to be nonce 0". */
    bool sawPoke = false;
    uint8_t lastPokeNonce = 0;

    /* The latest two-player session traffic heard from this peer. Kept raw and
     * unjudged: whether a move is legal, expected, or even in the right game
     * is the app's business, and this module has no idea what the numbers
     * mean. */
    bool inviting = false;
    char inviteTarget[5] = {0};
    uint8_t inviteSession = 0;
    /* The last invitation from this peer we raised a banner for. Same
     * reasoning as sawPoke: an invitation repeats for seconds so a scan window
     * cannot miss it, so "have I already announced this one?" is the whole of
     * what makes it an event rather than an alarm. */
    bool sawInvite = false;
    uint8_t lastInviteByte = 0;

    /* When the scanner last heard this peer, copied straight off the sighting.
     *
     * It is the only thing in this table that says a peer is still THERE, as
     * opposed to what it last said. A two-player game needs that: a console
     * whose battery dies stops advertising and says nothing about it, so the
     * absence is the entire signal. See NearbyPlay::peerSilentMs(). */
    uint32_t lastSeenMs = 0;

    bool hasTurn = false;
    uint8_t turnSession = 0;
    uint8_t turnPly = 0;
    uint8_t turnFrom = 0;
    uint8_t turnTo = 0;
    uint8_t turnAck = 0;
};

extern bool enabled_;
extern uint8_t activeGameIndex_;
extern Known known_[BleScan::MAX_SIGHTINGS];
extern uint8_t knownCount_;

}   // namespace detail
}   // namespace NearbyPlay
