#include "Board.h"

#include "BleBeacon.h"

namespace {
constexpr uint8_t RGB_CH_R = 5;
constexpr uint8_t RGB_CH_G = 6;
constexpr uint8_t RGB_CH_B = 7;
constexpr uint32_t RGB_PWM_HZ = 5000;
constexpr uint8_t RGB_PWM_BITS = 8;
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

void Board::setRgb(bool red, bool green, bool blue) {
    digitalWrite(PIN_RGB_R, red ? LOW : HIGH);
    digitalWrite(PIN_RGB_G, green ? LOW : HIGH);
    digitalWrite(PIN_RGB_B, blue ? LOW : HIGH);
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
    if (!rgbReady_) {
        ledcSetup(RGB_CH_R, RGB_PWM_HZ, RGB_PWM_BITS);
        ledcSetup(RGB_CH_G, RGB_PWM_HZ, RGB_PWM_BITS);
        ledcSetup(RGB_CH_B, RGB_PWM_HZ, RGB_PWM_BITS);
        ledcAttachPin(PIN_RGB_R, RGB_CH_R);
        ledcAttachPin(PIN_RGB_G, RGB_CH_G);
        ledcAttachPin(PIN_RGB_B, RGB_CH_B);
        rgbReady_ = true;
    }
    rgbR_ = r;
    rgbG_ = g;
    rgbB_ = b;
    ledcWrite(RGB_CH_R, 255 - r);
    ledcWrite(RGB_CH_G, 255 - g);
    ledcWrite(RGB_CH_B, 255 - b);
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
