#include "BleBeacon.h"
#include "BleBeaconInternal.h"
#include "Watchdog.h"

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <string>

namespace BleBeacon {
namespace {

Advertisement adv_;
bool built_ = false;
bool enabled_ = false;
bool active_ = false;
uint32_t pokeStartedMs_ = 0;    // when the live poke went on air
/* Set when advertising has stopped but the controller could not safely be torn
 * down yet. See stopRadio(). */
bool deinitPending_ = false;

/* NimBLE takes the interval in 0.625ms units. */
uint16_t intervalUnits(uint16_t ms) {
    const uint32_t units = (static_cast<uint32_t>(ms) * 1000UL) / 625UL;
    return static_cast<uint16_t>(constrain(units, 32UL, 16384UL));
}

void startRadio() {
    if (active_) {
        return;
    }
    /* Whatever teardown was waiting on Wi-Fi is moot: the stack is wanted
     * again, and completing it now would only be followed by a re-init. */
    deinitPending_ = false;
    /* Bringing the controller up blocks for a couple of hundred milliseconds,
     * which from the loop task looks exactly like a hang. */
    Watchdog::Pause guard;

    if (!NimBLEDevice::getInitialized()) {
        NimBLEDevice::init(adv_.deviceName);
    }
    NimBLEDevice::setPower(static_cast<esp_power_level_t>(ESP_PWR_LVL_P3));

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData data;
    /* Raw bytes, not the helper setters: the controller must receive exactly
     * the buffer System Info will display, byte for byte. */
    data.addData(std::string(reinterpret_cast<const char*>(adv_.payload), adv_.payloadLen));
    advertising->setAdvertisementData(data);
    advertising->setScanResponse(false);
    advertising->setAdvertisementType(adv_.connectable ? BLE_GAP_CONN_MODE_UND
                                                       : BLE_GAP_CONN_MODE_NON);
    const uint16_t units = intervalUnits(adv_.advIntervalMs);
    advertising->setMinInterval(units);
    advertising->setMaxInterval(units);

    active_ = advertising->start();
    Serial.printf("[ble] advertising %s as %s (%u byte payload)\n",
                  active_ ? "started" : "FAILED", adv_.deviceName, adv_.payloadLen);
}

/* True whenever the Wi-Fi driver is started, connected or not.
 *
 * Connection state is the wrong question: coexistence arbitration is live from
 * the moment the driver starts, so a device that has begun associating and not
 * yet succeeded is exactly as dangerous to tear a controller down underneath as
 * one holding an address. */
bool wifiRadioUp() {
    return WiFi.getMode() != WIFI_MODE_NULL;
}

/* Take the beacon off the air, and tear the controller down if it is safe.
 *
 * Those are two separate things, and conflating them crashed the device.
 * Measured on hardware: with Wi-Fi running for NTP, advertising->stop()
 * followed by NimBLEDevice::deinit(true) panics with
 *
 *     Guru Meditation Error: Core 0 panic'ed (InstrFetchProhibited)
 *     PC : 0x00000000
 *
 * A PC of zero is a call through a null function pointer -- the controller is
 * being deinitialised while Wi-Fi coexistence callbacks still reference it. It
 * is a race rather than a certainty: consecutive runs alternated between the
 * panic and a survivable "timeout when WiFi un-init, type=4". Steady-state
 * coexistence is fine -- five rounds of scanNetworks() against a live
 * advertisement left free heap flat at ~155KB -- so this is deinit ordering,
 * not memory, and Watchdog::Pause does not help because a panic is not a stall.
 *
 * The reachable user gesture is turning the beacon off in Settings while the
 * clock is syncing, which is not an exotic one.
 *
 * So: stopping advertising is unconditional and immediate, because that is the
 * half the privacy promise rests on -- when the setting says the radio is
 * quiet, nothing is on the air. The deinit is deferred while Wi-Fi is up, and
 * tickRadio() completes it once Wi-Fi goes down. The cost of deferring is the
 * ~30KB of heap the stack holds, which is a price paid only by a device that
 * has Wi-Fi configured, and only until the radio next goes idle. A crash is
 * not a price worth paying for it. */
void stopRadio() {
    if (!NimBLEDevice::getInitialized()) {
        active_ = false;
        deinitPending_ = false;
        return;
    }
    Watchdog::Pause guard;
    NimBLEDevice::getAdvertising()->stop();
    active_ = false;

    if (wifiRadioUp()) {
        deinitPending_ = true;
        Serial.println("[ble] advertising stopped; controller held up "
                       "(Wi-Fi active, deinit deferred)");
        return;
    }

    /* Fully tear the stack down: leaving it initialised holds ~30KB of heap
     * that the games would rather have, and "off" should mean off. */
    NimBLEDevice::deinit(true);
    deinitPending_ = false;
    Serial.println("[ble] advertising stopped");
}

/* Push a rebuilt payload to a controller that is already advertising. NimBLE
 * will not swap the data underneath a running advertisement, so this is a
 * stop/start. It is cheap but not free, which is why setActivity() only calls
 * it when the bytes actually changed. */
void restartRadio() {
    if (!active_) {
        return;
    }
    Watchdog::Pause guard;
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->stop();
    NimBLEAdvertisementData data;
    data.addData(std::string(reinterpret_cast<const char*>(adv_.payload), adv_.payloadLen));
    advertising->setAdvertisementData(data);
    active_ = advertising->start();
}

}   // namespace

void begin(bool startEnabled) {
    if (!built_) {
        deriveIdentity(adv_);
        buildPayload(adv_);
        built_ = true;
    }
    enabled_ = startEnabled;
    if (enabled_) {
        startRadio();
    }
}

void setEnabled(bool on) {
    if (!built_) {
        begin(on);
        return;
    }
    if (on == enabled_) {
        return;
    }
    enabled_ = on;
    if (on) {
        startRadio();
    } else {
        stopRadio();
    }
}

/* Finish a teardown that stopRadio() had to defer. Called once per frame from
 * the runtime; the common case is a bool test and a return.
 *
 * This is the whole reason the beacon has a per-frame hook at all. Retrying
 * from the next stopRadio() would never fire -- the radio is already off -- and
 * leaving it to the next reboot would hold 30KB indefinitely on the one device
 * shape that has it: Wi-Fi configured and the beacon switched off. */
void tickRadio() {
    if (!deinitPending_ || wifiRadioUp()) {
        return;
    }
    if (!NimBLEDevice::getInitialized()) {
        deinitPending_ = false;
        return;
    }
    Watchdog::Pause guard;
    NimBLEDevice::deinit(true);
    deinitPending_ = false;
    Serial.println("[ble] deferred controller teardown completed");
}

void setActivity(bool share, uint8_t gameIndex, uint32_t bestScore) {
    if (!built_) {
        deriveIdentity(adv_);
        built_ = true;
    }
    /* Normalise before comparing, so "stop sharing" collapses to one state
     * rather than leaving whatever game was last open sitting in the struct
     * where a reader could mistake it for something still on air. */
    const uint8_t game = share ? gameIndex : GAME_NONE;
    const uint32_t score = share ? bestScore : 0;
    if (adv_.sharesActivity == share && adv_.gameIndex == game && adv_.bestScore == score) {
        return;
    }
    adv_.sharesActivity = share;
    adv_.gameIndex = game;
    adv_.bestScore = score;
    buildPayload(adv_);
    restartRadio();
}

/* Invite one peer to a two-player game.
 *
 * Deliberately the poke's machinery with a different flag, because it is the
 * same problem: an event aimed at one peer, on a medium with no delivery
 * guarantee, that must be acted on once however many times it is heard. The
 * session id doubles as the nonce -- one number, and the thing that ties the
 * invitation to every move that follows it.
 *
 * The same two honest limits apply as to a poke, and for the same reasons.
 * It is a BROADCAST: everyone in range hears who was invited, and only the
 * named device acts. And it displaces the score while it is on air, because a
 * 31-byte payload has no spare bytes. */
bool invitePeer(const char* targetDeviceId, uint8_t session,
                uint8_t gameIndex) {
    uint8_t target[2];
    if (!parseDeviceId(targetDeviceId, target)) {
        return false;
    }
    if (!enabled_ || !active_ || !adv_.sharesActivity) {
        return false;
    }
    /* An invitation and a poke share one four-byte hole, so they cannot both
     * be on air. The invitation wins: it is the one the other person is
     * waiting for. */
    adv_.hasTurn = false;
    adv_.poking = false;
    adv_.findMe = false;
    adv_.inviting = true;
    adv_.pokeTarget[0] = target[0];
    adv_.pokeTarget[1] = target[1];
    /* Seven bits pass through: the caller's session id plus whatever else it
     * needs both consoles to agree before the first move -- NearbyPlay puts
     * the who-moves-first bit here. This layer does not interpret any of it. */
    adv_.pokeNonce = static_cast<uint8_t>(session & 0x7F);
    /* The invitation SAYS which game it is for rather than inheriting whatever
     * the activity block happened to be advertising. Those are two different
     * facts and they were conflated once already: a game with no score
     * advertised GAME_NONE, so an invitation to play chess arrived as an
     * invitation to play nothing, and the banner on the other console could
     * only say "wants to play". Stating it here means an invitation is
     * self-describing however the sharing state is set. */
    adv_.gameIndex = gameIndex;
    pokeStartedMs_ = millis();
    buildPayload(adv_);
    restartRadio();
    Serial.printf("[ble] game invite to %s, session %u\n",
                  targetDeviceId, static_cast<unsigned>(session & 0x3F));
    return true;
}

/* Advertise our latest move, and keep advertising it.
 *
 * A poke is an event and stops after a few seconds. A move is a STATE and must
 * not: the opponent may be anywhere in its scan cycle, may have missed the
 * last three windows, or may have just come back into range. Leaving the move
 * on air until it is replaced is what makes the protocol reliable without an
 * acknowledgement channel -- the ack rides the opponent's own advertisement.
 *
 * Idempotent on purpose. Re-setting the same move does not touch the radio;
 * restarting an advertisement costs a stop and a start, and this is called
 * every frame by a screen that has no idea whether anything changed. */
void setTurn(uint8_t session, uint8_t ply, uint8_t from, uint8_t to,
                  uint8_t ack) {
    const uint8_t s6 = static_cast<uint8_t>(session & 0x3F);
    const uint8_t p7 = static_cast<uint8_t>(ply & 0x7F);
    const uint8_t f6 = static_cast<uint8_t>(from & 0x3F);
    const uint8_t t6 = static_cast<uint8_t>(to & 0x3F);
    const uint8_t a7 = static_cast<uint8_t>(ack & 0x7F);

    if (adv_.hasTurn && adv_.turnSession == s6 && adv_.turnPly == p7 &&
        adv_.turnFrom == f6 && adv_.turnTo == t6 && adv_.turnAck == a7) {
        return;
    }
    if (!built_) {
        deriveIdentity(adv_);
        built_ = true;
    }
    adv_.hasTurn = true;
    adv_.poking = false;
    adv_.findMe = false;
    adv_.inviting = false;
    adv_.turnSession = s6;
    adv_.turnPly = p7;
    adv_.turnFrom = f6;
    adv_.turnTo = t6;
    adv_.turnAck = a7;
    buildPayload(adv_);
    restartRadio();
}

void clearTurn() {
    if (!adv_.hasTurn) {
        return;
    }
    adv_.hasTurn = false;
    buildPayload(adv_);
    restartRadio();
}

bool poke(const char* targetDeviceId, bool findMe) {
    if (!active_) {
        /* Nothing is on air, so nothing would carry it. Refusing beats
         * arming a poke that expires unheard six seconds later. */
        return false;
    }
    uint8_t target[2];
    if (!parseDeviceId(targetDeviceId, target)) {
        return false;
    }
    adv_.poking = true;
    adv_.findMe = findMe;
    adv_.pokeTarget[0] = target[0];
    adv_.pokeTarget[1] = target[1];
    /* Wraps at 256 and that is fine: the receiver only asks whether this nonce
     * differs from the last one it acted on for this peer, and 256 pokes from
     * one device inside one sighting's lifetime is not a thing that happens. */
    ++adv_.pokeNonce;
    pokeStartedMs_ = millis();
    buildPayload(adv_);
    restartRadio();
    Serial.printf("[ble] %s %s (nonce %u)\n", findMe ? "finding" : "poking",
                  targetDeviceId, static_cast<unsigned>(adv_.pokeNonce));
    return true;
}

/* Expires both a poke and a game invitation: they share the wire block, the
 * timer and the argument for having one -- each is an event, and an
 * advertisement left up forever would turn it into a state somebody is stuck
 * in. A move is the opposite and is deliberately NOT expired here; see
 * setTurn(). */
void clearExpiredPoke() {
    if (!adv_.poking && !adv_.inviting) {
        return;
    }
    if (millis() - pokeStartedMs_ < POKE_ADVERTISE_MS) {
        return;
    }
    adv_.poking = false;
    adv_.findMe = false;
    adv_.inviting = false;
    adv_.pokeTarget[0] = 0;
    adv_.pokeTarget[1] = 0;
    /* The nonce is deliberately NOT reset: it has to keep increasing across
     * pokes or the next one to the same peer would look like a repeat of this
     * one and be ignored. */
    buildPayload(adv_);
    restartRadio();
}

bool poking() { return adv_.poking; }

bool enabled() { return enabled_; }
bool active() { return active_; }
const Advertisement& configured() { return adv_; }
const Advertisement* broadcasting() { return active_ ? &adv_ : nullptr; }

String address() {
    if (!NimBLEDevice::getInitialized()) {
        return String();
    }
    return String(NimBLEDevice::getAddress().toString().c_str());
}

const char* modeText() {
    return adv_.connectable ? "BLE Connectable" : "BLE Advertise Only";
}

String toHex(const uint8_t* data, uint8_t len) {
    String out;
    out.reserve(static_cast<unsigned>(len) * 3);
    char buf[4];
    for (uint8_t i = 0; i < len; ++i) {
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        if (i > 0) out += ' ';
        out += buf;
    }
    return out;
}

}   // namespace BleBeacon
