#include "BleBeacon.h"
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
uint8_t idBytes_[2] = {0, 0};   // the two MAC bytes behind adv_.deviceId
uint32_t pokeStartedMs_ = 0;    // when the live poke went on air
/* Set when advertising has stopped but the controller could not safely be torn
 * down yet. See stopRadio(). */
bool deinitPending_ = false;

/* GAP AD types (Core Supplement, Part A). */
constexpr uint8_t AD_FLAGS = 0x01;
constexpr uint8_t AD_UUID16_COMPLETE = 0x03;
constexpr uint8_t AD_NAME_COMPLETE = 0x09;
constexpr uint8_t AD_SERVICE_DATA_16 = 0x16;
constexpr uint8_t AD_MANUFACTURER = 0xFF;

/* LE General Discoverable + BR/EDR Not Supported. Set even though we are
 * non-connectable, so ordinary phone scanners will list the device at all. */
constexpr uint8_t FLAGS_VALUE = 0x06;

/** Append one AD structure. Returns false (and appends nothing) if it won't fit. */
bool appendAd(Advertisement& a, uint8_t type, const uint8_t* data, uint8_t len) {
    const uint16_t need = static_cast<uint16_t>(len) + 2;
    if (a.payloadLen + need > PAYLOAD_MAX) {
        return false;
    }
    a.payload[a.payloadLen++] = static_cast<uint8_t>(len + 1);   // type + data
    a.payload[a.payloadLen++] = type;
    for (uint8_t i = 0; i < len; ++i) {
        a.payload[a.payloadLen++] = data[i];
    }
    return true;
}

/* Derive the short device id from the factory MAC. It is a hardware serial,
 * not anything the player or parent typed, and it is stable across reboots so a
 * parent can recognise their own device in a scanner. */
void deriveIdentity(Advertisement& a) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    idBytes_[0] = mac[4];
    idBytes_[1] = mac[5];
    snprintf(a.deviceId, sizeof(a.deviceId), "%02X%02X", idBytes_[0], idBytes_[1]);
    snprintf(a.familyId, sizeof(a.familyId), "%s", FAMILY_ID);
    snprintf(a.deviceName, sizeof(a.deviceName), "%s-%s", FAMILY_ID, a.deviceId);
}

/* Compile the struct into AD structures. This buffer is what start() gives the
 * controller and what System Info reads back -- keep it that way. */
void buildPayload(Advertisement& a) {
    a.payloadLen = 0;

    const uint8_t flags = FLAGS_VALUE;
    appendAd(a, AD_FLAGS, &flags, 1);

    appendAd(a, AD_NAME_COMPLETE,
             reinterpret_cast<const uint8_t*>(a.deviceName),
             static_cast<uint8_t>(strlen(a.deviceName)));

    /* Manufacturer data: company id (little endian), family tag, layout
     * version, device id, flags -- and, only while Nearby play is on, the
     * open game and this device's best score for it. Every byte here is
     * decoded field by field by the System Info screen and read back by
     * decode() below, so there is one layout and not two.
     *
     * When sharing is off the block stops at the flag byte. The game and
     * score fields are absent from the air rather than present-and-zero:
     * "not transmitted" has to be structural to be worth claiming. */
    uint8_t mfg[sizeof(a.manufacturerData)];
    uint8_t n = 0;
    mfg[n++] = static_cast<uint8_t>(a.companyId & 0xFF);
    mfg[n++] = static_cast<uint8_t>(a.companyId >> 8);
    mfg[n++] = static_cast<uint8_t>(FAMILY_TAG[0]);
    mfg[n++] = static_cast<uint8_t>(FAMILY_TAG[1]);
    mfg[n++] = PAYLOAD_VERSION;
    mfg[n++] = idBytes_[0];
    mfg[n++] = idBytes_[1];
    mfg[n++] = static_cast<uint8_t>((a.sharesActivity ? FLAG_SHARES_ACTIVITY : 0x00) |
                                    (a.poking ? FLAG_POKE : 0x00) |
                                    (a.inviting ? FLAG_INVITE : 0x00) |
                                    (a.hasTurn ? FLAG_TURN : 0x00));
    if (a.sharesActivity) {
        mfg[n++] = a.gameIndex;
        /* The poke displaces the score rather than following it: there are no
         * spare bytes in a 31-byte payload, and dropping a number a peer can
         * ask for again in six seconds is cheaper than dropping the poke,
         * which is an event that does not come back. */
        if (a.poking || a.inviting) {
            /* An invitation is the poke's wire shape with a different flag:
             * a target and a nonce, aimed at one peer, repeated because scan
             * windows have gaps. Sharing the layout means sharing the
             * idempotence argument too, which is the part that is easy to get
             * wrong twice. */
            mfg[n++] = a.pokeTarget[0];
            mfg[n++] = a.pokeTarget[1];
            mfg[n++] = a.pokeNonce;
        } else if (a.hasTurn) {
            /* A move, packed into the four bytes the score was using. See
             * MFG_LEN_TURN for the bit layout -- session 6, ply 7, from 6,
             * to 6, ack 7, which is thirty-two bits exactly and the whole of
             * what is left in a payload that is already full. */
            const uint32_t w =
                (static_cast<uint32_t>(a.turnSession & 0x3F) << 26) |
                (static_cast<uint32_t>(a.turnPly & 0x7F) << 19) |
                (static_cast<uint32_t>(a.turnFrom & 0x3F) << 13) |
                (static_cast<uint32_t>(a.turnTo & 0x3F) << 7) |
                (static_cast<uint32_t>(a.turnAck & 0x7F));
            mfg[n++] = static_cast<uint8_t>(w & 0xFF);
            mfg[n++] = static_cast<uint8_t>((w >> 8) & 0xFF);
            mfg[n++] = static_cast<uint8_t>((w >> 16) & 0xFF);
            mfg[n++] = static_cast<uint8_t>((w >> 24) & 0xFF);
        } else {
            mfg[n++] = static_cast<uint8_t>(a.bestScore & 0xFF);
            mfg[n++] = static_cast<uint8_t>((a.bestScore >> 8) & 0xFF);
            mfg[n++] = static_cast<uint8_t>((a.bestScore >> 16) & 0xFF);
            mfg[n++] = static_cast<uint8_t>((a.bestScore >> 24) & 0xFF);
        }
    } else if (a.poking || a.inviting) {
        /* Poking without sharing still needs the game slot occupied, because
         * the target and nonce are positional. GAME_NONE is the honest filler:
         * it is the value that already means "no game open". */
        mfg[n++] = GAME_NONE;
        mfg[n++] = a.pokeTarget[0];
        mfg[n++] = a.pokeTarget[1];
        mfg[n++] = a.pokeNonce;
    }
    a.manufacturerLen = n;
    memcpy(a.manufacturerData, mfg, n);
    /* The sharing payload uses all 31 legal bytes, so a longer device name or
     * another AD structure would silently push this one off the air. Say so
     * rather than transmitting an advertisement nobody can decode. */
    if (!appendAd(a, AD_MANUFACTURER, a.manufacturerData, a.manufacturerLen)) {
        Serial.printf("[ble] manufacturer data (%u B) does not fit the payload\n",
                      a.manufacturerLen);
        a.manufacturerLen = 0;
    }

    /* No service UUID or service data is advertised. Left at zero length so
     * the UI omits the rows entirely rather than showing an empty value. */
    if (a.serviceUuid16 != 0) {
        const uint8_t uuid[2] = {static_cast<uint8_t>(a.serviceUuid16 & 0xFF),
                                 static_cast<uint8_t>(a.serviceUuid16 >> 8)};
        appendAd(a, AD_UUID16_COMPLETE, uuid, 2);
        if (a.serviceDataLen > 0) {
            uint8_t sd[10];
            sd[0] = uuid[0];
            sd[1] = uuid[1];
            memcpy(sd + 2, a.serviceData, a.serviceDataLen);
            appendAd(a, AD_SERVICE_DATA_16, sd, static_cast<uint8_t>(a.serviceDataLen + 2));
        }
    }
}

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

/* Parse "A4F2" into the two bytes the payload carries. Rejects anything that
 * is not exactly four hex digits rather than letting strtol's tolerance put a
 * half-parsed id on air. */
namespace {
bool parseDeviceId(const char* text, uint8_t out[2]) {
    if (text == nullptr) {
        return false;
    }
    uint8_t nibbles[4];
    for (uint8_t i = 0; i < 4; ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9')       nibbles[i] = static_cast<uint8_t>(c - '0');
        else if (c >= 'A' && c <= 'F')  nibbles[i] = static_cast<uint8_t>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f')  nibbles[i] = static_cast<uint8_t>(c - 'a' + 10);
        else return false;
    }
    if (text[4] != '\0') {
        return false;
    }
    out[0] = static_cast<uint8_t>((nibbles[0] << 4) | nibbles[1]);
    out[1] = static_cast<uint8_t>((nibbles[2] << 4) | nibbles[3]);
    return true;
}
}   // namespace

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

bool poke(const char* targetDeviceId) {
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
    adv_.pokeTarget[0] = target[0];
    adv_.pokeTarget[1] = target[1];
    /* Wraps at 256 and that is fine: the receiver only asks whether this nonce
     * differs from the last one it acted on for this peer, and 256 pokes from
     * one device inside one sighting's lifetime is not a thing that happens. */
    ++adv_.pokeNonce;
    pokeStartedMs_ = millis();
    buildPayload(adv_);
    restartRadio();
    Serial.printf("[ble] poking %s (nonce %u)\n", targetDeviceId,
                  static_cast<unsigned>(adv_.pokeNonce));
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

/* The exact inverse of buildPayload()'s manufacturer block. Anything that does
 * not match this layout, at this version, is somebody else's advertisement and
 * is discarded rather than guessed at. */
bool decode(const uint8_t* mfg, uint8_t len, Observation& out) {
    if (mfg == nullptr || len < MFG_LEN_BASE) {
        return false;
    }
    const uint16_t company = static_cast<uint16_t>(mfg[0] | (static_cast<uint16_t>(mfg[1]) << 8));
    if (company != COMPANY_ID_NONE) {
        return false;
    }
    if (mfg[2] != static_cast<uint8_t>(FAMILY_TAG[0]) ||
        mfg[3] != static_cast<uint8_t>(FAMILY_TAG[1])) {
        return false;
    }
    if (mfg[4] != PAYLOAD_VERSION) {
        return false;
    }
    snprintf(out.deviceId, sizeof(out.deviceId), "%02X%02X", mfg[5], mfg[6]);
    /* The flag bit alone is not enough: a truncated block would otherwise be
     * read as a game index and score that were never transmitted. */
    /* Each field is gated on the length that actually carries it. A flag bit
     * alone is not enough -- a truncated block would otherwise be read as a
     * game, a score or a poke target that was never transmitted -- and one
     * length test for all of them is not enough either, which is what version
     * 2 did and what a poke's shorter block breaks. */
    const bool shares = (mfg[7] & FLAG_SHARES_ACTIVITY) != 0;
    const bool poke = (mfg[7] & FLAG_POKE) != 0;
    const bool invite = (mfg[7] & FLAG_INVITE) != 0;
    const bool turn = (mfg[7] & FLAG_TURN) != 0;

    out.sharesActivity = shares && len >= MFG_LEN_GAME;
    out.gameIndex = GAME_NONE;
    out.bestScore = 0;
    out.haveScore = false;
    out.poking = false;
    out.pokeTarget[0] = '\0';
    out.pokeNonce = 0;
    out.inviting = false;
    out.inviteTarget[0] = 0;
    out.inviteSession = 0;
    out.hasTurn = false;
    out.turnSession = 0;
    out.turnPly = 0;
    out.turnFrom = 0;
    out.turnTo = 0;
    out.turnAck = 0;

    if (len >= MFG_LEN_GAME && (shares || poke || invite || turn)) {
        out.gameIndex = mfg[8];
    }

    /* An invitation reuses the poke block, so it is read at the poke
     * length -- but into its own fields, because the two mean different
     * things and a caller must not have to guess which arrived. */
    if (invite && len >= MFG_LEN_POKE) {
        out.inviting = true;
        snprintf(out.inviteTarget, sizeof(out.inviteTarget), "%02X%02X",
                 mfg[9], mfg[10]);
        out.inviteSession = mfg[11];
    }

    /* A move, gated on the FLAG rather than on the length. CHESS and
     * ACTIVITY are both thirteen bytes, so length alone cannot tell a
     * move from a best score -- which is exactly why PAYLOAD_VERSION had
     * to go to 4 rather than this being added quietly. A version-3
     * reader would have shown somebody a score of several million. */
    if (turn && len >= MFG_LEN_TURN) {
        const uint32_t w = static_cast<uint32_t>(mfg[9]) |
                           (static_cast<uint32_t>(mfg[10]) << 8) |
                           (static_cast<uint32_t>(mfg[11]) << 16) |
                           (static_cast<uint32_t>(mfg[12]) << 24);
        out.hasTurn = true;
        out.turnSession = static_cast<uint8_t>((w >> 26) & 0x3F);
        out.turnPly = static_cast<uint8_t>((w >> 19) & 0x7F);
        out.turnFrom = static_cast<uint8_t>((w >> 13) & 0x3F);
        out.turnTo = static_cast<uint8_t>((w >> 7) & 0x3F);
        out.turnAck = static_cast<uint8_t>(w & 0x7F);
    }
    if (poke && len >= MFG_LEN_POKE) {
        out.poking = true;
        snprintf(out.pokeTarget, sizeof(out.pokeTarget), "%02X%02X", mfg[9], mfg[10]);
        out.pokeNonce = mfg[11];
    } else if (out.sharesActivity && !turn && len >= MFG_LEN_ACTIVITY) {
        out.haveScore = true;
        out.bestScore = static_cast<uint32_t>(mfg[9]) |
                        (static_cast<uint32_t>(mfg[10]) << 8) |
                        (static_cast<uint32_t>(mfg[11]) << 16) |
                        (static_cast<uint32_t>(mfg[12]) << 24);
    }
    return true;
}

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
