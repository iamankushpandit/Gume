#include "AppRuntime.h"

#include "engine/NearbyPlay.h"

/* ---------------------------------------------------- nearby two-player
 *
 * Forwarding only. NearbyPlay decides what is allowed and what a sighting
 * means; this just narrows it to the shape an app is given. */
uint8_t BrainoApp::nearbySeatCount() {
    return NearbyPlay::seatCount();
}

bool BrainoApp::nearbySeatAt(uint8_t index, NearbySeat& out) {
    return NearbyPlay::seatAt(board_, index, out);
}

bool BrainoApp::nearbyInvite(const char* deviceId, uint8_t session,
                             bool& weMoveFirst) {
    return NearbyPlay::invite(deviceId, session, weMoveFirst);
}

bool BrainoApp::nearbyInviteForUs(NearbySeat& out) {
    return NearbyPlay::inviteForUs(board_, out);
}

void BrainoApp::nearbyPublish(uint8_t session, uint8_t ply, uint8_t from,
                              uint8_t to, uint8_t ack) {
    NearbyPlay::publishTurn(session, ply, from, to, ack);
}

void BrainoApp::nearbyEnd(uint8_t session, uint8_t ply, uint8_t ack) {
    NearbyPlay::publishEnd(session, ply, ack);
}

void BrainoApp::nearbyStop() {
    NearbyPlay::stopTurns();
}

bool BrainoApp::nearbyTurnFrom(const char* deviceId, uint8_t session,
                               NearbyTurn& out) {
    return NearbyPlay::turnFrom(deviceId, session, out);
}

const char* BrainoApp::nearbySelfId() {
    return NearbyPlay::selfId();
}

uint32_t BrainoApp::nearbyPeerSilentMs(const char* deviceId) {
    return NearbyPlay::peerSilentMs(deviceId);
}