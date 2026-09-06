#pragma once

#include <Arduino.h>

/* BLE beacon -- a non-connectable, advertise-only presence broadcast.
 *
 * By default the device announces nothing but its own identity: a family tag
 * and a short hardware-derived id. No player name, no location, no Wi-Fi
 * details. Nothing that identifies a person ever reaches the radio.
 *
 * When the owner opts in to Nearby play (Settings > Nearby, which itself
 * requires the beacon to be on), two anonymous fields are added: which game is
 * open and the best score recorded on this device for it. Those are game
 * facts, not player facts -- see docs/BLE_BEACON_SPEC.md.
 *
 * ------------------------------------------------------------------------
 * The single source of truth
 * ------------------------------------------------------------------------
 * The whole point of this module is that there is exactly ONE description of
 * what goes on air: the Advertisement struct below, and the raw AD-structure
 * byte buffer it is compiled into. buildPayload() fills that buffer, start()
 * hands those exact bytes to the controller, and the System Info screen reads
 * back the same buffer to tell the owner what is being transmitted.
 *
 * There is deliberately no second, hand-written UI description of the payload.
 * If you change what is advertised, change Advertisement / buildPayload() and
 * the System Info screen follows automatically -- which is the only way the
 * display and the radio cannot silently drift apart.
 *
 * decode() is the reverse of buildPayload() and lives here for the same
 * reason: the scanner in hal/BleNearby must read peers using this file's
 * layout, not a second copy of it that can rot.
 */
namespace BleBeacon {

/** Longest legal legacy advertising payload. */
constexpr uint8_t PAYLOAD_MAX = 31;

/* Assigned-number 0xFFFF is the SIG's reserved "no company / testing" id.
 * We are not a member company, and claiming someone else's id would be worse
 * than honestly using the reserved one. */
constexpr uint16_t COMPANY_ID_NONE = 0xFFFF;

/* Bumped whenever the manufacturer-data layout changes. Peers ignore an
 * advertisement whose version they do not know, which is what stops a future
 * layout from being decoded as this one.
 *
 * 3 added the poke fields and, with them, per-field length gating: version 2
 * decided "is the score here?" and "is the game here?" with one length test,
 * which a poke's shorter block would have answered wrongly for both. */
constexpr uint8_t PAYLOAD_VERSION = 3;

/** Advertising interval, in milliseconds. Slow: this is a presence ping. */
constexpr uint16_t ADV_INTERVAL_MS = 1000;

/** Transmit power we explicitly request, in dBm. */
constexpr int8_t TX_POWER_DBM = 3;

/** Product family tag. Two-letter short form goes in the manufacturer data. */
constexpr const char* FAMILY_ID = "Braino";
constexpr const char* FAMILY_TAG = "BR";

/** Name prefix a peer must carry to be one of ours: "Braino-". */
constexpr const char* NAME_PREFIX = "Braino-";

/** gameIndex value meaning "no game open". */
constexpr uint8_t GAME_NONE = 0xFF;

/* Manufacturer-data flag bits. The rest are reserved and must be transmitted
 * as zero. */
constexpr uint8_t FLAG_SHARES_ACTIVITY = 0x01;
constexpr uint8_t FLAG_POKE = 0x02;

/* Manufacturer-data lengths, in bytes, excluding the AD header.
 *   BASE     company(2) tag(2) version(1) id(2) flags(1)
 *   GAME     BASE + gameIndex(1)
 *   POKE     GAME + pokeTarget(2) + pokeNonce(1)
 *   ACTIVITY GAME + bestScore(4)
 *
 * THE SHARING PAYLOAD USES ALL 31 LEGAL BYTES. Flags(3) + the complete local
 * name "Braino-A4F2"(13) + manufacturer data(2+13) is exactly the maximum, so
 * a poke cannot be appended -- there is nowhere to put it. It therefore
 * DISPLACES the four score bytes for as long as it is on air: POKE is one byte
 * shorter than ACTIVITY, the game stays visible, and the score is structurally
 * absent rather than stale. A reader must gate each field on its own length,
 * which is why there are four constants here and not two. */
constexpr uint8_t MFG_LEN_BASE = 8;
constexpr uint8_t MFG_LEN_GAME = 9;
constexpr uint8_t MFG_LEN_POKE = 12;
constexpr uint8_t MFG_LEN_ACTIVITY = 13;

/* How long one poke stays on air. It has to exceed a peer's worst-case gap
 * between scan windows, or a poke can be transmitted perfectly and never
 * heard; it also has to end, because a poke is an event and an advertisement
 * is a state. The nonce is what makes the repetition safe -- a receiver acts
 * on a (device id, nonce) pair exactly once, however many times it hears it. */
constexpr uint32_t POKE_ADVERTISE_MS = 6000;

/* One authoritative representation of the outgoing advertisement.
 *
 * Every field here is either transmitted or explicitly marked absent. Fields
 * that are not transmitted (service UUID, service data) are left empty and the
 * UI omits them rather than printing a value that is not on air. */
struct Advertisement {
    char deviceName[24] = {0};      // Complete Local Name, e.g. "Braino-A4F2"
    char familyId[12] = {0};        // "Braino"
    char deviceId[5] = {0};         // "A4F2" -- last two bytes of the base MAC

    uint16_t companyId = COMPANY_ID_NONE;
    uint8_t manufacturerData[16] = {0};
    uint8_t manufacturerLen = 0;

    /* Nearby play. All three are absent from the payload while sharing is
     * off: manufacturerLen drops back to MFG_LEN_BASE and the flag bit is
     * clear, so there is no field on air holding a stale game or score. */
    bool sharesActivity = false;
    uint8_t gameIndex = GAME_NONE;
    uint32_t bestScore = 0;

    /* An outgoing poke, while one is live. pokeTarget is the peer's own
     * two-byte device id -- the identifier that device already broadcasts
     * about itself -- and never anything derived from a profile. Cleared by
     * clearExpiredPoke() once POKE_ADVERTISE_MS has passed, which is what
     * keeps this an event rather than a state somebody is left stuck in. */
    bool poking = false;
    uint8_t pokeTarget[2] = {0, 0};
    uint8_t pokeNonce = 0;

    uint16_t serviceUuid16 = 0;     // 0 == none advertised
    uint8_t serviceData[8] = {0};
    uint8_t serviceDataLen = 0;

    uint16_t advIntervalMs = ADV_INTERVAL_MS;
    int8_t txPowerDbm = TX_POWER_DBM;
    bool txPowerConfigured = true;
    bool connectable = false;       // ADV_NONCONN_IND

    /* The assembled AD structures, exactly as handed to the controller. */
    uint8_t payload[PAYLOAD_MAX] = {0};
    uint8_t payloadLen = 0;
};

/* What one peer's manufacturer data says, once decoded. This is the whole of
 * what a scanner can learn from us, and therefore the whole of what we can
 * learn about anybody else. */
struct Observation {
    char deviceId[5] = {0};
    bool sharesActivity = false;
    bool haveScore = false;     // false while this peer is poking: see MFG_LEN_POKE
    uint8_t gameIndex = GAME_NONE;
    uint32_t bestScore = 0;

    /* The peer is poking somebody. Everyone in range hears this, and the
     * target is in the clear -- an advertisement is a broadcast and pretending
     * otherwise in the API would invite somebody to treat it as private. Only
     * the device whose id matches reacts. */
    bool poking = false;
    char pokeTarget[5] = {0};
    uint8_t pokeNonce = 0;
};

/* Build the advertisement from the hardware id and start the radio if the
 * stored setting says it should be on. Safe to call once from Board::begin().*/
void begin(bool enabled);

/** Turn advertising on or off at runtime. Idempotent. */
void setEnabled(bool enabled);

/* Set (or clear) the Nearby-play fields and re-advertise if they changed.
 * `share == false` removes both fields from the payload entirely. Cheap and
 * idempotent when nothing changed, so it is safe to call on every screen
 * change. */
void setActivity(bool share, uint8_t gameIndex, uint32_t bestScore);

/* Put a poke on air for POKE_ADVERTISE_MS, aimed at one peer's device id (the
 * four hex digits it advertises, e.g. "A4F2"). Bumps the nonce so a repeat of
 * the same target is a new event, and returns false if the id is malformed or
 * the beacon is not advertising. */
bool poke(const char* targetDeviceId);

/* Drop an expired poke and re-advertise without it. Called once per frame from
 * the same tick that drives the scanner; cheap and idempotent when there is no
 * poke live. */
void clearExpiredPoke();

/** True while a poke of ours is on air. */
bool poking();

/** The setting. True does not by itself prove the radio came up -- see active(). */
bool enabled();

/** True only while the controller is actually advertising. */
bool active();

/* The configured advertisement. Always populated once begin() has run, even
 * while advertising is stopped -- callers must label it as configured, not as
 * currently broadcast, unless active() is true. */
const Advertisement& configured();

/* The advertisement currently on air, or nullptr when nothing is being
 * transmitted. This is the accessor a "what is my device broadcasting right
 * now" display should use. */
const Advertisement* broadcasting();

/* Read a peer's manufacturer data using the same layout buildPayload() wrote.
 * Returns false unless the block is one of ours, at a version we understand
 * and of a length this layout defines. */
bool decode(const uint8_t* mfg, uint8_t len, Observation& out);

/** Controller address, or an empty string when the stack is down. */
String address();

/** Human-readable advertising mode, derived from the configuration. */
const char* modeText();

/** Hex dump of `len` bytes as "42 52 02". */
String toHex(const uint8_t* data, uint8_t len);

}   // namespace BleBeacon
