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

/* Gauge filter: separate from charge inference. Charge detection needs to
 * notice a cable within ~2s (fast), while the gauge must ignore load transients
 * over seconds (slow). A ~40s time constant at 2s sample rate (alpha ~0.05)
 * ignores SPI bursts and backlight steps but still follows real discharge. */
constexpr float GAUGE_SMOOTH_ALPHA = 0.05f;    // low-pass on displayed percentage

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

/* ---- Sampling, on a task of its own -------------------------------------
 *
 * This used to be lazy: the first caller after the 2s cache expired paid for
 * the conversion. That caller was almost always Ui::drawTopBar(), so eight
 * analogRead()s and both filters ran inside a frame, on the render path, and
 * the filters advanced on whatever cadence the UI happened to ask on rather
 * than on a clock. Caching made it cheap on average and unpredictable in the
 * particular frame that paid.
 *
 * It belongs on a timer, so it is on one. Watchdog's monitor task is the
 * precedent: priority 1 on core 0, out of the way of the Arduino loop task on
 * core 1. The loop now only ever reads a settled snapshot, and cannot be the
 * thing that makes the gauge advance.
 *
 * Everything the filters carry -- chargeState_, chargeSmoothV_, gaugeFilteredV_,
 * displayPct_ -- is touched by this task alone once begin() has returned.
 * Readers see only batteryPublished_, swapped under a spinlock, so a snapshot
 * is always one consistent sample rather than a voltage from one and a
 * percentage from the next. */
void Board::sampleBattery() {
    const uint32_t now = millis();

    /* No sense line means no reading. A zeroed sample reads as implausible
     * downstream, which is already how the gauge says "I cannot see this". */
    if (!BOARD.hasBatterySense()) {
        portENTER_CRITICAL(&batteryMux_);
        batteryPublished_ = BatteryPublic{};
        portEXIT_CRITICAL(&batteryMux_);
        return;
    }

    if (!s_adcCharacterised) {
        analogSetPinAttenuation(BOARD.battery.adcPin, ADC_11db);
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12,
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

    updateChargeState(sample.batteryVoltage, now);
    updateGaugeFilter(sample.batteryVoltage);

    /* The deadband is applied here, once per sample, rather than in the getter.
     * The getter is called several times per frame and this has memory in it;
     * advancing it on a reader's cadence is the mistake this whole task exists
     * to stop making. */
    const float vBat = gaugeFilteredV_;
    if (vBat < V_IMPLAUSIBLE || vBat > V_SENSOR_MAX) {
        displayPct_ = -1;   // a fault is reported at once, never held back
    } else {
        const int8_t raw = curvePercent(vBat);
        const int8_t drift = static_cast<int8_t>(raw > displayPct_ ? raw - displayPct_
                                                                   : displayPct_ - raw);
        if (displayPct_ < 0 || raw >= 100 || raw <= 0 || drift >= PERCENT_DEADBAND) {
            displayPct_ = raw;
        }
    }

    portENTER_CRITICAL(&batteryMux_);
    batteryPublished_.sample = sample;
    batteryPublished_.state = chargeState_;
    batteryPublished_.pct = displayPct_;
    portEXIT_CRITICAL(&batteryMux_);
}

Board::BatteryPublic Board::batterySnapshot() {
    portENTER_CRITICAL(&batteryMux_);
    const BatteryPublic copy = batteryPublished_;
    portEXIT_CRITICAL(&batteryMux_);
    return copy;
}

void Board::batteryTask(void* arg) {
    Board* self = static_cast<Board*>(arg);
    for (;;) {
        self->sampleBattery();
        vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_MS));
    }
}

void Board::beginBatteryMonitor() {
    if (batteryTaskHandle_ != nullptr) {
        return;
    }
    /* One synchronous sample before the task exists, so the first screen drawn
     * has a real reading rather than the -1 that means "sensor fault". Priming
     * the gauge filter here is also what stops the badge ramping up from zero
     * for the first half-minute after every boot. */
    sampleBattery();

    if (!BOARD.hasBatterySense()) {
        return;   // nothing to poll; the zeroed snapshot above is the answer
    }
    xTaskCreatePinnedToCore(batteryTask, "battery", 2560, this, 1,
                            &batteryTaskHandle_, 0);
}

Board::BatteryTelemetry Board::readBatteryTelemetry() {
    return batterySnapshot().sample;
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
        } else if (chargeState_ == ChargingState::FULL ||
                   chargeState_ == ChargingState::CHARGING) {
            /* Flat voltage, low level, and we thought we were charging: the
             * charger must have been unplugged. Exit to DISCHARGING. This
             * handles the case where USB is removed but voltage drops gradually
             * (below V_CHARGER_HELD) without a large step, so the state must
             * transition back based on the low level after a flat window. */
            if (chargeSmoothV_ < CHARGE_FULL_V) {
                chargeState_ = ChargingState::DISCHARGING;
            }
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

/* Called only on a fresh sample (every BATTERY_SAMPLE_MS) to smooth the voltage
 * that feeds the battery percentage display. Separate from charge inference:
 * charge detection must be fast (~2s to notice a cable), while the gauge must
 * be slow to ignore transients. Prime on first sample rather than converging
 * from zero, so the gauge does not ramp for 30s after every boot. */
void Board::updateGaugeFilter(float volts) {
    if (!gaugeFilterReady_) {
        gaugeFilteredV_ = volts;
        gaugeFilterReady_ = true;
        return;
    }
    gaugeFilteredV_ += (volts - gaugeFilteredV_) * GAUGE_SMOOTH_ALPHA;
}

float Board::getBatteryVoltage() {
    return readBatteryTelemetry().batteryVoltage;
}

Board::PowerState Board::getPowerSource() {
    const BatteryPublic snap = batterySnapshot();
    const float vBat = snap.sample.batteryVoltage;
    if (vBat >= V_IMPLAUSIBLE && vBat <= V_SENSOR_MAX) {
        /* The cell voltage is all there is to go on, and it sits in the same
         * range whether the cable is in, out, or there is no pack at all -- so
         * the charge verdict is the only thing that tells them apart. There is
         * deliberately no "no pack" answer here; see V_SENSOR_MAX above. */
        if (snap.state == ChargingState::CHARGING ||
            snap.state == ChargingState::FULL) {
            return PowerState::EXTERNAL_POWER;
        }
        return PowerState::BATTERY;
    }
    return PowerState::UNKNOWN;
}

/* The curve mapping alone, with no memory in it. getBatteryPercent() is what
 * the screens read, and it adds the deadband described there. */
int8_t Board::curvePercent(float vBat) const {
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

/* The displayed charge, with a deadband, because one percent is smaller than
 * this hardware can actually resolve.
 *
 * Mid-discharge the curve above spends 10 percentage points on 20mV -- 2mV per
 * percent. The divider halves the cell before the ADC sees it and one count at
 * 11dB is a bit over a millivolt, so a single count of noise is worth most of a
 * percentage point, and the plateau is where the pack sits for most of its
 * life. The gauge filter (~40s) kills the fast noise but cannot stop a value
 * resting on a boundary from crossing it, so the reading flapped between two
 * neighbouring percentages indefinitely. With the BLE beacon advertising, its
 * supply ripple made that continuous.
 *
 * Every crossing invalidated the header, and until the chrome repaint landed
 * that meant wiping the whole panel -- so the console visibly flashed every
 * couple of seconds. The repaint is cheap now; this stops the number itself
 * from twitching, which is the other half of the same complaint.
 *
 * The band is applied to the percentage, not to the measurement: the voltage,
 * the charge verdict and the gauge filter all still read the filtered value
 * directly. A real discharge still tracks, in steps of PERCENT_DEADBAND rather
 * than one at a time.
 *
 * isBatteryLow() and isBatteryCritical() do read through this, so a warning
 * can arrive a percent later than it once would. That is the right way round:
 * they were previously free to flap on and off while the reading sat on their
 * threshold, and a low-battery banner that appears and vanishes every two
 * seconds is worse than one that arrives a moment late.
 *
 * Both endpoints are exempt. "100%" on the charger and "0%" about to die are
 * the two readings a person acts on, and a deadband that could leave the badge
 * showing 99 on a full pack would be trading a true number for a still one. */
int8_t Board::getBatteryPercent() {
    return batterySnapshot().pct;   // -1 means sensor fault, NOT "no pack"
}

Board::ChargingState Board::getChargingState() {
    return batterySnapshot().state;
}

/* One snapshot for both reads: taking the percentage from one sample and the
 * charge verdict from the next could report a low battery that the same
 * snapshot says is on the charger. */
bool Board::isBatteryLow() {
    const BatteryPublic snap = batterySnapshot();
    if (snap.pct < 0) return false;
    if (snap.state == ChargingState::CHARGING ||
        snap.state == ChargingState::FULL) {
        return false;
    }
    return snap.pct <= BATTERY_LOW_PERCENT;
}

bool Board::isBatteryCritical() {
    const BatteryPublic snap = batterySnapshot();
    if (snap.pct < 0) return false;
    if (snap.state == ChargingState::CHARGING ||
        snap.state == ChargingState::FULL) {
        return false;
    }
    return snap.pct <= BATTERY_CRITICAL_PERCENT;
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
