#include "Board.h"

#include "BleBeacon.h"

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

void Board::beep(uint16_t frequency, uint16_t ms) {
    (void)frequency;
    (void)ms;
}

void Board::beepOk() {
    pulseRgb(0, 255, 40, 450);
    beep(1175, 55);
}

void Board::beepError() {
    pulseRgb(255, 0, 0, 450);
    beep(220, 120);
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
