#include "Board.h"

#include <math.h>
#if GUME_TOUCH_CAPACITIVE
#include <Wire.h>
#endif
#include "Watchdog.h"
#include "ui/Ui.h"

namespace {
constexpr uint32_t TOUCH_CAL_MAGIC = 0x43594431UL;

/* FT6336U. Only the four registers this firmware needs; the part has many
 * more and none of them are wanted. TD_STATUS is followed directly by the
 * first touch point, so one 5-byte read gets the count and the coordinates
 * without a second transaction per frame. */
constexpr uint8_t FT_REG_TD_STATUS = 0x02;
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
    /* A capacitive controller reports pixels; there is nothing to fit, so it
     * is always "calibrated". This is not a convenience -- every caller that
     * sees false offers or forces the wizard, and on this panel that wizard
     * cannot be completed. Answering honestly here is what keeps a first boot
     * from being a screen the owner cannot get past. */
#if GUME_TOUCH_CAPACITIVE
    return true;
#else
    return cal_.magic == TOUCH_CAL_MAGIC;
#endif
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

#if !GUME_TOUCH_CAPACITIVE
uint16_t Board::readTouchAdc(uint8_t command) {
    uint16_t raw = 0;
    digitalWrite(BOARD.touch.cs, LOW);

    for (int8_t bit = 7; bit >= 0; --bit) {
        digitalWrite(BOARD.touch.mosi, (command & (1 << bit)) ? HIGH : LOW);
        digitalWrite(BOARD.touch.sclk, HIGH);
        delayMicroseconds(2);
        digitalWrite(BOARD.touch.sclk, LOW);
        delayMicroseconds(2);
    }

    for (uint8_t bit = 0; bit < 16; ++bit) {
        digitalWrite(BOARD.touch.sclk, HIGH);
        delayMicroseconds(2);
        raw = static_cast<uint16_t>((raw << 1) | (digitalRead(BOARD.touch.miso) ? 1 : 0));
        digitalWrite(BOARD.touch.sclk, LOW);
        delayMicroseconds(2);
    }

    digitalWrite(BOARD.touch.cs, HIGH);
    return static_cast<uint16_t>((raw >> 3) & 0x0FFF);
}

/* Read one point from an I2C capacitive controller.
 *
 * Returns coordinates in the panel's NATIVE portrait frame, which is what the
 * chip reports and all it knows -- it has never heard of setRotation(). The
 * rotation into screen space happens in mapTouch(), deliberately in the same
 * place the resistive path applies its affine fit, so pollTouch() below sees
 * one kind of coordinate whatever the board wires.
 *
 * `pressure` is synthesised: capacitive contact is binary, and every caller
 * that reads a pressure only ever compares it against a threshold. Reporting
 * a constant above every threshold is honest about that, where reporting 0
 * would make a real press look like noise. */
#endif  /* !GUME_TOUCH_CAPACITIVE -- end of the XPT2046 sampling half */

#if GUME_TOUCH_CAPACITIVE
Board::RawTouch Board::readCapacitiveTouch() {
    RawTouch touch;
    uint8_t buf[5];

    Wire.beginTransmission(BOARD.touch.i2cAddress);
    Wire.write(FT_REG_TD_STATUS);
    if (Wire.endTransmission(false) != 0) {
        return touch;
    }
    if (Wire.requestFrom(static_cast<int>(BOARD.touch.i2cAddress), 5) != 5) {
        return touch;
    }
    for (uint8_t i = 0; i < 5; ++i) {
        buf[i] = Wire.read();
    }

    if ((buf[0] & 0x0F) == 0) {
        return touch;
    }

    touch.down = true;
    touch.x = static_cast<int16_t>(((buf[1] & 0x0F) << 8) | buf[2]);
    touch.y = static_cast<int16_t>(((buf[3] & 0x0F) << 8) | buf[4]);
    touch.pressure = 0xFFFF;
    return touch;
}
#endif

#if !GUME_TOUCH_CAPACITIVE
Board::RawTouch Board::readResistiveTouch() {
    RawTouch touch;
    const uint16_t z1 = readTouchAdc(CMD_READ_Z1);
    const uint16_t z2 = readTouchAdc(CMD_READ_Z2);
    uint16_t pressure = 0;
    if (z1 > 0 && z2 > z1) {
        pressure = static_cast<uint16_t>(z1 + 4095 - z2);
    }

    const bool irqDown = digitalRead(BOARD.touch.irq) == LOW;
    if (!irqDown && pressure < BOARD.touch.pressureThreshold) {
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

#endif  /* !GUME_TOUCH_CAPACITIVE -- end of the XPT2046 half */

Board::RawTouch Board::readRawTouch() {
#if GUME_TOUCH_CAPACITIVE
    return readCapacitiveTouch();
#else
    return readResistiveTouch();
#endif
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
    /* Reachable from Settings as well as from boot, so refusing here rather
     * than only at the call sites is what makes it safe. */
#if GUME_TOUCH_CAPACITIVE
    return;
#else
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
        tft_.drawString("Calibration failed", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 4);
        delay(1500);
    }
    tft_.setTextDatum(TL_DATUM);

    tft_.setRotation(savedRotation);
    displayRotation_ = savedRotation;
#endif
}

bool Board::mapTouch(const RawTouch& raw, int16_t& x, int16_t& y) const {
    if (!raw.down) {
        return false;
    }

    /* Capacitive: rotate the controller's native portrait coordinates into the
     * landscape frame the resistive calibration also produces, so pollTouch()'s
     * rotation switch below serves both without knowing which it has.
     *
     * The specific quarter-turn is a property of the panel, not a convention:
     * it depends on which physical corner the controller calls its origin.
     * This one was established on hardware rather than derived -- the FNK0104B
     * probe drew a crosshair through this exact transform and it tracked a
     * finger at all four rotations, with the opposite handedness available on
     * a key and demonstrably wrong. See src/s3_diag.cpp. */
#if GUME_TOUCH_CAPACITIVE
    {
        const int16_t lx = raw.y;
        const int16_t ly = static_cast<int16_t>(BOARD.panel.nativeWidth - 1 - raw.x);
        x = constrain(lx, static_cast<int16_t>(0),
                      static_cast<int16_t>(SCREEN_WIDTH - 1));
        y = constrain(ly, static_cast<int16_t>(0),
                      static_cast<int16_t>(SCREEN_HEIGHT - 1));
        return true;
    }
#else
    if (cal_.magic != TOUCH_CAL_MAGIC) {
        return false;
    }
    const float mappedX = cal_.ax * raw.x + cal_.bx * raw.y + cal_.cx;
    const float mappedY = cal_.ay * raw.x + cal_.by * raw.y + cal_.cy;
    x = constrain(static_cast<int16_t>(lroundf(mappedX)), 0, SCREEN_WIDTH - 1);
    y = constrain(static_cast<int16_t>(lroundf(mappedY)), 0, SCREEN_HEIGHT - 1);
    return true;
#endif
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
