#pragma once

#include <Arduino.h>
#include "hal/BleBeacon.h"

/* Passive BLE observer for other Braino devices.
 *
 * This module is the radio half of Nearby play and nothing else: it listens,
 * decides whether an advertisement is one of ours, and records what it said.
 * It holds no opinion about scores, profiles or notifications -- that policy
 * lives in engine/NearbyPlay, which is where the score catalog and the local
 * records are. Keeping the split means the hal never has to know what a game
 * is, and the policy never has to know what an AD structure is.
 *
 * Three properties are deliberate:
 *
 * 1. **The scan is passive.** Passive scanning never transmits a scan request,
 *    so listening for peers adds nothing to what this device puts on air. The
 *    beacon payload remains the only thing we broadcast.
 * 2. **Peers are decoded with BleBeacon::decode().** That is the exact inverse
 *    of the function that builds our own payload, so there is one description
 *    of the wire format rather than a transmit copy and a receive copy that
 *    can drift.
 * 3. **Nothing allocates.** The sighting table is a fixed static array and the
 *    advertisement is parsed straight out of the controller's buffer, because
 *    the NimBLE callback runs on the host task at whatever rate the air is
 *    busy -- exactly the churn that fragments this heap.
 */
namespace BleScan {

/* How many distinct devices we will track. A classroom or a living room does
 * not have more than this within a few metres, and a fixed cap is what keeps
 * the table static. The weakest signal is evicted when a ninth appears. */
constexpr uint8_t MAX_SIGHTINGS = 8;

/** A peer is forgotten after this long without hearing from it. */
constexpr uint32_t SIGHTING_TTL_MS = 45000;

struct Sighting {
    char deviceId[5] = {0};
    bool sharesActivity = false;
    uint8_t gameIndex = BleBeacon::GAME_NONE;
    uint32_t bestScore = 0;
    int8_t rssi = 0;
    uint32_t lastSeenMs = 0;
};

/** Start or stop observing. Idempotent; a no-op when the stack is down. */
void setEnabled(bool on);

/** The setting. True does not prove the controller took it -- see scanning(). */
bool enabled();

/** True only while the controller is actually scanning. */
bool scanning();

/* Expire stale sightings and restart the scan if it lapsed. Called once per
 * frame from the runtime; does no work worth measuring when nothing changed
 * and never blocks. */
void tick();

/** Drop everything heard so far -- used when the radio is turned off. */
void clear();

uint8_t count();

/* Sightings are ordered strongest signal first, so index 0 is the nearest
 * device. Reading past count() returns a zeroed entry rather than garbage. */
Sighting at(uint8_t index);

/* Bumps every time the table gains, loses or updates an entry. A caller that
 * only wants to react to changes can compare this instead of diffing rows. */
uint32_t generation();

}   // namespace BleScan
