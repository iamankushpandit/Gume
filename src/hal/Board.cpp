#include "Board.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <sys/time.h>
#include <time.h>

#include <math.h>
#include <WiFi.h>
#include "ui/Ui.h"

namespace {
constexpr uint32_t TOUCH_CAL_MAGIC = 0x43594431UL;
constexpr uint8_t CMD_READ_X = 0xD0;
constexpr uint8_t CMD_READ_Y = 0x90;
constexpr uint8_t CMD_READ_Z1 = 0xB0;
constexpr uint8_t CMD_READ_Z2 = 0xC0;
constexpr const char* DEFAULT_NTP_SERVER = "pool.ntp.org";

uint16_t read16(File& file) {
    uint16_t result = file.read();
    result |= static_cast<uint16_t>(file.read()) << 8;
    return result;
}

uint32_t read32(File& file) {
    uint32_t result = file.read();
    result |= static_cast<uint32_t>(file.read()) << 8;
    result |= static_cast<uint32_t>(file.read()) << 16;
    result |= static_cast<uint32_t>(file.read()) << 24;
    return result;
}

void drawCrosshair(TFT_eSPI& tft, int16_t x, int16_t y, uint16_t color) {
    tft.drawCircle(x, y, 14, color);
    tft.drawCircle(x, y, 7, color);
    tft.drawLine(x - 18, y, x + 18, y, color);
    tft.drawLine(x, y - 18, x, y + 18, color);
}

bool computeAffine(const int16_t raw[3][2], const int16_t screen[3][2], Board::TouchCalibration& cal) {
    const float x0 = raw[0][0];
    const float y0 = raw[0][1];
    const float x1 = raw[1][0];
    const float y1 = raw[1][1];
    const float x2 = raw[2][0];
    const float y2 = raw[2][1];
    const float denom = x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1);
    if (fabsf(denom) < 0.001f) {
        return false;
    }

    const float sx0 = screen[0][0];
    const float sy0 = screen[0][1];
    const float sx1 = screen[1][0];
    const float sy1 = screen[1][1];
    const float sx2 = screen[2][0];
    const float sy2 = screen[2][1];

    cal.ax = (sx0 * (y1 - y2) + sx1 * (y2 - y0) + sx2 * (y0 - y1)) / denom;
    cal.bx = (sx0 * (x2 - x1) + sx1 * (x0 - x2) + sx2 * (x1 - x0)) / denom;
    cal.cx = (sx0 * (x1 * y2 - x2 * y1) + sx1 * (x2 * y0 - x0 * y2) + sx2 * (x0 * y1 - x1 * y0)) / denom;
    cal.ay = (sy0 * (y1 - y2) + sy1 * (y2 - y0) + sy2 * (y0 - y1)) / denom;
    cal.by = (sy0 * (x2 - x1) + sy1 * (x0 - x2) + sy2 * (x1 - x0)) / denom;
    cal.cy = (sy0 * (x1 * y2 - x2 * y1) + sy1 * (x2 * y0 - x0 * y2) + sy2 * (x0 * y1 - x1 * y0)) / denom;
    cal.magic = TOUCH_CAL_MAGIC;
    return true;
}
}

Board::Board() : sdSpi_(VSPI) {}

void Board::begin() {
    Serial.begin(115200);
    delay(100);

    pinMode(PIN_TFT_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_TFT_BACKLIGHT, HIGH);

    pinMode(PIN_RGB_R, OUTPUT);
    pinMode(PIN_RGB_G, OUTPUT);
    pinMode(PIN_RGB_B, OUTPUT);
    setRgb(false, false, false);

    pinMode(PIN_SPEAKER, OUTPUT);
    digitalWrite(PIN_SPEAKER, LOW);

    pinMode(PIN_TOUCH_MOSI, OUTPUT);
    pinMode(PIN_TOUCH_MISO, INPUT);
    pinMode(PIN_TOUCH_SCLK, OUTPUT);
    pinMode(PIN_TOUCH_CS, OUTPUT);
    pinMode(PIN_TOUCH_IRQ, INPUT);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    digitalWrite(PIN_TOUCH_SCLK, LOW);

    tft_.init();
    tft_.setRotation(CYD_SCREEN_ROTATION);
    displayRotation_ = CYD_SCREEN_ROTATION;
    tft_.setTextWrap(false, false);
    Ui::clear(tft_);

    prefs_.begin("cydkids", false);
    applyBrightness();      // needs prefs, so it runs after begin()
    loadTouchCalibration();
    mountSd();
}

TFT_eSPI& Board::display() {
    return tft_;
}

bool Board::sdReady() const {
    return sdMounted_;
}

bool Board::mountSd() {
    sdSpi_.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    sdMounted_ = SD.begin(PIN_SD_CS, sdSpi_, 16000000);
    setRgb(!sdMounted_, sdMounted_, false);
    Serial.printf("SD mount: %s\n", sdMounted_ ? "ok" : "failed");
    return sdMounted_;
}

bool Board::hasTouchCalibration() const {
    return cal_.magic == TOUCH_CAL_MAGIC;
}

bool Board::loadTouchCalibration() {
    const size_t len = prefs_.getBytesLength("touchCal");
    if (len != sizeof(TouchCalibration)) {
        cal_ = TouchCalibration{};
        return false;
    }
    prefs_.getBytes("touchCal", &cal_, sizeof(TouchCalibration));
    if (cal_.magic != TOUCH_CAL_MAGIC) {
        cal_ = TouchCalibration{};
        return false;
    }
    return true;
}

void Board::saveTouchCalibration() {
    prefs_.putBytes("touchCal", &cal_, sizeof(TouchCalibration));
}

uint16_t Board::readTouchAdc(uint8_t command) {
    uint16_t raw = 0;
    digitalWrite(PIN_TOUCH_CS, LOW);

    for (int8_t bit = 7; bit >= 0; --bit) {
        digitalWrite(PIN_TOUCH_MOSI, (command & (1 << bit)) ? HIGH : LOW);
        digitalWrite(PIN_TOUCH_SCLK, HIGH);
        delayMicroseconds(2);
        digitalWrite(PIN_TOUCH_SCLK, LOW);
        delayMicroseconds(2);
    }

    for (uint8_t bit = 0; bit < 16; ++bit) {
        digitalWrite(PIN_TOUCH_SCLK, HIGH);
        delayMicroseconds(2);
        raw = static_cast<uint16_t>((raw << 1) | (digitalRead(PIN_TOUCH_MISO) ? 1 : 0));
        digitalWrite(PIN_TOUCH_SCLK, LOW);
        delayMicroseconds(2);
    }

    digitalWrite(PIN_TOUCH_CS, HIGH);
    return static_cast<uint16_t>((raw >> 3) & 0x0FFF);
}

Board::RawTouch Board::readRawTouch() {
    RawTouch touch;
    const uint16_t z1 = readTouchAdc(CMD_READ_Z1);
    const uint16_t z2 = readTouchAdc(CMD_READ_Z2);
    uint16_t pressure = 0;
    if (z1 > 0 && z2 > z1) {
        pressure = static_cast<uint16_t>(z1 + 4095 - z2);
    }

    const bool irqDown = digitalRead(PIN_TOUCH_IRQ) == LOW;
    if (!irqDown && pressure < TOUCH_PRESSURE_THRESHOLD) {
        return touch;
    }

    uint32_t x = 0;
    uint32_t y = 0;
    constexpr uint8_t samples = 5;
    for (uint8_t i = 0; i < samples; ++i) {
        x += readTouchAdc(CMD_READ_X);
        y += readTouchAdc(CMD_READ_Y);
        delayMicroseconds(200);
    }

    touch.down = true;
    touch.x = static_cast<int16_t>(x / samples);
    touch.y = static_cast<int16_t>(y / samples);
    touch.pressure = pressure;
    return touch;
}

bool Board::waitForStableRaw(int16_t& rawX, int16_t& rawY) {
    const uint32_t deadline = millis() + 20000UL;
    while (millis() < deadline) {
        RawTouch first = readRawTouch();
        if (!first.down) {
            delay(20);
            continue;
        }

        uint32_t sumX = 0;
        uint32_t sumY = 0;
        uint8_t count = 0;
        for (uint8_t i = 0; i < 10; ++i) {
            RawTouch sample = readRawTouch();
            if (sample.down) {
                sumX += sample.x;
                sumY += sample.y;
                ++count;
            }
            delay(12);
        }
        if (count >= 6) {
            rawX = static_cast<int16_t>(sumX / count);
            rawY = static_cast<int16_t>(sumY / count);
            while (readRawTouch().down && millis() < deadline) {
                delay(20);
            }
            return true;
        }
    }
    return false;
}

void Board::runTouchCalibration() {
    /* Always calibrate in rotation 1. mapTouch() stores an affine in
     * rotation-1 coordinates and pollTouch() derives every other orientation
     * from it, so calibrating in any other rotation would be applied twice. */
    const uint8_t savedRotation = displayRotation_;
    tft_.setRotation(1);
    displayRotation_ = 1;

    constexpr int16_t screenPts[3][2] = {
        {28, 36},
        {SCREEN_WIDTH - 29, SCREEN_HEIGHT / 2},
        {SCREEN_WIDTH / 2, SCREEN_HEIGHT - 29},
    };
    int16_t rawPts[3][2] = {};
    const char* labels[3] = {"top-left target", "right target", "bottom target"};

    for (uint8_t i = 0; i < 3; ++i) {
        Ui::clear(tft_);
        tft_.setTextColor(Ui::text(), Ui::bg());
        tft_.setTextDatum(TC_DATUM);
        tft_.drawString("Touch Calibration", SCREEN_WIDTH / 2, 12, 4);
        tft_.drawString(String("Tap and hold the ") + labels[i], SCREEN_WIDTH / 2, 48, 2);
        drawCrosshair(tft_, screenPts[i][0], screenPts[i][1], Ui::warning());
        if (!waitForStableRaw(rawPts[i][0], rawPts[i][1])) {
            Ui::clear(tft_);
            tft_.setTextDatum(MC_DATUM);
            tft_.setTextColor(Ui::error(), Ui::bg());
            tft_.drawString("Calibration timed out", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 4);
            delay(1500);
            tft_.setTextDatum(TL_DATUM);
            return;
        }
        beepOk();
    }

    TouchCalibration next;
    if (computeAffine(rawPts, screenPts, next)) {
        cal_ = next;
        saveTouchCalibration();
        Ui::clear(tft_);
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(Ui::success(), Ui::bg());
        tft_.drawString("Calibration saved", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 4);
        delay(1200);
    } else {
        Ui::clear(tft_);
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(Ui::error(), Ui::bg());
        tft_.drawString("Calibration failed", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 4);
        delay(1500);
    }
    tft_.setTextDatum(TL_DATUM);

    tft_.setRotation(savedRotation);
    displayRotation_ = savedRotation;
}

bool Board::mapTouch(const RawTouch& raw, int16_t& x, int16_t& y) const {
    if (!raw.down || cal_.magic != TOUCH_CAL_MAGIC) {
        return false;
    }
    const float mappedX = cal_.ax * raw.x + cal_.bx * raw.y + cal_.cx;
    const float mappedY = cal_.ay * raw.x + cal_.by * raw.y + cal_.cy;
    x = constrain(static_cast<int16_t>(lroundf(mappedX)), 0, SCREEN_WIDTH - 1);
    y = constrain(static_cast<int16_t>(lroundf(mappedY)), 0, SCREEN_HEIGHT - 1);
    return true;
}

TouchPoint Board::pollTouch() {
    const RawTouch raw = readRawTouch();
    TouchPoint next;
    next.pressure = raw.pressure;
    int16_t lx = 0, ly = 0;
    next.down = mapTouch(raw, lx, ly); // lx/ly are always landscape (rot=1) calibrated
    if (next.down) {
        switch (displayRotation_) {
            case 2: // portrait: rotate 90° CW from landscape
                next.x = ly;
                next.y = static_cast<int16_t>(SCREEN_WIDTH - 1 - lx);
                break;
            case 0: // portrait: rotate 90° CCW from landscape
                next.x = static_cast<int16_t>(SCREEN_HEIGHT - 1 - ly);
                next.y = lx;
                break;
            case 3: // landscape flipped
                next.x = static_cast<int16_t>(SCREEN_WIDTH - 1 - lx);
                next.y = static_cast<int16_t>(SCREEN_HEIGHT - 1 - ly);
                break;
            default: // 1 = standard landscape (calibrated)
                next.x = lx;
                next.y = ly;
                break;
        }
    }
    next.justPressed = next.down && !lastTouch_.down;
    next.justReleased = !next.down && lastTouch_.down;
    if (!next.down) {
        next.x = lastTouch_.x;
        next.y = lastTouch_.y;
    }
    lastTouch_ = next;
    return next;
}

TouchPoint Board::touch() const {
    return lastTouch_;
}

void Board::beep(uint16_t frequency, uint16_t ms) {
    (void)frequency;
    (void)ms;
}

void Board::beepOk() {
    pulseRgb(0, 255, 40, 450);      // green = correct
    beep(1175, 55);
}

void Board::beepError() {
    pulseRgb(255, 0, 0, 450);       // red = wrong
    beep(220, 120);
}

uint32_t Board::getScore(const char* key, uint32_t fallback) {
    return prefs_.getUInt(key, fallback);
}

void Board::setScore(const char* key, uint32_t value) {
    prefs_.putUInt(key, value);
}

bool Board::saveBestScore(const char* key, uint32_t value, bool lowerIsBetter) {
    const uint32_t missing = lowerIsBetter ? UINT32_MAX : 0;
    const uint32_t current = prefs_.getUInt(key, missing);
    const bool shouldSave = lowerIsBetter ? value < current : value > current;
    if (shouldSave) {
        prefs_.putUInt(key, value);
    }
    return shouldSave;
}

void Board::loadBlob(const char* key, void* dst, size_t len) {
    // isKey() first so a first run does not log an NVS "not found" error.
    if (!prefs_.isKey(key)) return;
    if (prefs_.getBytesLength(key) != len) return;   // size changed: start over
    prefs_.getBytes(key, dst, len);
}

void Board::saveBlob(const char* key, const void* src, size_t len) {
    prefs_.putBytes(key, src, len);
}

Board::ThemeMode Board::themeMode() {
    return prefs_.getUChar("themeMode", static_cast<uint8_t>(ThemeMode::Dark)) == static_cast<uint8_t>(ThemeMode::Light)
        ? ThemeMode::Light
        : ThemeMode::Dark;
}

void Board::setThemeMode(ThemeMode mode) {
    prefs_.putUChar("themeMode", static_cast<uint8_t>(mode));
}

namespace {
constexpr uint8_t  BL_CHANNEL  = 4;      // RGB LED uses 5, 6 and 7
constexpr uint32_t BL_PWM_HZ   = 5000;
constexpr uint8_t  BL_PWM_BITS = 8;
bool blReady = false;
}

uint8_t Board::brightness() {
    const uint8_t v = prefs_.getUChar("bright", 100);
    if (v < BRIGHTNESS_MIN) return BRIGHTNESS_MIN;
    return v > 100 ? 100 : v;
}

void Board::setBrightness(uint8_t percent) {
    if (percent < BRIGHTNESS_MIN) percent = BRIGHTNESS_MIN;
    if (percent > 100) percent = 100;
    prefs_.putUChar("bright", percent);
    applyBrightness();
}

void Board::applyBrightness() {
    if (!blReady) {
        ledcSetup(BL_CHANNEL, BL_PWM_HZ, BL_PWM_BITS);
        ledcAttachPin(PIN_TFT_BACKLIGHT, BL_CHANNEL);
        blReady = true;
    }
    // Backlight is active high on this board, so duty maps straight through.
    ledcWrite(BL_CHANNEL, (brightness() * 255) / 100);
}

bool Board::screenFlipped() {
    return prefs_.getBool("scrFlip", false);
}

void Board::setScreenFlipped(bool flipped) {
    prefs_.putBool("scrFlip", flipped);
}

Board::LayoutMode Board::layoutMode() {
    return prefs_.getUChar("layoutMode", static_cast<uint8_t>(LayoutMode::Horizontal)) == static_cast<uint8_t>(LayoutMode::Vertical)
        ? LayoutMode::Vertical
        : LayoutMode::Horizontal;
}

void Board::setLayoutMode(LayoutMode mode) {
    prefs_.putUChar("layoutMode", static_cast<uint8_t>(mode));
}

uint16_t Board::screenSaverSeconds() {
    const uint16_t stored = prefs_.getUShort("idleSecs", 300);
    return stored < 15 ? 15 : stored;
}

void Board::setScreenSaverSeconds(uint16_t seconds) {
    const uint16_t clamped = seconds < 15 ? 15 : seconds;
    prefs_.putUShort("idleSecs", clamped);
}

bool Board::gameVisible(const char* appId, bool fallback) {
    char key[20];
    snprintf(key, sizeof(key), "show_%s", appId);
    return prefs_.getBool(key, fallback);
}

void Board::setGameVisible(const char* appId, bool visible) {
    char key[20];
    snprintf(key, sizeof(key), "show_%s", appId);
    prefs_.putBool(key, visible);
}

void Board::loadWifiCache() {
    if (wifiCacheLoaded_) return;
    // isKey() first, so a missing entry does not log an NVS error every call.
    wifiSsidCache_ = prefs_.isKey("wifiSsid") ? prefs_.getString("wifiSsid", "") : String();
    wifiPassCache_ = prefs_.isKey("wifiPass") ? prefs_.getString("wifiPass", "") : String();
    wifiCacheLoaded_ = true;
}

String Board::wifiSsid() {
    loadWifiCache();
    return wifiSsidCache_;
}

String Board::wifiPassword() {
    loadWifiCache();
    return wifiPassCache_;
}

void Board::setWifiCredentials(const String& ssid, const String& password) {
    const size_t nS = prefs_.putString("wifiSsid", ssid);
    const size_t nP = prefs_.putString("wifiPass", password);
    wifiSsidCache_ = ssid;
    wifiPassCache_ = password;
    wifiCacheLoaded_ = true;
    // putString returns 0 when the write fails (a full NVS partition is the
    // usual cause), so log the byte counts rather than assuming success.
    Serial.printf("[wifi] saved '%s' -> ssid %u bytes, pass %u bytes\n",
                  ssid.c_str(), (unsigned)nS, (unsigned)nP);
}

void Board::clearWifiCredentials() {
    prefs_.remove("wifiSsid");
    prefs_.remove("wifiPass");
    wifiSsidCache_ = String();
    wifiPassCache_ = String();
    wifiCacheLoaded_ = true;
}

bool Board::hasWifiCredentials() {
    loadWifiCache();
    return wifiSsidCache_.length() > 0;
}

bool Board::ntpEnabled() {
    return prefs_.getBool("ntpOn", true);
}

void Board::setNtpEnabled(bool enabled) {
    prefs_.putBool("ntpOn", enabled);
}

String Board::ntpServer() {
    String server = prefs_.getString("ntpServer", DEFAULT_NTP_SERVER);
    if (server.length() == 0) {
        server = DEFAULT_NTP_SERVER;
    }
    return server;
}

void Board::setNtpServer(const String& server) {
    const String value = server.length() > 0 ? server : String(DEFAULT_NTP_SERVER);
    prefs_.putString("ntpServer", value);
}

bool Board::isWifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void Board::setDisplayRotation(uint8_t rotation) {
    displayRotation_ = rotation;
    tft_.setRotation(rotation);
    lastTouch_ = TouchPoint{}; // clear stale touch after rotation change
}

uint8_t Board::displayRotation() const {
    return displayRotation_;
}

void Board::setRgb(bool red, bool green, bool blue) {
    digitalWrite(PIN_RGB_R, red ? LOW : HIGH);
    digitalWrite(PIN_RGB_G, green ? LOW : HIGH);
    digitalWrite(PIN_RGB_B, blue ? LOW : HIGH);
}

bool Board::drawBmp(const char* path, int16_t x, int16_t y, int16_t maxW, int16_t maxH) {
    if (!sdMounted_) {
        return false;
    }
    File bmp = SD.open(path, FILE_READ);
    if (!bmp) {
        return false;
    }

    if (read16(bmp) != 0x4D42) {
        bmp.close();
        return false;
    }
    (void)read32(bmp);
    (void)read32(bmp);
    const uint32_t imageOffset = read32(bmp);
    const uint32_t headerSize = read32(bmp);
    if (headerSize < 40) {
        bmp.close();
        return false;
    }

    int32_t bmpW = static_cast<int32_t>(read32(bmp));
    int32_t bmpH = static_cast<int32_t>(read32(bmp));
    if (read16(bmp) != 1) {
        bmp.close();
        return false;
    }
    const uint16_t depth = read16(bmp);
    const uint32_t compression = read32(bmp);
    if ((depth != 24 && depth != 16) || compression != 0 || bmpW <= 0 || bmpH == 0) {
        bmp.close();
        return false;
    }

    const bool flip = bmpH > 0;
    if (bmpH < 0) {
        bmpH = -bmpH;
    }
    const int16_t drawW = min<int32_t>(bmpW, maxW);
    const int16_t drawH = min<int32_t>(bmpH, maxH);
    const uint32_t rowSize = ((static_cast<uint32_t>(bmpW) * depth + 31) / 32) * 4;

    for (int16_t row = 0; row < drawH; ++row) {
        const uint32_t sourceRow = flip ? (bmpH - 1 - row) : row;
        bmp.seek(imageOffset + sourceRow * rowSize);
        for (int16_t col = 0; col < drawW; ++col) {
            uint16_t color = TFT_BLACK;
            if (depth == 24) {
                const uint8_t b = bmp.read();
                const uint8_t g = bmp.read();
                const uint8_t r = bmp.read();
                color = Ui::rgb(r, g, b);
            } else {
                color = read16(bmp);
            }
            const int16_t px = x + col;
            const int16_t py = y + row;
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                tft_.drawPixel(px, py, color);
            }
        }
    }

    bmp.close();
    return true;
}

// ---------------------------------------------------------------------------
// Time sync
//
// The radio exists solely to fetch the time. We connect on demand, call
// configTime(), and then re-issue it every five minutes so the clock cannot
// drift. Re-calling configTime() is used instead of sntp_set_sync_interval()
// because that symbol was renamed across ESP32 core versions.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t TIME_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t TIME_SYNC_TIMEOUT_MS    = 45000;
constexpr uint32_t TIME_RETRY_MS           = 5UL * 60UL * 1000UL;
constexpr uint32_t TIME_RESYNC_MS          = 5UL * 60UL * 1000UL;

bool clockLooksValid() {
    struct tm t;
    // A year past 2020 means SNTP has actually delivered a real timestamp.
    return getLocalTime(&t, 0) && t.tm_year > (2020 - 1900);
}
}

namespace {
/* Named zones with their standard-time offset in minutes. DST is handled by
 * re-detecting from the public IP, which reports the offset currently in
 * effect -- simpler and more reliable than shipping a TZ rule database. */
struct TzZone { const char* name; const char* posix; };
/* POSIX TZ strings rather than fixed offsets, so daylight saving is handled by
 * the C library. A plain offset would be correct for only part of the year --
 * "US Central" is UTC-6 in winter but UTC-5 from March to November. */
const TzZone TZ_ZONES[] = {
    { "UTC",            "UTC0"                          },
    { "US Eastern",     "EST5EDT,M3.2.0,M11.1.0"        },
    { "US Central",     "CST6CDT,M3.2.0,M11.1.0"        },
    { "US Mountain",    "MST7MDT,M3.2.0,M11.1.0"        },
    { "US Arizona",     "MST7"                          },
    { "US Pacific",     "PST8PDT,M3.2.0,M11.1.0"        },
    { "US Alaska",      "AKST9AKDT,M3.2.0,M11.1.0"      },
    { "Hawaii",         "HST10"                         },
    { "Brazil (Sao Paulo)", "<-03>3"                    },
    { "UK / Ireland",   "GMT0BST,M3.5.0/1,M10.5.0"      },
    { "Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"    },
    { "East Europe",    "EET-2EEST,M3.5.0/3,M10.5.0/4"  },
    { "India",          "IST-5:30"                      },
    { "China",          "CST-8"                         },
    { "Japan / Korea",  "JST-9"                         },
    { "Sydney",         "AEST-10AEDT,M10.1.0,M4.1.0/3"  },
    { "New Zealand",    "NZST-12NZDT,M9.5.0,M4.1.0/3"   },
};
constexpr uint8_t TZ_ZONE_COUNT = sizeof(TZ_ZONES) / sizeof(TZ_ZONES[0]);
}

uint8_t Board::tzZoneCount() { return TZ_ZONE_COUNT; }

const char* Board::tzZonePosix(uint8_t index) {
    if (index >= TZ_ZONE_COUNT) return "UTC0";
    return TZ_ZONES[index].posix;
}

const char* Board::tzZoneName(uint8_t index) {
    if (index >= TZ_ZONE_COUNT) return "?";
    return TZ_ZONES[index].name;
}

bool Board::tzZoneChosen() {
    return prefs_.getUChar("tzZone", 0xFF) != 0xFF;
}

uint8_t Board::tzZoneIndex() {
    const uint8_t i = prefs_.getUChar("tzZone", 0);
    return i < TZ_ZONE_COUNT ? i : 0;
}

void Board::setTzZoneIndex(uint8_t index) {
    if (index >= TZ_ZONE_COUNT) index = 0;
    prefs_.putUChar("tzZone", index);
    tzAutoDetected_ = false;
    if (WiFi.status() == WL_CONNECTED) {
        applyTimeConfig();
        lastResyncMs_ = millis();
    } else {
        // Apply the rules locally so the displayed time is right even offline.
        setenv("TZ", TZ_ZONES[index].posix, 1);
        tzset();
    }
}

int16_t Board::tzOffsetMinutes() {
    return static_cast<int16_t>(prefs_.getShort("tzOffMin", 0));
}

void Board::setTzOffsetMinutes(int16_t minutes) {
    if (minutes < -12 * 60) minutes = -12 * 60;
    if (minutes >  14 * 60) minutes =  14 * 60;
    prefs_.putShort("tzOffMin", minutes);
    if (WiFi.status() == WL_CONNECTED) {
        applyTimeConfig();
        lastResyncMs_ = millis();
    }
}

bool Board::detectTimezone() {
    if (WiFi.status() != WL_CONNECTED) return false;

    /* ip-api.com's "line" format returns the raw value with no JSON to parse.
     * The offset it reports already includes DST, so re-running this on each
     * sync keeps the clock right across the spring/autumn transitions. */
    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin("http://ip-api.com/line/?fields=offset")) {
        Serial.println("[time] tz lookup: http.begin failed");
        return false;
    }
    Serial.println("[time] tz lookup: querying ip-api.com");

    const int code = http.GET();
    if (code != 200) {
        Serial.printf("[time] tz lookup failed, http %d\n", code);
        http.end();
        return false;
    }

    const String body = http.getString();
    http.end();

    const long seconds = body.toInt();
    if (seconds == 0 && body.indexOf('0') < 0) return false;   // unparseable
    if (seconds < -12 * 3600L || seconds > 14 * 3600L) return false;

    const int16_t minutes = static_cast<int16_t>(seconds / 60);
    Serial.printf("[time] detected UTC%+d:%02d from public IP\n",
                  minutes / 60, abs(minutes % 60));
    prefs_.putShort("tzOffMin", minutes);
    tzAutoDetected_ = true;
    applyTimeConfig();
    return true;
}

bool Board::timeSynced() const {
    return timeSyncState_ == TimeSyncState::Synced;
}

void Board::beginTimeSync() {
    if (!ntpEnabled() || !hasWifiCredentials()) return;
    if (timeSyncState_ == TimeSyncState::Connecting ||
        timeSyncState_ == TimeSyncState::Syncing) return;

    lastSyncAttemptMs_ = millis();

    if (WiFi.status() == WL_CONNECTED) {
        applyTimeConfig();
        // Same direct query used on the fresh-connect path; SNTP alone stays
        // silent on this network.
        if (!ntpUdpProbe(ntpServer().c_str())) ntpUdpProbe("time.google.com");
        timeSyncStartedMs_ = millis();
        timeSyncState_ = TimeSyncState::Syncing;
        Serial.println("[time] already connected -> syncing");
        return;
    }

    /* Only start our own association if nothing else is mid-connect. The Wi-Fi
     * setup screen issues its own WiFi.begin(); firing a second one on top of
     * it used to restart the handshake underneath the user. */
    const uint32_t now = millis();
    if (lastWifiBeginMs_ != 0 && now - lastWifiBeginMs_ < 30000UL) {
        timeSyncState_ = TimeSyncState::Connecting;
        timeSyncStartedMs_ = now;
        return;
    }

    WiFi.mode(WIFI_STA);
    const String ssid = wifiSsid();
    const String pass = wifiPassword();
    Serial.printf("[time] connecting to %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    lastWifiBeginMs_ = now;
    timeSyncStartedMs_ = now;
    timeSyncState_ = TimeSyncState::Connecting;
}

/* Single place that programs SNTP, so the timezone can never be forgotten the
 * way the Wi-Fi screen's hardcoded configTime(0, 0, ...) did. */
void Board::applyTimeConfig() {
    /* A hand-picked zone uses its POSIX rules, so daylight saving is applied by
     * the C library -- "US Central" is UTC-6 in winter but UTC-5 from March to
     * November, and a stored fixed offset would be wrong for most of the year.
     * Only fall back to a raw offset when the zone is still auto-detected, in
     * which case the detected value already reflects the DST in force. */
    const uint8_t z = prefs_.getUChar("tzZone", 0xFF);
    if (z != 0xFF && z < tzZoneCount()) {
        const char* posix = tzZonePosix(z);
        configTzTime(posix, ntpServer().c_str(), "time.google.com", "time.cloudflare.com");
        Serial.printf("[time] configTzTime %s (%s)\n", tzZoneName(z), posix);
    } else {
        const long off = static_cast<long>(tzOffsetMinutes()) * 60L;
        configTime(off, 0, ntpServer().c_str(), "time.google.com", "time.cloudflare.com");
        Serial.printf("[time] configTime offset=%lds server=%s\n", off, ntpServer().c_str());
    }
}

bool Board::ntpUdpProbe(const char* host) {
    if (WiFi.status() != WL_CONNECTED) return false;

    IPAddress ip;
    if (!WiFi.hostByName(host, ip)) {
        Serial.printf("[time] DNS FAILED for %s\n", host);
        return false;
    }
    Serial.printf("[time] %s -> %s\n", host, ip.toString().c_str());

    WiFiUDP udp;
    if (!udp.begin(2390)) {
        Serial.println("[time] udp begin failed");
        return false;
    }

    uint8_t pkt[48] = {0};
    pkt[0] = 0b11100011;          // LI=3 (unsynced), VN=4, Mode=3 (client)
    udp.beginPacket(ip, 123);
    udp.write(pkt, 48);
    udp.endPacket();

    const uint32_t t0 = millis();
    while (millis() - t0 < 4000) {
        if (udp.parsePacket() >= 48) {
            udp.read(pkt, 48);
            udp.stop();
            // Transmit timestamp, seconds since 1900. Convert to Unix epoch.
            const uint32_t ntpSecs = (static_cast<uint32_t>(pkt[40]) << 24) |
                                     (static_cast<uint32_t>(pkt[41]) << 16) |
                                     (static_cast<uint32_t>(pkt[42]) << 8)  |
                                      static_cast<uint32_t>(pkt[43]);
            if (ntpSecs == 0) {
                Serial.println("[time] UDP reply had a zero timestamp");
                return false;
            }
            const uint32_t unixSecs = ntpSecs - 2208988800UL;
            struct timeval tv;
            tv.tv_sec = static_cast<time_t>(unixSecs);
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);
            Serial.printf("[time] UDP NTP OK, clock set from %s (unix %u)\n",
                          host, (unsigned)unixSecs);
            return true;
        }
        delay(20);
    }
    udp.stop();
    Serial.printf("[time] no UDP reply from %s - port 123 blocked?\n", host);
    return false;
}

void Board::syncTimeNow() {
    /* An explicit tap on "Sync now" is a direct instruction, so it deliberately
     * ignores the Auto-time switch. Routing it through beginTimeSync() meant
     * the ntpEnabled() guard swallowed it silently -- the log filled with
     * "manual sync requested" and nothing ever happened. */
    timeSyncState_ = TimeSyncState::Idle;
    lastSyncAttemptMs_ = 0;
    lastResyncMs_ = 0;
    Serial.println("[time] manual sync requested");

    if (!hasWifiCredentials()) {
        Serial.println("[time] manual sync: no saved network");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[time] manual sync: not connected, associating");
        beginTimeSync();
        return;
    }

    if (!tzZoneChosen()) detectTimezone();
    applyTimeConfig();
    if (!ntpUdpProbe(ntpServer().c_str())) {
        ntpUdpProbe("time.google.com");
    }
    timeSyncStartedMs_ = millis();
    timeSyncState_ = TimeSyncState::Syncing;
}

void Board::tickTimeSync() {
    const uint32_t now = millis();

    switch (timeSyncState_) {
        case TimeSyncState::Idle:
            if (ntpEnabled() && hasWifiCredentials() &&
                (lastSyncAttemptMs_ == 0 || now - lastSyncAttemptMs_ >= TIME_RETRY_MS)) {
                beginTimeSync();
            }
            break;

        case TimeSyncState::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[time] wifi up, ip=%s rssi=%d\n",
                              WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
                // Only auto-detect while the user has not chosen a zone by hand.
                if (prefs_.getUChar("tzZone", 0xFF) == 0xFF) detectTimezone();
                applyTimeConfig();
                // One direct query up front: it proves DNS and UDP/123 in the
                // log, and usually sets the clock before SNTP even polls.
                if (!ntpUdpProbe(ntpServer().c_str())) {
                    ntpUdpProbe("time.google.com");
                }
                timeSyncStartedMs_ = now;
                timeSyncState_ = TimeSyncState::Syncing;
            } else if (now - timeSyncStartedMs_ > TIME_CONNECT_TIMEOUT_MS) {
                Serial.printf("[time] connect timeout, status=%d\n", (int)WiFi.status());
                timeSyncState_ = TimeSyncState::Idle;
            }
            break;

        case TimeSyncState::Syncing:
            if (clockLooksValid()) {
                struct tm t;
                getLocalTime(&t, 0);
                Serial.printf("[time] SYNCED %04d-%02d-%02d %02d:%02d:%02d (%s)\n",
                              t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                              t.tm_hour, t.tm_min, t.tm_sec,
                              /* Name the zone rather than the stored offset.
                               * The stored value is only the auto-detect
                               * fallback and diverges from the POSIX rules
                               * under DST, which previously made this log read
                               * "tz-6" while the clock was correctly on -5. */
                              tzZoneChosen() ? tzZoneName(tzZoneIndex()) : "auto");
                timeSyncState_ = TimeSyncState::Synced;
                lastResyncMs_ = now;
            } else if (now - timeSyncStartedMs_ > TIME_SYNC_TIMEOUT_MS) {
                // SNTP stayed silent. Try the direct query once more before
                // giving up; it uses a different code path inside lwIP.
                Serial.println("[time] sntp timeout, trying direct UDP");
                if (ntpUdpProbe("time.google.com") || ntpUdpProbe("time.cloudflare.com")) {
                    timeSyncState_ = TimeSyncState::Synced;
                    lastResyncMs_ = now;
                } else {
                    timeSyncState_ = TimeSyncState::Idle;
                }
            }
            break;

        case TimeSyncState::Synced:
            if (now - lastResyncMs_ >= TIME_RESYNC_MS) {
                lastResyncMs_ = now;
                if (WiFi.status() == WL_CONNECTED) {
                    applyTimeConfig();
                } else {
                    timeSyncState_ = TimeSyncState::Idle;
                    lastSyncAttemptMs_ = 0;
                }
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// RGB status LED
//
// Common anode: pulling a pin LOW lights that channel, so the PWM duty is
// inverted. Three LEDC channels give full colour mixing, bright enough to read
// through a translucent enclosure.
// ---------------------------------------------------------------------------
namespace {
constexpr uint8_t RGB_CH_R = 5;
constexpr uint8_t RGB_CH_G = 6;
constexpr uint8_t RGB_CH_B = 7;
constexpr uint32_t RGB_PWM_HZ   = 5000;
constexpr uint8_t  RGB_PWM_BITS = 8;
}

void Board::setRgbEnabled(bool on) {
    prefs_.putBool("rgbOn", on);
    if (!on) setRgbColor(0, 0, 0);
}

bool Board::rgbEnabled() {
    return prefs_.getBool("rgbOn", true);
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
    rgbR_ = r; rgbG_ = g; rgbB_ = b;
    // Inverted: duty 255 = fully off on a common-anode LED.
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

