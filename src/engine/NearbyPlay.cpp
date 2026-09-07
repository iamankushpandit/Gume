#include "NearbyPlay.h"

#include <esp_system.h>   // esp_random(), for the who-moves-first toss
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

    bool hasTurn = false;
    uint8_t turnSession = 0;
    uint8_t turnPly = 0;
    uint8_t turnFrom = 0;
    uint8_t turnTo = 0;
    uint8_t turnAck = 0;
};

/* The owner's own label for a console, falling back to the tag it advertises.
 *
 * Every notification and every seat goes through here so that naming a peer
 * changes what the whole device calls it rather than what one screen does. It
 * reads local NVS -- Board mirrors peer names in RAM, so this is a comparison
 * and not a flash lookup -- and it travels one way only: towards the screen.
 * Nothing here can put a label on the air. */
const char* displayName(Board& board, const char* deviceId) {
    const char* label = board.peerName(deviceId);
    return label != nullptr ? label : deviceId;
}

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
    snprintf(text, sizeof(text), "%s beat your %s: %lu",
             displayName(board, seen.deviceId), title,
             static_cast<unsigned long>(seen.bestScore));
    pushEvent(text);
}

/* A poke aimed at this device, heard from `seen`.
 *
 * Two things are deliberate. The target comparison is against our own
 * advertised device id, so a poke aimed at somebody else is heard and ignored
 * -- an advertisement is a broadcast and every Braino in range sees all of
 * them. And this is the one notification that makes a noise: the rest of what
 * this module raises is ambient news about the room, while a poke is a person
 * asking for your attention, which is the distinction worth a sound. Mute is
 * still honoured, because every cue goes through Board::playSound(). */
/* Copy the session traffic across verbatim. No interpretation: this module
 * does not know a chess move from a backgammon one, and should not. */
void recordSession(Known& entry, const BleScan::Sighting& seen) {
    entry.inviting = seen.inviting;
    if (seen.inviting) {
        strncpy(entry.inviteTarget, seen.inviteTarget, sizeof(entry.inviteTarget) - 1);
        entry.inviteTarget[sizeof(entry.inviteTarget) - 1] = 0;
        entry.inviteSession = seen.inviteSession;
    }
    if (seen.hasTurn) {
        entry.hasTurn = true;
        entry.turnSession = seen.turnSession;
        entry.turnPly = seen.turnPly;
        entry.turnFrom = seen.turnFrom;
        entry.turnTo = seen.turnTo;
        entry.turnAck = seen.turnAck;
    }
}

/* Somebody in the room is offering us a game.
 *
 * Announced the same way a poke is, and for the same reason: it is a person
 * asking for your attention, and an offer nobody sees is an offer that expires
 * unanswered. Before this, an invitation only appeared if the other player
 * happened to already be sitting in that game's lobby, which made the feature
 * almost impossible to start.
 *
 * The game is named from the peer's advertised game index rather than from
 * anything this module knows, which is what keeps it a service: the day a
 * second two-player game exists, its invitations announce themselves correctly
 * with nothing added here. */
void evaluateInvite(Board& board, Known& entry, const BleScan::Sighting& seen) {
    if (!seen.inviting) {
        return;
    }
    const char* mine = BleBeacon::configured().deviceId;
    if (mine[0] == '\0' ||
        strncmp(seen.inviteTarget, mine, sizeof(seen.inviteTarget)) != 0) {
        return;   // aimed at somebody else; a broadcast is heard by everyone
    }
    if (entry.sawInvite && entry.lastInviteByte == seen.inviteSession) {
        return;   // the same invitation, still on air
    }
    entry.sawInvite = true;
    entry.lastInviteByte = seen.inviteSession;

    const char* title = gameTitle(seen.gameIndex);
    char text[BANNER_MAX];
    if (title != nullptr) {
        snprintf(text, sizeof(text), "%s wants to play %s",
                 displayName(board, seen.deviceId), title);
    } else {
        snprintf(text, sizeof(text), "%s wants to play",
                 displayName(board, seen.deviceId));
    }
    pushEvent(text);
    board.playSound(Sound::Pop);
}

void evaluatePoke(Board& board, Known& entry, const BleScan::Sighting& seen) {
    if (!seen.poking) {
        return;
    }
    const char* mine = BleBeacon::configured().deviceId;
    if (mine[0] == '\0' || strncmp(seen.pokeTarget, mine, sizeof(seen.pokeTarget)) != 0) {
        return;
    }
    if (entry.sawPoke && entry.lastPokeNonce == seen.pokeNonce) {
        return;   // the same poke, still on air
    }
    entry.sawPoke = true;
    entry.lastPokeNonce = seen.pokeNonce;

    char text[BANNER_MAX];
    snprintf(text, sizeof(text), "%s poked you!", displayName(board, seen.deviceId));
    pushEvent(text);
    board.playSound(Sound::Pop);
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
                snprintf(text, sizeof(text), "%s nearby, playing %s",
                         displayName(board, seen.deviceId), title);
            } else {
                snprintf(text, sizeof(text), "%s is nearby",
                         displayName(board, seen.deviceId));
            }
            pushEvent(text);
            evaluateScore(board, *entry, seen);
            evaluatePoke(board, *entry, seen);
        } else {
            const bool gameChanged = entry->lastGame != seen.gameIndex;
            const bool scoreChanged = entry->lastScore != seen.bestScore;
            entry->lastGame = seen.gameIndex;
            entry->lastScore = seen.bestScore;

            if (gameChanged) {
                const char* title = gameTitle(seen.gameIndex);
                if (seen.sharesActivity && title != nullptr) {
                    snprintf(text, sizeof(text), "%s is playing %s",
                             displayName(board, seen.deviceId), title);
                    pushEvent(text);
                }
            }
            if (gameChanged || scoreChanged) {
                evaluateScore(board, *entry, seen);
            }
            /* Unconditional, unlike the score check above: a poke is not a
             * change to the peer's game or score, so gating it on those would
             * lose a poke from somebody sitting on the same screen -- which
             * is most of the time. The nonce does the de-duplication. */
            evaluatePoke(board, *entry, seen);
        }

        /* OUTSIDE the branch above, for peers we have just met and peers we
         * already knew alike.
         *
         * recordSession() used to sit inside the new-peer branch, which meant
         * a console's turn was copied across exactly once -- on the sighting
         * that first brought it into range -- and every move it made after
         * that was dropped on the floor. Nothing else in the system would have
         * complained: the scanner saw the moves, the payload decoded, and the
         * peer table simply never learned about them. Anything that has to
         * track a peer's live state belongs here, not in either branch. */
        recordSession(*entry, seen);
        evaluateInvite(board, *entry, seen);

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

bool poke(Board& board, const char* deviceId) {
    /* Same gate as everything else here, re-derived rather than assumed: a
     * poke is a transmission, so it is not available while the feature that
     * authorises transmitting is off. */
    if (!enabled_ || !BleBeacon::active()) {
        return false;
    }
    if (!BleBeacon::poke(deviceId)) {
        return false;
    }
    board.playSound(Sound::Tap);
    return true;
}

bool pokeInFlight() {
    return BleBeacon::poking();
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
    /* Takes our own poke off air once it has had its few seconds. Idempotent
     * and free when there is no poke live. */
    BleBeacon::clearExpiredPoke();

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
    return BleBeacon::invitePeer(deviceId, payload);
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

}   // namespace NearbyPlay
