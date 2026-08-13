#pragma once

#include <Arduino.h>

/* BLE beacon -- a non-connectable, advertise-only presence broadcast.
 *
 * The device announces nothing but its own identity: a family tag and a short
 * hardware-derived id. No child name, no location, no Wi-Fi details, no
 * scores. Nothing that is stored per profile ever reaches the radio.
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
 */
namespace BleBeacon {

/** Longest legal legacy advertising payload. */
constexpr uint8_t PAYLOAD_MAX = 31;

/* Assigned-number 0xFFFF is the SIG's reserved "no company / testing" id.
 * We are not a member company, and claiming someone else's id would be worse
 * than honestly using the reserved one. */
constexpr uint16_t COMPANY_ID_NONE = 0xFFFF;

/** Bumped whenever the manufacturer-data layout changes. */
constexpr uint8_t PAYLOAD_VERSION = 1;

/** Advertising interval, in milliseconds. Slow: this is a presence ping. */
constexpr uint16_t ADV_INTERVAL_MS = 1000;

/** Transmit power we explicitly request, in dBm. */
constexpr int8_t TX_POWER_DBM = 3;

/** Product family tag. Two-letter short form goes in the manufacturer data. */
constexpr const char* FAMILY_ID = "LearnKey";
constexpr const char* FAMILY_TAG = "LK";

/* One authoritative representation of the outgoing advertisement.
 *
 * Every field here is either transmitted or explicitly marked absent. Fields
 * that are not transmitted (service UUID, service data) are left empty and the
 * UI omits them rather than printing a value that is not on air. */
struct Advertisement {
    char deviceName[24] = {0};      // Complete Local Name, e.g. "LearnKey-A4F2"
    char familyId[12] = {0};        // "LearnKey"
    char deviceId[5] = {0};         // "A4F2" -- last two bytes of the base MAC

    uint16_t companyId = COMPANY_ID_NONE;
    uint8_t manufacturerData[8] = {0};
    uint8_t manufacturerLen = 0;

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

/* Build the advertisement from the hardware id and start the radio if the
 * stored setting says it should be on. Safe to call once from Board::begin().*/
void begin(bool enabled);

/** Turn advertising on or off at runtime. Idempotent. */
void setEnabled(bool enabled);

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

/** Controller address, or an empty string when the stack is down. */
String address();

/** Human-readable advertising mode, derived from the configuration. */
const char* modeText();

/** Hex dump of `len` bytes as "4C 4B 01". */
String toHex(const uint8_t* data, uint8_t len);

}   // namespace BleBeacon
