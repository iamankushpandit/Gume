#pragma once

#include <Arduino.h>

#include "hal/BleScanner.h"

class Board;
struct AppDefinition;

/* Nearby play -- the policy half of the anonymous score exchange.
 *
 * hal/BleScanner listens and hal/BleBeacon transmits; neither knows what a
 * game or a score is. This is where those become meaning: which playable app
 * an index refers to, whether a peer's number beats the record on this device,
 * and what the owner should be told about it.
 *
 * ------------------------------------------------------------------------
 * What is anonymous here, and why
 * ------------------------------------------------------------------------
 * A peer is only ever a four hex digit device id taken from its Bluetooth MAC.
 * Profile names are never read by this module, never mind transmitted, and the
 * only numbers that travel are a game index and a best score. Two players
 * therefore learn that "someone with a Braino nearby has 42 on Maze" and
 * nothing whatever about each other. That is the whole design: the fun of a
 * leaderboard without a way to find out who is on it.
 *
 * The feature is off by default and requires the BLE beacon to be on. Turning
 * the beacon off turns this off with it -- there is no path that listens or
 * shares while the radio setting says the device is quiet.
 */
namespace NearbyPlay {

/* How long one notification stays in the header before it is removed. Long
 * enough to read a short line, short enough that it is gone before it becomes
 * furniture. */
constexpr uint32_t BANNER_MS = 5000;

/** Queued notifications. Older ones are dropped when a burst overflows. */
constexpr uint8_t MAX_EVENTS = 6;

/** Longest notification text, sized to the top bar at font 2. */
constexpr uint8_t BANNER_MAX = 40;

/* One peer, resolved against this device's catalog and records. Everything
 * here is derived at read time -- nothing is stored between calls. */
struct PeerView {
    char deviceId[5] = {0};
    const char* gameTitle = nullptr;   // nullptr: idle, or a game we don't have
    bool sharing = false;
    uint32_t theirScore = 0;
    bool haveOwnScore = false;
    uint32_t yourScore = 0;
    bool beatsYou = false;
    const char* unit = nullptr;
    int8_t rssi = 0;
};

/* Read the stored setting and arm the radio accordingly. Call once from the
 * runtime after Board::begin(), which is what brings the beacon up. */
void begin(Board& board);

/** Turn the exchange on or off and persist it. Requires the beacon to be on. */
void setEnabled(Board& board, bool on);

/** The setting, from RAM. Does not by itself prove the radio is listening. */
bool enabled();

/** True only while the beacon is advertising and the scanner is listening. */
bool active();

/* Publish which game is open. Pass nullptr for "back at the launcher". Reads
 * the best score for that game from the active profile and hands both to the
 * beacon; a no-op when sharing is off or the app records no score. */
void setActiveApp(Board& board, const AppDefinition* app);

/* Re-read and re-publish the score for the app that is already open. Call
 * after a record is saved, so a peer sees the new number rather than the one
 * that was current when the screen opened. */
void refreshScore(Board& board);

/* Once per frame from the runtime. Expires sightings, raises notifications for
 * anything that changed and ages out the banner. Does nothing measurable when
 * the feature is off. */
void tick(Board& board);

/* The notification currently being shown, or nullptr when there is none. The
 * runtime paints this over the header and repaints the header underneath once
 * it goes away, which is what "and then remove them" means here. */
const char* banner();

/* Bumps whenever the banner appears or disappears, so the runtime can tell
 * that the screen underneath needs repainting without diffing strings. */
uint32_t bannerGeneration();

/** How many peers are currently in range, strongest signal first. */
uint8_t peerCount();

/* Resolve one peer against the catalog and this profile's records. Reads NVS,
 * so build a screen's rows from it on change rather than every frame. */
PeerView peerAt(Board& board, uint8_t index);

/** Bumps when the peer table changes -- the stale flag for a screen. */
uint32_t peerGeneration();

}   // namespace NearbyPlay
