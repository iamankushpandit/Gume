#include "Board.h"

#include <math.h>
#include "Watchdog.h"
#include "ui/Ui.h"

namespace {
/* The calibration is a 3-point affine fit onto SCREEN_WIDTH x SCREEN_HEIGHT,
 * so it is only meaningful for the panel geometry it was captured on. Nothing
 * checked that, and the failure is silent and confusing: moving the same NVS
 * to a 480x320 panel left a calibration fitted to 320x240, so touches mapped
 * into a corner of the screen and the device looked like its digitiser had
 * died.
 *
 * Folding the geometry into the magic makes a calibration from another panel
 * simply not validate, which drops the owner into the calibration wizard at
 * boot -- the correct behaviour. The literal is preserved verbatim for the
 * 320x240 panel it was minted on, so 2.8-inch devices in the field keep the
 * calibration they already have. */
constexpr uint32_t TOUCH_CAL_BASE_MAGIC = 0x43594431UL;
constexpr uint32_t TOUCH_CAL_MAGIC =
    (SCREEN_WIDTH == 320 && SCREEN_HEIGHT == 240)
        ? TOUCH_CAL_BASE_MAGIC
        : (TOUCH_CAL_BASE_MAGIC ^ ((static_cast<uint32_t>(SCREEN_WIDTH) << 16) |
                                   static_cast<uint32_t>(SCREEN_HEIGHT)));
constexpr uint8_t CMD_READ_X = 0xD0;
constexpr uint8_t CMD_READ_Y = 0x90;
constexpr uint8_t CMD_READ_Z1 = 0xB0;
constexpr uint8_t CMD_READ_Z2 = 0xC0;

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

/* Two ways to reach the XPT2046, chosen at build time by how the board wires
 * it.
 *
 * The E32R28T-1 gives the digitiser its own three pins, so bit-banging is both
 * simple and safe -- nothing else touches them.
 *
 * The 4-inch ST7796S board shares SCLK/MOSI/MISO with the LCD, and TFT_eSPI
 * owns those pins through the ESP32's hardware SPI peripheral. digitalWrite()
 * on a peripheral-routed pin does not reliably reach the pad, so the bit-bang
 * path reads a constant there: every calibration target returns the same raw
 * pair, the affine solve correctly rejects three identical points, and it
 * surfaces as "Calibration failed" with nothing pointing at the bus. Going
 * through TFT_eSPI's own touch support drives the digitiser over the
 * peripheral that already owns the lines, and serialises against panel
 * traffic for free. */
#ifdef TOUCH_SHARES_LCD_BUS
Board::RawTouch Board::readRawTouch() {
    RawTouch touch;
    uint16_t rx = 0;
    uint16_t ry = 0;
    const uint16_t pressure = tft_.getTouchRawZ();
    if (pressure < TOUCH_PRESSURE_THRESHOLD || !tft_.getTouchRaw(&rx, &ry)) {
        return touch;
    }
    touch.down = true;
    touch.x = static_cast<int16_t>(rx);
    touch.y = static_cast<int16_t>(ry);
    touch.pressure = pressure;
    return touch;
}
#else
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
#endif

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
    const Watchdog::Pause wdtPause;
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
        tft_.fillScreen(Ui::bg());
        tft_.setTextColor(Ui::text(), Ui::bg());
        tft_.setTextDatum(TC_DATUM);
        tft_.drawString("Touch Calibration", SCREEN_WIDTH / 2, 12, 4);
        tft_.drawString(String("Tap and hold the ") + labels[i], SCREEN_WIDTH / 2, 48, 2);
        drawCrosshair(tft_, screenPts[i][0], screenPts[i][1], Ui::warning());
        if (!waitForStableRaw(rawPts[i][0], rawPts[i][1])) {
            tft_.fillScreen(Ui::bg());
            tft_.setTextDatum(MC_DATUM);
            tft_.setTextColor(Ui::error(), Ui::bg());
            tft_.drawString("Calibration timed out", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 4);
            delay(1500);
            tft_.setTextDatum(TL_DATUM);
            return;
        }
        /* The raw pair for each target, because "Calibration failed" on its own
         * cannot distinguish a degenerate solve from a digitiser that is not
         * being read at all -- identical values across all three targets mean
         * the touch bus is returning a constant, i.e. wrong pins or a
         * different controller. */
        Serial.printf("[touchcal] target %u screen=(%d,%d) raw=(%d,%d)\n",
                      i, screenPts[i][0], screenPts[i][1],
                      rawPts[i][0], rawPts[i][1]);
        beepOk();
    }

    TouchCalibration next;
    if (computeAffine(rawPts, screenPts, next)) {
        cal_ = next;
        saveTouchCalibration();
        tft_.fillScreen(Ui::bg());
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(Ui::success(), Ui::bg());
        tft_.drawString("Calibration saved", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 4);
        delay(1200);
    } else {
        tft_.fillScreen(Ui::bg());
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(Ui::error(), Ui::bg());
        Serial.println("[touchcal] computeAffine rejected the points -- they are "
                       "collinear or identical, so no affine transform exists");
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
    int16_t lx = 0;
    int16_t ly = 0;
    next.down = mapTouch(raw, lx, ly);
    if (next.down) {
        switch (displayRotation_) {
            case 2:
                next.x = ly;
                next.y = static_cast<int16_t>(SCREEN_WIDTH - 1 - lx);
                break;
            case 0:
                next.x = static_cast<int16_t>(SCREEN_HEIGHT - 1 - ly);
                next.y = lx;
                break;
            case 3:
                next.x = static_cast<int16_t>(SCREEN_WIDTH - 1 - lx);
                next.y = static_cast<int16_t>(SCREEN_HEIGHT - 1 - ly);
                break;
            default:
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
