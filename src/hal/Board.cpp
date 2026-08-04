#include "Board.h"

#include <math.h>
#include "ui/Ui.h"

namespace {
constexpr uint32_t TOUCH_CAL_MAGIC = 0x43594431UL;
constexpr uint8_t CMD_READ_X = 0xD0;
constexpr uint8_t CMD_READ_Y = 0x90;
constexpr uint8_t CMD_READ_Z1 = 0xB0;
constexpr uint8_t CMD_READ_Z2 = 0xC0;

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
    tft_.setTextWrap(false, false);
    Ui::clear(tft_);

    prefs_.begin("cydkids", false);
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
    next.down = mapTouch(raw, next.x, next.y);
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
    beep(1175, 55);
}

void Board::beepError() {
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
