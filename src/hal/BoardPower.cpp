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

/* Gauge filter: the gauge must ignore load transients over seconds. A ~40s time constant at 2s sample rate (alpha ~0.05)
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
 * Everything the filters carry -- gaugeFilteredV_ and displayPct_ -- is touched by this task alone once begin() has returned.
 * Readers see only batteryPublished_, swapped under a spinlock, so a snapshot
 * is always one consistent sample rather than a voltage from one and a
 * percentage from the next. */
void Board::sampleBattery() {
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

/* Called only on a fresh sample (every BATTERY_SAMPLE_MS) to smooth the voltage
 * that feeds the battery percentage display. Slow on purpose, to ignore
 * transients. Prime on first sample rather than converging
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
 * The band is applied to the percentage, not to the measurement: the voltage
 * and the gauge filter still read the filtered value
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

bool Board::isBatteryLow() {
    const int8_t pct = getBatteryPercent();
    return pct >= 0 && pct <= BATTERY_LOW_PERCENT;
}

bool Board::isBatteryCritical() {
    const int8_t pct = getBatteryPercent();
    return pct >= 0 && pct <= BATTERY_CRITICAL_PERCENT;
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
    /* Not while the panel sleeps: that would light the backlight over a dark
     * panel. Unreachable from the Settings slider -- nobody can touch it with
     * the screen off -- but the serial console can set brightness at any
     * time, and displayWake() re-applies the stored value anyway. */
    if (!displayAsleep_) applyBrightness();
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
    tft_.writecommand(0x11);        // Sleep Out
    delay(PANEL_SLEEP_SETTLE_MS);
    /* Display ON, and it is not redundant.
     *
     * Sleep Out alone is enough on the ILI9341, which is the panel this
     * function was written against and the only one it was tested on. It is
     * NOT enough on the ST7796 (the 4-inch board): that controller comes out
     * of sleep with the display output still disabled, so the firmware wakes
     * correctly -- touch is read, the wake path runs, the log prints a normal
     * 120ms panel delay -- and the screen stays black. To an owner that is
     * indistinguishable from a dead device, and the only way out is the reset
     * button.
     *
     * Measured on an E32R40T: two sleep/wake cycles both logged
     * "[display] wake ... panel delay 120ms" with nothing on the glass.
     *
     * Sent unconditionally rather than behind a board test. On a panel that is
     * already displaying, Display ON is a no-op, so the cost is one byte on
     * the SPI bus once per wake; a per-board branch here would be one more
     * thing for the next panel to get wrong, and this is exactly the kind of
     * difference that produces a healthy log and a dark screen. */
    tft_.writecommand(0x29);
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
