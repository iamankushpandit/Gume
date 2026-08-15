#include "Board.h"

#include <esp_adc_cal.h>

namespace {
esp_adc_cal_characteristics_t s_adcChars;
bool s_adcCharacterised = false;

constexpr uint32_t ADC_DEFAULT_VREF_MV = 1100;
constexpr uint8_t BATTERY_SAMPLES = 8;
constexpr float DIVIDER_RATIO = 2.0f;
constexpr float V_NO_BATTERY = 4.35f;
constexpr float V_IMPLAUSIBLE = 3.0f;

struct CurvePoint { float volts; uint8_t pct; };
constexpr CurvePoint LIPO_CURVE[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.93f, 70}, {3.87f, 60},
    {3.82f, 50},  {3.79f, 40}, {3.77f, 30}, {3.74f, 20}, {3.68f, 10},
    {3.55f, 5},   {3.20f, 0},
};
constexpr uint8_t LIPO_CURVE_COUNT = sizeof(LIPO_CURVE) / sizeof(LIPO_CURVE[0]);

constexpr uint8_t BL_CHANNEL = 4;
constexpr uint32_t BL_PWM_HZ = 5000;
constexpr uint8_t BL_PWM_BITS = 8;
bool blReady = false;
}

Board::BatteryTelemetry Board::readBatteryTelemetry() {
    const uint32_t now = millis();
    if (batterySampled_ && now - batterySampleMs_ < BATTERY_SAMPLE_MS) {
        return batterySample_;
    }

    if (!s_adcCharacterised) {
        analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                                 ADC_DEFAULT_VREF_MV, &s_adcChars);
        s_adcCharacterised = true;
    }

    uint32_t accumulator = 0;
    for (uint8_t i = 0; i < BATTERY_SAMPLES; ++i) {
        accumulator += static_cast<uint32_t>(analogRead(PIN_BAT_ADC));
    }

    BatteryTelemetry sample;
    sample.rawAdc = static_cast<uint16_t>(accumulator / BATTERY_SAMPLES);
    sample.adcVoltage = esp_adc_cal_raw_to_voltage(sample.rawAdc, &s_adcChars) / 1000.0f;
    sample.batteryVoltage = sample.adcVoltage * DIVIDER_RATIO;

    batterySample_ = sample;
    batterySampleMs_ = now;
    batterySampled_ = true;
    return sample;
}

float Board::getBatteryVoltage() {
    return readBatteryTelemetry().batteryVoltage;
}

Board::PowerState Board::getPowerSource() {
    const float vBat = readBatteryTelemetry().batteryVoltage;
    if (vBat > V_NO_BATTERY) {
        return PowerState::EXTERNAL_POWER;
    }
    if (vBat >= V_IMPLAUSIBLE) {
        return PowerState::BATTERY;
    }
    return PowerState::UNKNOWN;
}

bool Board::isBatteryPresent() {
    const float vBat = readBatteryTelemetry().batteryVoltage;
    return vBat >= V_IMPLAUSIBLE && vBat <= V_NO_BATTERY;
}

int8_t Board::getBatteryPercent() {
    const float vBat = readBatteryTelemetry().batteryVoltage;
    if (vBat < V_IMPLAUSIBLE || vBat > V_NO_BATTERY) {
        return -1;
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
    return ChargingState::UNKNOWN;
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
    ledcWrite(BL_CHANNEL, (brightness() * 255) / 100);
}

void Board::displaySleep() {
    if (displayAsleep_) return;
    ledcWrite(BL_CHANNEL, 0);
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
    tft_.writecommand(0x11);
    delay(120);
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
