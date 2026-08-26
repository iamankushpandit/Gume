#include "Board.h"

#include <esp_adc_cal.h>

namespace {
esp_adc_cal_characteristics_t s_adcChars;
bool s_adcCharacterised = false;

constexpr uint32_t ADC_DEFAULT_VREF_MV = 1100;
constexpr uint8_t BATTERY_SAMPLES = 8;
/* The divider and the ADC's fault ceiling are properties of the board, so they
 * come from its profile rather than being restated here. Everything below them
 * is a property of a lithium cell, and is the same on any board. */
constexpr float DIVIDER_RATIO = BOARD.battery.dividerRatio;
/* ---- Why there is no "is a pack fitted?" test -------------------------
 * There is not one, and on this board there cannot be. Measured 2026-08-24
 * with the batdiag wizard, three power states, 8s averaged:
 *
 *     pack + USB      4.224 V
 *     USB, NO pack    4.159 V   <-- sits BETWEEN the other two
 *     pack only       4.066 V
 *
 * The TP4054 holds its BAT output at the float voltage whether or not a cell
 * is attached, so "USB with no pack" lands inside the range a real pack
 * occupies. No threshold separates the two in either direction, so do not try
 * to retune one; this is measured, not inferred.
 *
 * The old V_NO_BATTERY = 4.35f was above anything this board produces, so it
 * never fired: isBatteryPresent() was a constant true, getPowerSource()'s
 * no-pack branch was dead, getBatteryPercent() never returned -1, and the
 * blank-digit rendering in Ui::drawBatteryBadge was unreachable.
 *
 * V_SENSOR_MAX is a sanity ceiling on the ADC, NOT a pack test. Above it the
 * divider or the ADC is faulty and the gauge blanks its digits to say so.
 * With no pack fitted the gauge reads HIGH -- about 4.16 V, near full -- and
 * that is the honest description of what this hardware can see.
 */
constexpr float V_SENSOR_MAX = BOARD.battery.sensorMaxVolts;
constexpr float V_IMPLAUSIBLE = 3.0f;

struct CurvePoint { float volts; uint8_t pct; };
constexpr CurvePoint LIPO_CURVE[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.93f, 70}, {3.87f, 60},
    {3.82f, 50},  {3.79f, 40}, {3.77f, 30}, {3.74f, 20}, {3.68f, 10},
    {3.55f, 5},   {3.20f, 0},
};
constexpr uint8_t LIPO_CURVE_COUNT = sizeof(LIPO_CURVE) / sizeof(LIPO_CURVE[0]);

/* ---- Charge inference ------------------------------------------------
 * There is no CHRG line on this board, so "is it charging?" has to come out
 * of the cell voltage alone. Three signals, cheapest first:
 *
 * 1. A step between two consecutive samples. Attaching USB pulls the terminal
 *    voltage up within a second or two, and unplugging drops it back under
 *    load; both are far larger than ADC noise, which is why this is the signal
 *    that makes the icon respond to a cable within ~2s.
 * 2. A voltage no resting cell reaches. A pack off the cable was measured at
 *    4.066V, while the charger holds the rail up to 4.224-4.238V, so anything
 *    above V_CHARGER_HELD is a charger holding it there. The old 4.24f sat
 *    above the measured maximum and so never fired at all.
 * 3. The slow trend, for everything in between. Mid-discharge a LiPo sits on a
 *    plateau where 40% of the capacity spans 20mV, so the window has to be
 *    long enough that real movement clears the noise -- and when the window is
 *    genuinely flat the previous verdict stands rather than flapping.
 */
constexpr float CHARGE_STEP_V = 0.060f;        // plug/unplug, sample to sample
constexpr float V_CHARGER_HELD = 4.21f;        // above a resting cell, below
                                               // the 4.238V measured on charge
constexpr float CHARGE_FULL_V = 4.13f;         // charged, once charging is known
constexpr uint32_t CHARGE_WINDOW_MS = 45000;   // slow-trend window
constexpr float CHARGE_TREND_V = 0.012f;       // movement that clears ADC noise
constexpr float CHARGE_SMOOTH_ALPHA = 0.30f;   // low-pass on the trend input

constexpr uint8_t BL_CHANNEL = 4;
constexpr uint32_t BL_PWM_HZ = 5000;
constexpr uint8_t BL_PWM_BITS = 8;
bool blReady = false;

/* A backlight wired active-low is off at full duty, so the duty is inverted
 * rather than the pin, which would defeat the PWM entirely. */
uint32_t backlightDuty(uint8_t percent) {
    const uint32_t duty = (static_cast<uint32_t>(percent) * 255) / 100;
    return BOARD.panel.backlightActiveHigh ? duty : 255 - duty;
}
}

Board::BatteryTelemetry Board::readBatteryTelemetry() {
    const uint32_t now = millis();
    if (batterySampled_ && now - batterySampleMs_ < BATTERY_SAMPLE_MS) {
        return batterySample_;
    }

    /* No sense line means no reading. A zeroed sample reads as implausible
     * downstream, which is already how the gauge says "I cannot see this". */
    if (!BOARD.hasBatterySense()) {
        batterySample_ = BatteryTelemetry{};
        batterySampleMs_ = now;
        batterySampled_ = true;
        return batterySample_;
    }

    if (!s_adcCharacterised) {
        analogSetPinAttenuation(BOARD.battery.adcPin, ADC_11db);
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                                 ADC_DEFAULT_VREF_MV, &s_adcChars);
        s_adcCharacterised = true;
    }

    uint32_t accumulator = 0;
    for (uint8_t i = 0; i < BATTERY_SAMPLES; ++i) {
        accumulator += static_cast<uint32_t>(analogRead(BOARD.battery.adcPin));
    }

    BatteryTelemetry sample;
    sample.rawAdc = static_cast<uint16_t>(accumulator / BATTERY_SAMPLES);
    sample.adcVoltage = esp_adc_cal_raw_to_voltage(sample.rawAdc, &s_adcChars) / 1000.0f;
    sample.batteryVoltage = sample.adcVoltage * DIVIDER_RATIO;

    batterySample_ = sample;
    batterySampleMs_ = now;
    batterySampled_ = true;
    updateChargeState(sample.batteryVoltage, now);
    return sample;
}

/* Called only on a fresh sample, so "the previous sample" is BATTERY_SAMPLE_MS
 * ago regardless of how often the render path asked for telemetry. */
void Board::updateChargeState(float volts, uint32_t nowMs) {
    if (volts < V_IMPLAUSIBLE || volts > V_SENSOR_MAX) {
        // Sensor out of range: there is no trend worth reading.
        chargeState_ = ChargingState::UNKNOWN;
        chargeTracking_ = false;
        return;
    }

    if (!chargeTracking_) {
        chargeLastV_ = volts;
        chargeSmoothV_ = volts;
        chargeRefV_ = volts;
        chargeRefMs_ = nowMs;
        chargeTracking_ = true;
        return;   // UNKNOWN until there is something to compare against
    }

    const float step = volts - chargeLastV_;
    chargeLastV_ = volts;
    chargeSmoothV_ += (volts - chargeSmoothV_) * CHARGE_SMOOTH_ALPHA;

    if (step <= -CHARGE_STEP_V) {
        /* Tested before the held-high level on purpose: a cell that is still
         * above V_CHARGER_HELD while falling is coming down, not charging. */
        chargeState_ = ChargingState::DISCHARGING;
    } else if (step >= CHARGE_STEP_V || volts >= V_CHARGER_HELD) {
        chargeState_ = ChargingState::CHARGING;
    } else if (nowMs - chargeRefMs_ < CHARGE_WINDOW_MS) {
        return;   // window still open: no new evidence, keep the last verdict
    } else {
        const float trend = chargeSmoothV_ - chargeRefV_;
        if (trend >= CHARGE_TREND_V) {
            chargeState_ = ChargingState::CHARGING;
        } else if (trend <= -CHARGE_TREND_V) {
            chargeState_ = ChargingState::DISCHARGING;
        } else if (chargeState_ == ChargingState::CHARGING &&
                   chargeSmoothV_ >= CHARGE_FULL_V) {
            /* Flat and high, having been charging: the charger has tapered
             * off. A pack resting at the same voltage off the cable reads the
             * same, which is why this is only reachable from CHARGING. */
            chargeState_ = ChargingState::FULL;
        } else if (chargeState_ == ChargingState::UNKNOWN &&
                   chargeSmoothV_ < CHARGE_FULL_V) {
            /* Flat, low, and nothing has said charger: it is running on cell.
             * The level test is what makes this "low" -- without it a board
             * booted on USB and sitting flat at float voltage fell through
             * here and reported DISCHARGING after CHARGE_WINDOW_MS. */
            chargeState_ = ChargingState::DISCHARGING;
        }
    }

    chargeRefV_ = chargeSmoothV_;
    chargeRefMs_ = nowMs;
}

float Board::getBatteryVoltage() {
    return readBatteryTelemetry().batteryVoltage;
}

Board::PowerState Board::getPowerSource() {
    const float vBat = readBatteryTelemetry().batteryVoltage;
    if (vBat >= V_IMPLAUSIBLE && vBat <= V_SENSOR_MAX) {
        /* The cell voltage is all there is to go on, and it sits in the same
         * range whether the cable is in, out, or there is no pack at all -- so
         * the charge verdict is the only thing that tells them apart. There is
         * deliberately no "no pack" answer here; see V_SENSOR_MAX above. */
        if (chargeState_ == ChargingState::CHARGING ||
            chargeState_ == ChargingState::FULL) {
            return PowerState::EXTERNAL_POWER;
        }
        return PowerState::BATTERY;
    }
    return PowerState::UNKNOWN;
}

int8_t Board::getBatteryPercent() {
    const float vBat = readBatteryTelemetry().batteryVoltage;
    if (vBat < V_IMPLAUSIBLE || vBat > V_SENSOR_MAX) {
        return -1;   // sensor fault -- NOT "no pack", which is undetectable
    }
    if (vBat >= LIPO_CURVE[0].volts) {
        return 100;
    }
    for (uint8_t i = 1; i < LIPO_CURVE_COUNT; ++i) {
        const CurvePoint& hi = LIPO_CURVE[i - 1];
        const CurvePoint& lo = LIPO_CURVE[i];
        if (vBat >= lo.volts) {
            const float span = hi.volts - lo.volts;
            const float frac = span > 0.0f ? (vBat - lo.volts) / span : 0.0f;
            return static_cast<int8_t>(lo.pct + frac * (hi.pct - lo.pct) + 0.5f);
        }
    }
    return 0;
}

Board::ChargingState Board::getChargingState() {
    readBatteryTelemetry();   // keeps the verdict advancing off the render path
    return chargeState_;
}

bool Board::isBatteryLow() {
    const int8_t pct = getBatteryPercent();
    if (pct < 0) return false;
    if (chargeState_ == ChargingState::CHARGING ||
        chargeState_ == ChargingState::FULL) {
        return false;
    }
    return pct <= BATTERY_LOW_PERCENT;
}

bool Board::isBatteryCritical() {
    const int8_t pct = getBatteryPercent();
    if (pct < 0) return false;
    if (chargeState_ == ChargingState::CHARGING ||
        chargeState_ == ChargingState::FULL) {
        return false;
    }
    return pct <= BATTERY_CRITICAL_PERCENT;
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
    if (!BOARD.hasBacklightControl()) return;
    if (!blReady) {
        ledcSetup(BL_CHANNEL, BL_PWM_HZ, BL_PWM_BITS);
        ledcAttachPin(BOARD.panel.backlightPin, BL_CHANNEL);
        blReady = true;
    }
    ledcWrite(BL_CHANNEL, backlightDuty(brightness()));
}

void Board::displaySleep() {
    if (displayAsleep_) return;
    if (BOARD.hasBacklightControl()) ledcWrite(BL_CHANNEL, backlightDuty(0));
    tft_.writecommand(0x10);
    displayAsleep_ = true;
    displaySleepTelemetry_.sleepCount++;
    displaySleepTelemetry_.lastSleepMs = millis();
    Serial.printf("[display] sleep #%lu at %lums\n",
                  static_cast<unsigned long>(displaySleepTelemetry_.sleepCount),
                  static_cast<unsigned long>(displaySleepTelemetry_.lastSleepMs));
}

void Board::displayWake() {
    if (!displayAsleep_) return;
    const uint32_t wakeStartMs = millis();
    /* The ILI9341 ignores Sleep Out inside 120ms of a Sleep In, and a touch
     * arriving that fast is reachable: the saver can hand over to sleep and the
     * player's next tap lands milliseconds later. Wait out only the remainder --
     * after a real sleep this is already long past and costs nothing. */
    const uint32_t sinceSleep = wakeStartMs - displaySleepTelemetry_.lastSleepMs;
    if (sinceSleep < PANEL_SLEEP_SETTLE_MS) {
        delay(PANEL_SLEEP_SETTLE_MS - sinceSleep);
    }
    tft_.writecommand(0x11);
    delay(PANEL_SLEEP_SETTLE_MS);
    applyBrightness();
    displayAsleep_ = false;
    const uint32_t wakeEndMs = millis();
    displaySleepTelemetry_.wakeCount++;
    displaySleepTelemetry_.lastWakeMs = wakeEndMs;
    displaySleepTelemetry_.lastWakeDelayMs = wakeEndMs - wakeStartMs;
    displaySleepTelemetry_.lastSleepDurationMs =
        displaySleepTelemetry_.lastSleepMs > 0 ? wakeEndMs - displaySleepTelemetry_.lastSleepMs : 0;
    Serial.printf("[display] wake #%lu at %lums after %lums, panel delay %lums\n",
                  static_cast<unsigned long>(displaySleepTelemetry_.wakeCount),
                  static_cast<unsigned long>(displaySleepTelemetry_.lastWakeMs),
                  static_cast<unsigned long>(displaySleepTelemetry_.lastSleepDurationMs),
                  static_cast<unsigned long>(displaySleepTelemetry_.lastWakeDelayMs));
}
