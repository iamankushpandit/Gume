#include "NearbyPlay.h"

#include <string.h>

#include "engine/AppRegistry.h"
#include "hal/BleBeacon.h"
#include "hal/Board.h"

namespace NearbyPlay {
namespace {

bool enabled_ = false;

/* Which playable app is open, as an index into the playable registry. This is
 * the only thing about the current session that reaches the radio, and it is
 * an index into a table both devices compile from the same source -- which is
 * why the payload carries a layout version that peers must match. */
uint8_t activeGameIndex_ = BleBeacon::GAME_NONE;

/* What we have already told the owner about each peer, so a beacon repeating
 * itself once a second does not repeat its notification once a second. */
struct Known {
    char deviceId[5] = {0};
    uint8_t lastGame = BleBeacon::GAME_NONE;
    uint32_t lastScore = 0;
    bool announcedBeat = false;
    uint8_t announcedGame = BleBeacon::GAME_NONE;
    uint32_t announcedScore = 0;
};

Known known_[BleScan::MAX_SIGHTINGS];
uint8_t knownCount_ = 0;
uint32_t lastScanGeneration_ = 0;
uint32_t peerGeneration_ = 0;

char events_[MAX_EVENTS][BANNER_MAX];
uint8_t eventHead_ = 0;
uint8_t eventCount_ = 0;

char banner_[BANNER_MAX] = {0};
bool bannerActive_ = false;
uint32_t bannerAtMs_ = 0;
uint32_t bannerGeneration_ = 0;

const AppDefinition* playableAt(uint8_t index) {
    if (index >= playableAppCount()) {
        return nullptr;
    }
    return &playableAppAt(index);
}

/** Title of a peer's game, or nullptr when the index is not one we know. */
const char* gameTitle(uint8_t index) {
    const AppDefinition* app = playableAt(index);
    return app != nullptr ? app->title() : nullptr;
}

const AppScoreInfo* gameScore(uint8_t index) {
    const AppDefinition* app = playableAt(index);
    return app != nullptr ? app->score() : nullptr;
}

bool better(uint32_t candidate, uint32_t incumbent, bool lowerIsBetter) {
    return lowerIsBetter ? candidate < incumbent : candidate > incumbent;
}

void pushEvent(const char* text) {
    if (eventCount_ == MAX_EVENTS) {
        /* A burst is more interesting at its newest end: drop the oldest
         * rather than refusing the arrival. */
        eventHead_ = static_cast<uint8_t>((eventHead_ + 1) % MAX_EVENTS);
        --eventCount_;
    }
    const uint8_t slot = static_cast<uint8_t>((eventHead_ + eventCount_) % MAX_EVENTS);
    snprintf(events_[slot], BANNER_MAX, "%s", text);
    ++eventCount_;
}

Known* findKnown(const char* deviceId) {
    for (uint8_t i = 0; i < knownCount_; ++i) {
        if (strncmp(known_[i].deviceId, deviceId, sizeof(known_[i].deviceId)) == 0) {
            return &known_[i];
        }
    }
    return nullptr;
}

/* Decide whether this peer's number beats the record on this device and, if
 * it does and we have not said so already, say so once.
 *
 * The NVS read is the reason this is only reached when a peer's game or score
 * actually changed: a beacon repeats every second, and looking a record up at
 * that rate would put a flash lookup inside the frame budget for no new
 * information. */
void evaluateScore(Board& board, Known& entry, const BleScan::Sighting& seen) {
    char text[BANNER_MAX];

    if (!seen.sharesActivity || seen.gameIndex == BleBeacon::GAME_NONE) {
        return;
    }
    const AppScoreInfo* score = gameScore(seen.gameIndex);
    const char* title = gameTitle(seen.gameIndex);
    if (score == nullptr || title == nullptr) {
        return;
    }
    if (!board.hasScore(score->bestKey)) {
        return;   // nothing of ours to beat yet
    }
    const uint32_t mine = board.getScore(score->bestKey);
    if (!better(seen.bestScore, mine, score->lowerIsBetter)) {
        return;
    }
    if (entry.announcedBeat && entry.announcedGame == seen.gameIndex &&
        entry.announcedScore == seen.bestScore) {
        return;
    }
    entry.announcedBeat = true;
    entry.announcedGame = seen.gameIndex;
    entry.announcedScore = seen.bestScore;
    snprintf(text, sizeof(text), "%s beat your %s: %lu", seen.deviceId, title,
             static_cast<unsigned long>(seen.bestScore));
    pushEvent(text);
}

/* Fold the current sightings into what we already knew, raising one
 * notification per thing that actually changed. */
void reconcile(Board& board) {
    char text[BANNER_MAX];
    const uint8_t seenCount = BleScan::count();

    bool stillHere[BleScan::MAX_SIGHTINGS] = {false};

    for (uint8_t i = 0; i < seenCount; ++i) {
        const BleScan::Sighting seen = BleScan::at(i);
        if (seen.deviceId[0] == '\0') {
            continue;
        }
        Known* entry = findKnown(seen.deviceId);
        if (entry == nullptr) {
            if (knownCount_ >= BleScan::MAX_SIGHTINGS) {
                continue;
            }
            entry = &known_[knownCount_++];
            *entry = Known{};
            snprintf(entry->deviceId, sizeof(entry->deviceId), "%s", seen.deviceId);
            entry->lastGame = seen.gameIndex;
            entry->lastScore = seen.bestScore;

            const char* title = gameTitle(seen.gameIndex);
            if (seen.sharesActivity && title != nullptr) {
                snprintf(text, sizeof(text), "%s nearby, playing %s", seen.deviceId, title);
            } else {
                snprintf(text, sizeof(text), "%s is nearby", seen.deviceId);
            }
            pushEvent(text);
            evaluateScore(board, *entry, seen);
        } else {
            const bool gameChanged = entry->lastGame != seen.gameIndex;
            const bool scoreChanged = entry->lastScore != seen.bestScore;
            entry->lastGame = seen.gameIndex;
            entry->lastScore = seen.bestScore;

            if (gameChanged) {
                const char* title = gameTitle(seen.gameIndex);
                if (seen.sharesActivity && title != nullptr) {
                    snprintf(text, sizeof(text), "%s is playing %s", seen.deviceId, title);
                    pushEvent(text);
                }
            }
            if (gameChanged || scoreChanged) {
                evaluateScore(board, *entry, seen);
            }
        }

        for (uint8_t k = 0; k < knownCount_; ++k) {
            if (&known_[k] == entry) {
                stillHere[k] = true;
                break;
            }
        }
    }

    /* Forget anyone who has walked away, so returning later reads as an
     * arrival again rather than silently resuming. */
    uint8_t out = 0;
    for (uint8_t i = 0; i < knownCount_; ++i) {
        if (stillHere[i]) {
            if (out != i) {
                known_[out] = known_[i];
            }
            ++out;
        }
    }
    knownCount_ = out;
    ++peerGeneration_;
}

/* Hand the beacon the game and score to advertise, or clear both. Cheap and
 * idempotent -- BleBeacon::setActivity() only touches the radio when the bytes
 * change. */
void publish(Board& board) {
    if (!enabled_) {
        BleBeacon::setActivity(false, BleBeacon::GAME_NONE, 0);
        return;
    }
    const AppDefinition* app = playableAt(activeGameIndex_);
    const AppScoreInfo* score = app != nullptr ? app->score() : nullptr;
    if (score == nullptr) {
        /* Sharing stays on -- peers should still see us in the room -- but
         * there is no game or number to attach. */
        BleBeacon::setActivity(true, BleBeacon::GAME_NONE, 0);
        return;
    }
    BleBeacon::setActivity(true, activeGameIndex_, board.getScore(score->bestKey));
}

void resetPeers() {
    knownCount_ = 0;
    eventHead_ = 0;
    eventCount_ = 0;
    bannerActive_ = false;
    banner_[0] = '\0';
    ++bannerGeneration_;
    ++peerGeneration_;
}

}   // namespace

void begin(Board& board) {
    enabled_ = board.nearbyEnabled() && board.bleBeaconEnabled();
    BleScan::setEnabled(enabled_);
    publish(board);
}

void setEnabled(Board& board, bool on) {
    /* The beacon is the master switch. Nothing here listens or shares while
     * the device's radio setting says it is quiet. */
    const bool want = on && board.bleBeaconEnabled();
    board.setNearbyEnabled(on);
    if (want == enabled_) {
        return;
    }
    enabled_ = want;
    BleScan::setEnabled(enabled_);
    if (!enabled_) {
        BleScan::clear();
        resetPeers();
    }
    publish(board);
}

bool enabled() { return enabled_; }

bool active() {
    return enabled_ && BleBeacon::active() && BleScan::scanning();
}

void setActiveApp(Board& board, const AppDefinition* app) {
    uint8_t index = BleBeacon::GAME_NONE;
    if (app != nullptr) {
        for (uint8_t i = 0; i < playableAppCount(); ++i) {
            if (&playableAppAt(i) == app) {
                index = i;
                break;
            }
        }
    }
    if (index == activeGameIndex_) {
        return;
    }
    activeGameIndex_ = index;
    publish(board);
}

void refreshScore(Board& board) {
    publish(board);
}

void tick(Board& board) {
    /* One gate, re-evaluated every frame, rather than an ordering contract
     * between Settings and the radio. The beacon can go down underneath us --
     * turning it off in Settings tears the whole stack down -- and it can come
     * back; either way this feature follows it instead of sitting there
     * claiming a state the radio is not in. Both reads are from RAM. */
    const bool want = board.nearbyEnabled() && BleBeacon::enabled();
    if (want != enabled_) {
        enabled_ = want;
        BleScan::setEnabled(enabled_);
        if (!enabled_) {
            resetPeers();
        }
        publish(board);
    }
    if (!enabled_) {
        return;
    }

    BleScan::tick();

    const uint32_t generation = BleScan::generation();
    if (generation != lastScanGeneration_) {
        lastScanGeneration_ = generation;
        reconcile(board);
    }

    const uint32_t now = millis();
    if (bannerActive_ && now - bannerAtMs_ >= BANNER_MS) {
        bannerActive_ = false;
        banner_[0] = '\0';
        ++bannerGeneration_;
    }
    if (!bannerActive_ && eventCount_ > 0) {
        snprintf(banner_, sizeof(banner_), "%s", events_[eventHead_]);
        eventHead_ = static_cast<uint8_t>((eventHead_ + 1) % MAX_EVENTS);
        --eventCount_;
        bannerActive_ = true;
        bannerAtMs_ = now;
        ++bannerGeneration_;
    }
}

const char* banner() {
    return bannerActive_ ? banner_ : nullptr;
}

uint32_t bannerGeneration() { return bannerGeneration_; }

uint8_t peerCount() {
    return enabled_ ? BleScan::count() : 0;
}

PeerView peerAt(Board& board, uint8_t index) {
    PeerView view;
    if (!enabled_) {
        return view;
    }
    const BleScan::Sighting seen = BleScan::at(index);
    snprintf(view.deviceId, sizeof(view.deviceId), "%s", seen.deviceId);
    view.sharing = seen.sharesActivity;
    view.rssi = seen.rssi;
    view.theirScore = seen.bestScore;
    view.gameTitle = seen.sharesActivity ? gameTitle(seen.gameIndex) : nullptr;

    const AppScoreInfo* score = seen.sharesActivity ? gameScore(seen.gameIndex) : nullptr;
    if (score == nullptr) {
        return view;
    }
    view.unit = score->unit;
    view.haveOwnScore = board.hasScore(score->bestKey);
    if (view.haveOwnScore) {
        view.yourScore = board.getScore(score->bestKey);
        view.beatsYou = better(seen.bestScore, view.yourScore, score->lowerIsBetter);
    }
    return view;
}

uint32_t peerGeneration() { return peerGeneration_; }

}   // namespace NearbyPlay
