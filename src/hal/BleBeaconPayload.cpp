#include "BleBeaconInternal.h"

#include <esp_mac.h>

/* What goes on air, and how to read it back -- nothing else.
 *
 * This file is one half of BleBeacon; the other half (BleBeacon.cpp) drives the
 * controller and never touches a byte layout. The split is deliberate and worth
 * keeping: the module's central promise is that there is exactly ONE
 * description of the advertisement, and that decode() is its exact inverse. Now
 * they are the only two things in one file, where a change to either without
 * the other is visible in a single diff.
 *
 * Do not add a second parser anywhere. The scanner, the System Info screen and
 * the Nearby screen all read peers through decode().
 */
namespace BleBeacon {
namespace {

/* The two MAC bytes behind adv_.deviceId. Written by deriveIdentity(), read by
 * buildPayload() -- the only two things that need them. */
uint8_t idBytes_[2] = {0, 0};

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
}   // namespace

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
                                    (a.poking && a.findMe ? FLAG_FIND : 0x00) |
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

/* Parse "A4F2" into the two bytes the payload carries. Rejects anything that
 * is not exactly four hex digits rather than letting strtol's tolerance put a
 * half-parsed id on air. */
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
    const bool find = (mfg[7] & FLAG_FIND) != 0;
    const bool invite = (mfg[7] & FLAG_INVITE) != 0;
    const bool turn = (mfg[7] & FLAG_TURN) != 0;

    out.sharesActivity = shares && len >= MFG_LEN_GAME;
    out.gameIndex = GAME_NONE;
    out.bestScore = 0;
    out.haveScore = false;
    out.poking = false;
    out.findMe = false;
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
        /* Gated on the poke's own length like every other field, not on the
         * flag alone: FLAG_FIND carries no bytes of its own, but the target
         * and nonce it qualifies do. */
        out.findMe = find;
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

}   // namespace BleBeacon
