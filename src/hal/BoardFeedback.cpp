#include "Board.h"

#include "BleBeacon.h"

/* Feedback that is not sound: the RGB case LED, and the two radio switches
 * that sit beside it as device-wide settings.
 *
 * Audio used to live here too. It now has its own unit in BoardAudio.cpp --
 * once a beep stopped being two sine waves and became a synthesiser with a
 * phoneme table, the two concerns had nothing to say to each other beyond
 * beepOk() pulsing the LED, and this file was heading past the ~600 line mark
 * CLAUDE.md's modularity rule draws. */

namespace {
constexpr uint8_t RGB_CH_R = 5;
constexpr uint8_t RGB_CH_G = 6;
constexpr uint8_t RGB_CH_B = 7;
constexpr uint32_t RGB_PWM_HZ = 5000;
constexpr uint8_t RGB_PWM_BITS = 8;

void attachRgbChannel(int8_t pin, uint8_t channel) {
    if (pin == PIN_NONE) return;
    ledcSetup(channel, RGB_PWM_HZ, RGB_PWM_BITS);
    ledcAttachPin(pin, channel);
}

/* Duty is inverted on a common-anode LED: full brightness is a line held low. */
void writeRgbChannel(int8_t pin, uint8_t channel, uint8_t level) {
    if (pin == PIN_NONE) return;
    ledcWrite(channel, BOARD.rgb.commonAnode ? 255 - level : level);
}
}

/* Common-anode boards sink current, so a channel lights when its line is
 * driven LOW. A board that wires the LED the other way says so in its profile
 * rather than needing this inverted here. */
void Board::setRgb(bool red, bool green, bool blue) {
    const uint8_t on = BOARD.rgb.commonAnode ? LOW : HIGH;
    const uint8_t off = BOARD.rgb.commonAnode ? HIGH : LOW;
    if (BOARD.rgb.r != PIN_NONE) digitalWrite(BOARD.rgb.r, red ? on : off);
    if (BOARD.rgb.g != PIN_NONE) digitalWrite(BOARD.rgb.g, green ? on : off);
    if (BOARD.rgb.b != PIN_NONE) digitalWrite(BOARD.rgb.b, blue ? on : off);
}

void Board::setRgbEnabled(bool on) {
    prefs_.putBool("rgbOn", on);
    if (!on) setRgbColor(0, 0, 0);
}

bool Board::rgbEnabled() {
    return prefs_.getBool("rgbOn", true);
}

bool Board::bleBeaconEnabled() {
    return prefs_.getBool("bleOn", false);
}

void Board::setBleBeaconEnabled(bool on) {
    prefs_.putBool("bleOn", on);
    BleBeacon::setEnabled(on);
    /* Nearby play rides on this radio, so turning the beacon off must take it
     * with it. engine/NearbyPlay watches BleBeacon::enabled() every frame and
     * stands itself down; the stored preference is left alone so that turning
     * the beacon back on restores whatever the owner had chosen. */
}

/* Mirrored in RAM: NearbyPlay::tick() consults this once per frame to decide
 * whether the radio should be listening, and Preferences is flash-backed. */
bool Board::nearbyEnabled() {
    if (!nearbyCached_) {
        cachedNearby_ = prefs_.getBool("nearbyOn", false);
        nearbyCached_ = true;
    }
    return cachedNearby_;
}

void Board::setNearbyEnabled(bool on) {
    cachedNearby_ = on;
    nearbyCached_ = true;
    prefs_.putBool("nearbyOn", on);
}

/* ---------------------------------------------------------------- peer labels
 *
 * One blob, one RAM mirror, and nothing here is ever read by BleBeacon. See
 * the contract on Board::peerName() in Board.h.
 */
void Board::loadPeerNames() {
    if (peerNamesCached_) {
        return;
    }
    peerNamesCached_ = true;
    for (uint8_t i = 0; i < PEER_NAME_SLOTS; ++i) {
        peerLabels_[i] = PeerLabel{};
    }
    /* A short or absent blob leaves the table empty rather than half-filled:
     * getBytes writes nothing when the stored size does not match, and a
     * partially-populated table would show one device somebody else's name. */
    const size_t want = sizeof(peerLabels_);
    if (prefs_.getBytesLength("peerNames") != want) {
        return;
    }
    prefs_.getBytes("peerNames", peerLabels_, want);
    /* Trust nothing out of flash: a truncated write or an older layout could
     * leave a field unterminated, and every reader here is a C string. */
    for (uint8_t i = 0; i < PEER_NAME_SLOTS; ++i) {
        peerLabels_[i].id[sizeof(peerLabels_[i].id) - 1] = '\0';
        peerLabels_[i].name[sizeof(peerLabels_[i].name) - 1] = '\0';
    }
}

const char* Board::peerName(const char* deviceId) {
    if (deviceId == nullptr || deviceId[0] == '\0') {
        return nullptr;
    }
    loadPeerNames();
    for (uint8_t i = 0; i < PEER_NAME_SLOTS; ++i) {
        if (peerLabels_[i].name[0] != '\0' &&
            strncmp(peerLabels_[i].id, deviceId, sizeof(peerLabels_[i].id)) == 0) {
            return peerLabels_[i].name;
        }
    }
    return nullptr;
}

bool Board::setPeerName(const char* deviceId, const char* name) {
    if (deviceId == nullptr || strlen(deviceId) != 4) {
        return false;
    }
    loadPeerNames();

    const bool clearing = (name == nullptr || name[0] == '\0');
    int8_t slot = -1;
    int8_t free = -1;
    for (uint8_t i = 0; i < PEER_NAME_SLOTS; ++i) {
        if (peerLabels_[i].name[0] == '\0') {
            if (free < 0) free = static_cast<int8_t>(i);
            continue;
        }
        if (strncmp(peerLabels_[i].id, deviceId, sizeof(peerLabels_[i].id)) == 0) {
            slot = static_cast<int8_t>(i);
            break;
        }
    }

    if (clearing) {
        if (slot < 0) {
            return true;   // already nameless; nothing to write
        }
        peerLabels_[slot] = PeerLabel{};
    } else {
        if (slot < 0) {
            slot = free;
        }
        if (slot < 0) {
            /* Every slot belongs to a different device. Refusing is better
             * than evicting somebody's label to make room, which would look
             * like the name had been forgotten at random. */
            return false;
        }
        snprintf(peerLabels_[slot].id, sizeof(peerLabels_[slot].id), "%s", deviceId);
        snprintf(peerLabels_[slot].name, sizeof(peerLabels_[slot].name), "%s", name);
    }
    prefs_.putBytes("peerNames", peerLabels_, sizeof(peerLabels_));
    return true;
}

void Board::setRgbColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!BOARD.hasRgbLed()) return;
    if (!rgbReady_) {
        attachRgbChannel(BOARD.rgb.r, RGB_CH_R);
        attachRgbChannel(BOARD.rgb.g, RGB_CH_G);
        attachRgbChannel(BOARD.rgb.b, RGB_CH_B);
        rgbReady_ = true;
    }
    rgbR_ = r;
    rgbG_ = g;
    rgbB_ = b;
    writeRgbChannel(BOARD.rgb.r, RGB_CH_R, r);
    writeRgbChannel(BOARD.rgb.g, RGB_CH_G, g);
    writeRgbChannel(BOARD.rgb.b, RGB_CH_B, b);
}

void Board::pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
    if (!rgbEnabled()) return;
    setRgbColor(r, g, b);
    rgbHoldUntilMs_ = millis() + ms;
}

void Board::tickRgb() {
    if (rgbHoldUntilMs_ == 0) return;
    if (millis() >= rgbHoldUntilMs_) {
        rgbHoldUntilMs_ = 0;
        setRgbColor(0, 0, 0);
    }
}
