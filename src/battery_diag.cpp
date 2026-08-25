/* Standalone battery bring-up and calibration tool for the E32R28T-1.
 *
 * ------------------------------------------------------------------------
 * Why this is a separate firmware
 * ------------------------------------------------------------------------
 * The main app cannot answer the questions this board actually poses. It has
 * a frame budget, a watchdog, a 2s telemetry cache and a UI that must not
 * block -- all of which are the right decisions for a product and all of which
 * get in the way of "sit here for an hour and tell me exactly what the ADC is
 * doing". So this builds ALONE (`build_src_filter = +<battery_diag.cpp>`,
 * env:batdiag), exactly as wifi_diag.cpp does for the radio. No Board, no Ui,
 * no games, nothing else to blame for a reading.
 *
 * ------------------------------------------------------------------------
 * What the hardware actually provides (LCDWIKI E32R28T-1 user manual)
 * ------------------------------------------------------------------------
 *   CONFIRMED  BAT_ADC is IO34 (ADC1_CH6, input-only). ADC1 matters: ADC2 is
 *              unusable whenever Wi-Fi is associated.
 *   CONFIRMED  R2/R3 form a divider and "the obtained voltage multiplied by 2
 *              is the actual battery voltage". Ratio 2.0; resistances not
 *              published, which is what page CAL exists to check.
 *   CONFIRMED  Charger is a TP4054. R27 sets the current, 500mA maximum.
 *   CONFIRMED  Q3, a P-channel FET, DISCONNECTS the battery whenever USB is
 *              present -- "when powered through the Type-C interface ... the
 *              drain and source are cut off, and the battery supply is
 *              interrupted". So on USB the cell carries no system load, and
 *              the voltage read is a charge voltage, not a resting one.
 *   ABSENT     No CHRG / charge-status line reaches any GPIO.
 *   ABSENT     No VBUS / USB-presence line reaches any GPIO. The vendor pin
 *              table lists TYPE-C_POWER with connection "/".
 *
 * Charging therefore cannot be measured on this board, only inferred, and
 * this tool is built to show the inference working rather than to hide it.
 *
 * ------------------------------------------------------------------------
 * Using it
 * ------------------------------------------------------------------------
 *   pio run -e batdiag -t upload && pio device monitor
 *
 * BOOT (IO0) cycles pages. Over serial: 'n' next page, 'p' previous,
 * 'r' reset statistics, 'm' mark an event in the log, 'c' toggle CSV.
 *
 * CSV streams continuously on the serial port regardless of the page shown,
 * so a long discharge can be captured to a file and plotted:
 *   ms,raw,adc_mv,cell_mv,pct,state
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_adc_cal.h>

#include "BoardConfig.h"

// ---------------------------------------------------------------- tunables

namespace cfg {
/* Everything a calibration session might need to change lives here, so no
 * measurement ever requires editing logic. */
constexpr float DIVIDER_RATIO = 2.0f;      // vendor manual: "multiplied by 2"
constexpr float CAL_FACTOR = 1.0f;         // set from page CAL if the meter disagrees
constexpr uint32_t ADC_VREF_MV = 1100;     // fallback only; eFuse overrides
constexpr uint8_t BURST_SAMPLES = 32;      // per statistics burst
constexpr uint8_t TRIM = 8;                // discarded from each end of a burst
constexpr uint32_t FAST_SAMPLE_MS = 250;   // this tool's own cadence
constexpr uint32_t PROD_SAMPLE_MS = 2000;  // mirrors Board::BATTERY_SAMPLE_MS
constexpr float EMA_ALPHA = 0.25f;

/* Charge-inference constants, copied verbatim from BoardPower.cpp so what is
 * observed here is what the product will do. */
constexpr float CHARGE_STEP_V = 0.060f;
/* Was 4.24f, which is unreachable on this board and therefore dead code: the
 * highest cell voltage measured here across a full charge cycle was 4.238V,
 * so the "a charger is holding it up" signal could never fire. 4.21 sits in
 * the gap -- above a rested cell (which settles to ~4.15-4.18 within minutes
 * of coming off charge) and below the 4.22-4.238 the TP4054 actually holds.
 * The margin is only ~15mV each side, which is why the negative-step test
 * below now runs FIRST: a falling cell is never charging, whatever its
 * absolute voltage happens to be. */
constexpr float V_CHARGER_HELD = 4.21f;
constexpr float CHARGE_FULL_V = 4.13f;
constexpr uint32_t CHARGE_WINDOW_MS = 45000;
constexpr float CHARGE_TREND_V = 0.012f;
constexpr float CHARGE_SMOOTH_ALPHA = 0.30f;

/* UNVERIFIED. These two decide "is a pack fitted", and page NOBAT exists
 * because the reasoning behind V_NO_BATTERY looks wrong: with no cell the
 * TP4054 BAT pin should float near its 4.2V float voltage, which is BELOW
 * this threshold, so the no-battery case may never be detected at all. */
constexpr float V_NO_BATTERY = 4.35f;
constexpr float V_IMPLAUSIBLE = 3.00f;
}   // namespace cfg

// The production discharge curve, so page SOC reports the shipping answer.
struct CurvePoint { float volts; uint8_t pct; };
constexpr CurvePoint LIPO_CURVE[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.93f, 70}, {3.87f, 60},
    {3.82f, 50},  {3.79f, 40}, {3.77f, 30}, {3.74f, 20}, {3.68f, 10},
    {3.55f, 5},   {3.20f, 0},
};
constexpr uint8_t LIPO_COUNT = sizeof(LIPO_CURVE) / sizeof(LIPO_CURVE[0]);

// ------------------------------------------------------------------ colours

namespace col {
constexpr uint16_t BG      = 0x0843;
constexpr uint16_t BAR     = 0x10A6;
constexpr uint16_t TEXT    = 0xF7BE;
constexpr uint16_t MUTED   = 0xA534;
constexpr uint16_t OK      = 0x37F0;
constexpr uint16_t WARN    = 0xFFE6;
constexpr uint16_t ERR     = 0xF9EA;
constexpr uint16_t ACCENT  = 0x2439;
}

/* RGB LED, per the LCDWIKI pin table: LED_RED=IO22, LED_GREEN=IO16,
 * LED_BLUE=IO17, common anode, lit at LOW. NOTE: the main firmware currently
 * uses R=16/G=4/B=17, which disagrees -- IO4 is the audio-amp enable, not an
 * LED. This tool deliberately uses the documented mapping, so the wizard's
 * colours double as a live test of which table is right: if the "act now"
 * prompt glows RED here, the vendor table wins. */
constexpr uint8_t PIN_LED_RED = 22;
constexpr uint8_t PIN_LED_GREEN = 16;
constexpr uint8_t PIN_LED_BLUE = 17;

void led(bool r, bool g, bool b) {
    digitalWrite(PIN_LED_RED, r ? LOW : HIGH);
    digitalWrite(PIN_LED_GREEN, g ? LOW : HIGH);
    digitalWrite(PIN_LED_BLUE, b ? LOW : HIGH);
}

// -------------------------------------------------------------------- state

TFT_eSPI tft;
esp_adc_cal_characteristics_t adcChars;

enum Page : uint8_t {
    PAGE_WIZARD = 0, PAGE_LIVE, PAGE_NOISE, PAGE_FILTER, PAGE_SOC,
    PAGE_CHARGE, PAGE_NOBAT, PAGE_CAL, PAGE_LOG, PAGE_COUNT
};
const char* const PAGE_NAME[PAGE_COUNT] = {
    "WIZARD", "LIVE", "NOISE", "FILTERS", "SOC", "CHARGE", "NO-BATT", "CALIBRATE", "LOG"
};

uint8_t page = PAGE_WIZARD;
bool pageDirty = true;
bool csvOn = true;

// Latest burst
uint16_t rawMin, rawMax, rawMean, rawTrimmed, rawMedian;
float adcVolts, cellVolts;
float emaVolts = 0.0f;
bool emaSeeded = false;

// Session statistics
uint16_t sessRawMin = 0xFFFF, sessRawMax = 0;
float sessCellMin = 99.0f, sessCellMax = 0.0f;
uint32_t sampleCount = 0;
uint32_t startedMs = 0;

// Charge inference (mirrors BoardPower.cpp)
enum ChargeState : uint8_t { CS_UNKNOWN, CS_CHARGING, CS_FULL, CS_DISCHARGING };
const char* const CS_NAME[] = {"UNKNOWN", "CHARGING", "FULL", "DISCHARGING"};
ChargeState chargeState = CS_UNKNOWN;
float chargeLastV = 0, chargeSmoothV = 0, chargeRefV = 0, lastStepV = 0;
uint32_t chargeRefMs = 0;
bool chargeTracking = false;
uint32_t lastProdSampleMs = 0;

// Event log
struct Event { uint32_t ms; char text[34]; };
constexpr uint8_t EVENT_CAP = 8;
Event events[EVENT_CAP];
uint8_t eventCount = 0;

// No-battery experiment (legacy manual page)
float nobatWithPack = 0.0f, nobatWithout = 0.0f;

/* ---- Guided power-state wizard ---------------------------------------
 * Three power states, measured in the only order that keeps the board alive
 * throughout: it must never be without BOTH the pack and the cable at once.
 *
 *   A  pack + USB     the normal charging condition
 *   B  USB only       pack removed -- what a floating TP4054 BAT pin reads
 *   C  pack only      cable removed -- a real cell under real load
 *
 * B is the one that cannot be obtained any other way, and A vs B vs C is what
 * decides whether "is a battery fitted" is answerable at all on this board.
 *
 * Stage C runs with USB out, so there is no serial port: every result has to
 * be legible on the panel. That is why this ends on a summary screen rather
 * than a log line. */
enum WizStage : uint8_t {
    WZ_INTRO, WZ_MEASURE_A, WZ_PROMPT_B, WZ_MEASURE_B,
    WZ_PROMPT_C, WZ_MEASURE_C, WZ_DONE
};
const uint32_t WZ_MS[] = {4000, 8000, 12000, 8000, 15000, 8000, 0};

WizStage wizStage = WZ_INTRO;
uint32_t wizStageStartMs = 0;
WizStage wizLastDrawn = WZ_DONE;
double wizSum = 0.0;
uint16_t wizN = 0;
float wizMin = 99.0f, wizMax = 0.0f;
float wizA = 0.0f, wizB = 0.0f, wizC = 0.0f;
float wizAmin = 0, wizAmax = 0, wizBmin = 0, wizBmax = 0, wizCmin = 0, wizCmax = 0;

// ------------------------------------------------------------------ helpers

void logEvent(const char* fmt, ...) {
    Event e;
    e.ms = millis();
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.text, sizeof(e.text), fmt, ap);
    va_end(ap);
    // Newest first; the oldest falls off the end.
    for (int8_t i = EVENT_CAP - 1; i > 0; --i) events[i] = events[i - 1];
    events[0] = e;
    if (eventCount < EVENT_CAP) ++eventCount;
    Serial.printf("# %8lu  %s\n", (unsigned long)e.ms, e.text);
}

int cmpU16(const void* a, const void* b) {
    const uint16_t x = *(const uint16_t*)a, y = *(const uint16_t*)b;
    return (x > y) - (x < y);
}

/* One burst: BURST_SAMPLES reads, sorted, so the page can show the raw spread
 * and a trimmed mean from the same data. Trimming rather than plain averaging
 * is the point -- a single ADC outlier moves a mean of 8 by more than the
 * whole voltage range this board needs to resolve. */
void sampleBurst() {
    uint16_t s[cfg::BURST_SAMPLES];
    uint32_t sum = 0;
    for (uint8_t i = 0; i < cfg::BURST_SAMPLES; ++i) {
        s[i] = (uint16_t)analogRead(PIN_BAT_ADC);
        sum += s[i];
    }
    qsort(s, cfg::BURST_SAMPLES, sizeof(uint16_t), cmpU16);

    rawMin = s[0];
    rawMax = s[cfg::BURST_SAMPLES - 1];
    rawMean = (uint16_t)(sum / cfg::BURST_SAMPLES);
    rawMedian = s[cfg::BURST_SAMPLES / 2];

    uint32_t tsum = 0;
    for (uint8_t i = cfg::TRIM; i < cfg::BURST_SAMPLES - cfg::TRIM; ++i) tsum += s[i];
    rawTrimmed = (uint16_t)(tsum / (cfg::BURST_SAMPLES - 2 * cfg::TRIM));

    adcVolts = esp_adc_cal_raw_to_voltage(rawTrimmed, &adcChars) / 1000.0f;
    cellVolts = adcVolts * cfg::DIVIDER_RATIO * cfg::CAL_FACTOR;

    if (!emaSeeded) { emaVolts = cellVolts; emaSeeded = true; }
    else emaVolts += (cellVolts - emaVolts) * cfg::EMA_ALPHA;

    if (rawMin < sessRawMin) sessRawMin = rawMin;
    if (rawMax > sessRawMax) sessRawMax = rawMax;
    if (cellVolts < sessCellMin) sessCellMin = cellVolts;
    if (cellVolts > sessCellMax) sessCellMax = cellVolts;
    ++sampleCount;
}

int8_t socFromCurve(float v) {
    if (v < cfg::V_IMPLAUSIBLE || v > cfg::V_NO_BATTERY) return -1;
    if (v >= LIPO_CURVE[0].volts) return 100;
    for (uint8_t i = 1; i < LIPO_COUNT; ++i) {
        const CurvePoint& hi = LIPO_CURVE[i - 1];
        const CurvePoint& lo = LIPO_CURVE[i];
        if (v >= lo.volts) {
            const float span = hi.volts - lo.volts;
            const float frac = span > 0 ? (v - lo.volts) / span : 0;
            return (int8_t)(lo.pct + frac * (hi.pct - lo.pct) + 0.5f);
        }
    }
    return 0;
}

uint8_t socLinear(float v) {
    const float p = (v - 3.20f) / (4.20f - 3.20f) * 100.0f;
    return (uint8_t)constrain(p, 0.0f, 100.0f);
}

/* The production inference, run on the production cadence so its timing is
 * observable. Kept structurally identical to Board::updateChargeState(). */
void updateChargeState(float volts, uint32_t nowMs) {
    if (volts < cfg::V_IMPLAUSIBLE || volts > cfg::V_NO_BATTERY) {
        if (chargeState != CS_UNKNOWN) logEvent("state -> UNKNOWN (%.3fV)", volts);
        chargeState = CS_UNKNOWN;
        chargeTracking = false;
        return;
    }
    if (!chargeTracking) {
        chargeLastV = chargeSmoothV = chargeRefV = volts;
        chargeRefMs = nowMs;
        chargeTracking = true;
        return;
    }

    const ChargeState before = chargeState;
    const float step = volts - chargeLastV;
    lastStepV = step;
    chargeLastV = volts;
    chargeSmoothV += (volts - chargeSmoothV) * cfg::CHARGE_SMOOTH_ALPHA;

    /* Order matters. The negative step is tested before the held-high level
     * because they can both be true for one sample just after the cable comes
     * out, while the cell is still above V_CHARGER_HELD but already falling
     * fast. Testing the level first would report CHARGING at the exact moment
     * the charger was removed. */
    if (step <= -cfg::CHARGE_STEP_V) {
        chargeState = CS_DISCHARGING;
        if (before != CS_DISCHARGING) logEvent("DISCHARGING: step %.0fmV", step * 1000.0f);
    } else if (step >= cfg::CHARGE_STEP_V || volts >= cfg::V_CHARGER_HELD) {
        chargeState = CS_CHARGING;
        if (before != CS_CHARGING) {
            logEvent(step >= cfg::CHARGE_STEP_V ? "CHARGING: step +%.0fmV" : "CHARGING: held %.2fV",
                     step >= cfg::CHARGE_STEP_V ? step * 1000.0f : volts);
        }
    } else if (nowMs - chargeRefMs < cfg::CHARGE_WINDOW_MS) {
        return;   // window open; previous verdict stands
    } else {
        const float trend = chargeSmoothV - chargeRefV;
        if (trend >= cfg::CHARGE_TREND_V) {
            chargeState = CS_CHARGING;
            if (before != CS_CHARGING) logEvent("CHARGING: trend +%.0fmV/45s", trend * 1000.0f);
        } else if (trend <= -cfg::CHARGE_TREND_V) {
            chargeState = CS_DISCHARGING;
            if (before != CS_DISCHARGING) logEvent("DISCHARGING: trend %.0fmV/45s", trend * 1000.0f);
        } else if (chargeState == CS_CHARGING && chargeSmoothV >= cfg::CHARGE_FULL_V) {
            chargeState = CS_FULL;
            logEvent("FULL: flat at %.2fV", chargeSmoothV);
        } else if (chargeState == CS_UNKNOWN && chargeSmoothV < cfg::CHARGE_FULL_V) {
            /* Flat, genuinely low, and nothing has said charger: running on
             * the cell. The `< CHARGE_FULL_V` guard is the fix for the bug
             * this tool caught -- the old branch claimed "flat and low" but
             * only ever tested flat, so a board that booted on USB with a
             * full pack announced DISCHARGING at 4.22V after 45s. Flat AND
             * high is a full pack that may or may not be on a cable, and
             * those two are indistinguishable from a single sample, so the
             * honest answer there is to stay UNKNOWN rather than guess. */
            chargeState = CS_DISCHARGING;
            logEvent("DISCHARGING: flat and low");
        }
        chargeRefV = chargeSmoothV;
        chargeRefMs = nowMs;
        return;
    }
    chargeRefV = chargeSmoothV;
    chargeRefMs = nowMs;
}

// ------------------------------------------------------------------ drawing

constexpr int16_t HDR_H = 26;
constexpr int16_t FTR_Y = 224;

void drawChrome() {
    tft.fillScreen(col::BG);
    tft.fillRect(0, 0, 320, HDR_H, col::BAR);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, col::BAR);
    char buf[40];
    snprintf(buf, sizeof(buf), "%u/%u  %s", page + 1, (unsigned)PAGE_COUNT, PAGE_NAME[page]);
    tft.drawString(buf, 6, HDR_H / 2, 2);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("BAT DIAG", 314, HDR_H / 2, 2);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(col::MUTED, col::BG);
    tft.drawString("BOOT=next   serial: n p r m c", 6, FTR_Y + 8, 1);
    tft.setTextDatum(TL_DATUM);
}

/* Fixed-width value fields, so a shorter new value cannot leave the tail of
 * the old one on screen and there is no full-page flicker. */
void field(int16_t y, const char* label, const char* value, uint16_t colour = col::TEXT) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(col::MUTED, col::BG);
    tft.drawString(label, 8, y, 2);
    tft.setTextColor(colour, col::BG);
    /* 20 chars is what actually fits: the column runs x=150..316, ~166px,
     * and font 2 averages 8px. Padding rather than clipping is what stops a
     * shorter new value leaving the tail of the old one behind. */
    char padded[26];
    snprintf(padded, sizeof(padded), "%-20s", value);
    tft.drawString(padded, 150, y, 2);
    tft.setTextDatum(TL_DATUM);
}

void bigVolts(int16_t y, float v, uint16_t colour) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.3f V ", v);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(colour, col::BG);
    tft.drawString(buf, 160, y, 6);
    tft.setTextDatum(TL_DATUM);
}

void pageLive() {
    char b[40];
    bigVolts(70, cellVolts, chargeState == CS_CHARGING ? col::OK : col::TEXT);
    snprintf(b, sizeof(b), "%u  (%u..%u)", rawTrimmed, rawMin, rawMax);
    field(112, "ADC raw", b);
    snprintf(b, sizeof(b), "%.4f V at pin", adcVolts);
    field(134, "ADC volts", b);
    snprintf(b, sizeof(b), "x %.2f  (cal %.3f)", cfg::DIVIDER_RATIO, cfg::CAL_FACTOR);
    field(156, "Divider", b);
    const int8_t soc = socFromCurve(emaVolts);
    if (soc < 0) snprintf(b, sizeof(b), "-- (out of range)");
    else snprintf(b, sizeof(b), "%d %%", soc);
    field(178, "SoC (curve)", b, soc >= 0 && soc <= 15 ? col::ERR : col::TEXT);
    snprintf(b, sizeof(b), "%s", CS_NAME[chargeState]);
    field(200, "Charge", b, chargeState == CS_CHARGING ? col::OK : col::MUTED);
}

void pageNoise() {
    char b[40];
    const uint16_t spread = rawMax - rawMin;
    /* Spread in raw counts is meaningless on its own; what matters is what it
     * is worth at the cell, because that is the number the percentage moves
     * on. ~1.1mV per count at the pin, doubled by the divider. */
    const float mvPerCount = (adcVolts * 1000.0f) / (rawTrimmed ? rawTrimmed : 1);
    const float spreadMv = spread * mvPerCount * cfg::DIVIDER_RATIO;

    snprintf(b, sizeof(b), "%u samples/burst", (unsigned)cfg::BURST_SAMPLES);
    field(40, "Burst", b);
    snprintf(b, sizeof(b), "%u", rawMin);
    field(62, "min", b);
    snprintf(b, sizeof(b), "%u", rawMax);
    field(84, "max", b);
    snprintf(b, sizeof(b), "%u counts", spread);
    field(106, "spread", b, spread > 40 ? col::WARN : col::OK);
    snprintf(b, sizeof(b), "%.1f mV at cell", spreadMv);
    field(128, "spread as", b, spreadMv > 40 ? col::WARN : col::OK);
    snprintf(b, sizeof(b), "mean %u / med %u", rawMean, rawMedian);
    field(150, "centre", b);
    snprintf(b, sizeof(b), "%u (trim %u/end)", rawTrimmed, (unsigned)cfg::TRIM);
    field(172, "trimmed", b);
    snprintf(b, sizeof(b), "%u .. %u", sessRawMin, sessRawMax);
    field(194, "session", b);
}

void pageFilter() {
    char b[40];
    const float curveRaw = (float)socFromCurve(cellVolts);
    const float curveEma = (float)socFromCurve(emaVolts);

    field(40, "Compare", "raw vs EMA");
    snprintf(b, sizeof(b), "%.3f V", cellVolts);
    field(70, "raw (trimmed)", b);
    snprintf(b, sizeof(b), "%.3f V", emaVolts);
    field(92, "EMA a=0.25", b, col::OK);
    snprintf(b, sizeof(b), "%.1f mV", (cellVolts - emaVolts) * 1000.0f);
    field(114, "difference", b);
    snprintf(b, sizeof(b), "%d %%", (int)curveRaw);
    field(144, "SoC from raw", b);
    snprintf(b, sizeof(b), "%d %%", (int)curveEma);
    field(166, "SoC from EMA", b, col::OK);
    /* The whole reason filtering is not optional: if these two disagree, the
     * unfiltered badge is what flickers between two numbers on the shelf. */
    field(196, "Verdict", curveRaw == curveEma ? "stable, agree" : "raw would flicker",
          curveRaw == curveEma ? col::OK : col::WARN);
}

void pageSoc() {
    char b[40];
    const int8_t curve = socFromCurve(emaVolts);
    const uint8_t lin = socLinear(emaVolts);

    snprintf(b, sizeof(b), "%.3f V (EMA)", emaVolts);
    field(40, "Cell", b);
    snprintf(b, sizeof(b), "%d %%", curve);
    field(66, "Piecewise curve", b, col::OK);
    snprintf(b, sizeof(b), "%u %%", lin);
    field(88, "Naive linear", b, col::WARN);
    snprintf(b, sizeof(b), "%+d points", (int)lin - (int)curve);
    field(110, "Linear error", b, col::ERR);

    // Which two curve points are being interpolated between right now.
    const char* between = "-";
    static char bt[32];
    for (uint8_t i = 1; i < LIPO_COUNT; ++i) {
        if (emaVolts >= LIPO_CURVE[i].volts) {
            snprintf(bt, sizeof(bt), "%u%%-%u%% (%.2f-%.2f)",
                     LIPO_CURVE[i].pct, LIPO_CURVE[i - 1].pct,
                     LIPO_CURVE[i].volts, LIPO_CURVE[i - 1].volts);
            between = bt;
            break;
        }
    }
    field(140, "Between", between);

    tft.setTextColor(col::MUTED, col::BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("Voltage SoC is an estimate, not a measurement: there is  ", 8, 172, 1);
    tft.drawString("no coulomb counter or fuel gauge on this board.         ", 8, 186, 1);
    tft.drawString("Under load the cell sags, so SoC reads low while busy.  ", 8, 200, 1);
    tft.setTextDatum(TL_DATUM);
}

void pageCharge() {
    char b[44];
    snprintf(b, sizeof(b), "%s", CS_NAME[chargeState]);
    field(38, "Verdict", b, chargeState == CS_CHARGING ? col::OK : col::TEXT);
    field(60, "Confidence", "INFERRED (no CHRG)", col::WARN);
    snprintf(b, sizeof(b), "%+.0f mV (trip %.0f)", lastStepV * 1000.0f, cfg::CHARGE_STEP_V * 1000.0f);
    field(82, "Last step", b);
    snprintf(b, sizeof(b), "%.3f V (trip %.2f)", cellVolts, cfg::V_CHARGER_HELD);
    field(104, "Held test", b);
    const uint32_t age = chargeTracking ? (millis() - chargeRefMs) : 0;
    snprintf(b, sizeof(b), "%lus / %lus", (unsigned long)(age / 1000),
             (unsigned long)(cfg::CHARGE_WINDOW_MS / 1000));
    field(126, "Trend window", b);
    snprintf(b, sizeof(b), "%+.1f mV (trip %.0f)", (chargeSmoothV - chargeRefV) * 1000.0f,
             cfg::CHARGE_TREND_V * 1000.0f);
    field(148, "Trend so far", b);

    tft.setTextColor(col::MUTED, col::BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("Scenario: plug/unplug USB, watch it flip (~2s)  ", 8, 172, 1);
    for (uint8_t i = 0; i < 3 && i < eventCount; ++i) {
        char line[52];
        snprintf(line, sizeof(line), "%7lus  %-34s", (unsigned long)(events[i].ms / 1000), events[i].text);
        tft.setTextColor(col::TEXT, col::BG);
        tft.drawString(line, 8, 188 + i * 12, 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void pageNobat() {
    char b[40];
    tft.setTextColor(col::MUTED, col::BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("Resolves the one threshold the manual does not give.", 8, 38, 1);
    tft.drawString("1. USB in, battery CONNECTED   -> press 'm'", 8, 54, 1);
    tft.drawString("2. USB in, battery UNPLUGGED   -> press 'm' again", 8, 68, 1);
    tft.setTextDatum(TL_DATUM);

    snprintf(b, sizeof(b), "%.3f V", cellVolts);
    field(92, "Reading now", b);
    snprintf(b, sizeof(b), nobatWithPack > 0 ? "%.3f V" : "-- press m", nobatWithPack);
    field(118, "With pack", b, nobatWithPack > 0 ? col::OK : col::MUTED);
    snprintf(b, sizeof(b), nobatWithout > 0 ? "%.3f V" : "-- press m", nobatWithout);
    field(140, "Without pack", b, nobatWithout > 0 ? col::OK : col::MUTED);

    if (nobatWithPack > 0 && nobatWithout > 0) {
        const float mid = (nobatWithPack + nobatWithout) / 2.0f;
        snprintf(b, sizeof(b), "%.2f V", mid);
        field(166, "Threshold", b, col::OK);
        const bool worksToday = nobatWithout > cfg::V_NO_BATTERY;
        field(192, "Shipping 4.35V", worksToday ? "would detect it" : "NEVER FIRES - bug",
              worksToday ? col::OK : col::ERR);
    } else {
        field(178, "Shipping value", "V_NO_BATTERY = 4.35 V", col::MUTED);
    }
}

void pageCal() {
    char b[46];
    snprintf(b, sizeof(b), "%.4f V", adcVolts);
    field(38, "ADC pin reads", b);
    snprintf(b, sizeof(b), "%.3f V", cellVolts);
    field(60, "So cell should be", b);

    /* Meter-free cross-check. The TP4054 is itself a voltage reference: it
     * floats a full cell at 4.20V +/-1%, i.e. 4.158..4.242V. So the PEAK
     * reading seen while the charger was holding the pack is a known voltage,
     * and if it lands inside that band the divider ratio is right to about
     * 1% without any instrument at all. It cannot catch a small offset error
     * the way a meter would, but it does catch a wrong ratio. */
    const bool havePeak = sessCellMax > 4.0f;
    const bool inBand = sessCellMax >= 4.158f && sessCellMax <= 4.242f;
    snprintf(b, sizeof(b), havePeak ? "%.3f V" : "charge it first", sessCellMax);
    field(84, "Peak seen", b, !havePeak ? col::MUTED : (inBand ? col::OK : col::ERR));
    field(106, "TP4054 floats", "4.20 V +/-1%", col::MUTED);
    if (havePeak) {
        field(128, "Verdict", inBand ? "ratio right to ~1%" : "RATIO LOOKS WRONG",
              inBand ? col::OK : col::ERR);
    } else {
        field(128, "Verdict", "plug USB, let it top up", col::MUTED);
    }

    tft.setTextColor(col::MUTED, col::BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("With a meter, read the true ratio off the table below:", 8, 150, 1);
    tft.setTextDatum(TL_DATUM);

    /* No text entry on a board with no keyboard: instead show what the true
     * ratio would be for a range of meter readings around the prediction, so
     * the developer reads the answer off rather than typing one in. */
    const float centre = cellVolts;
    int16_t y = 166;
    for (int8_t i = -2; i <= 2; ++i) {
        const float meter = centre + i * 0.05f;
        const float ratio = adcVolts > 0.01f ? meter / adcVolts : 0.0f;
        char line[52];
        snprintf(line, sizeof(line), "meter %.2f V   ->   true ratio %.3f  %s ",
                 meter, ratio, (i == 0) ? "<- predicted" : "            ");
        tft.setTextColor(i == 0 ? col::OK : col::TEXT, col::BG);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(line, 8, y, 1);
        tft.setTextDatum(TL_DATUM);
        y += 15;
    }
}

void pageLog() {
    char b[40];
    const uint32_t elapsed = (millis() - startedMs) / 1000;
    snprintf(b, sizeof(b), "%luh %02lum %02lus", (unsigned long)(elapsed / 3600),
             (unsigned long)((elapsed % 3600) / 60), (unsigned long)(elapsed % 60));
    field(46, "Running", b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)sampleCount);
    field(72, "Bursts", b);
    field(98, "CSV", csvOn ? "streaming (c=off)" : "paused (c=on)",
          csvOn ? col::OK : col::MUTED);
    snprintf(b, sizeof(b), "%.3f .. %.3f V", sessCellMin, sessCellMax);
    field(124, "Cell range", b);
    snprintf(b, sizeof(b), "%.1f mV", (sessCellMax - sessCellMin) * 1000.0f);
    field(146, "Excursion", b);

    tft.setTextColor(col::MUTED, col::BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("Capture a full discharge:", 8, 176, 1);
    tft.drawString("pio device monitor | tee discharge.csv", 8, 190, 1);
    tft.drawString("columns: ms,raw,adc_mv,cell_mv,pct,state", 8, 204, 1);
    tft.setTextDatum(TL_DATUM);
}

// ------------------------------------------------------------------- wizard

void wizEnter(WizStage st) {
    wizStage = st;
    wizStageStartMs = millis();
    wizSum = 0.0; wizN = 0; wizMin = 99.0f; wizMax = 0.0f;
    pageDirty = true;
}

void wizReset() {
    wizA = wizB = wizC = 0.0f;
    wizEnter(WZ_INTRO);
}

/* Advances on the fast cadence. Measurement stages average every burst in
 * their window rather than snapshotting one, because the interesting stages
 * are exactly the ones where the voltage is settling after a connector moved. */
void wizTick() {
    const uint32_t now = millis();
    const uint32_t elapsed = now - wizStageStartMs;

    if (wizStage == WZ_MEASURE_A || wizStage == WZ_MEASURE_B || wizStage == WZ_MEASURE_C) {
        wizSum += cellVolts;
        ++wizN;
        if (cellVolts < wizMin) wizMin = cellVolts;
        if (cellVolts > wizMax) wizMax = cellVolts;
    }

    if (wizStage == WZ_DONE || elapsed < WZ_MS[wizStage]) {
        return;
    }

    const float avg = wizN ? (float)(wizSum / wizN) : 0.0f;
    switch (wizStage) {
        case WZ_INTRO:     wizEnter(WZ_MEASURE_A); break;
        case WZ_MEASURE_A:
            wizA = avg; wizAmin = wizMin; wizAmax = wizMax;
            logEvent("A pack+USB = %.3fV", wizA);
            wizEnter(WZ_PROMPT_B);
            break;
        case WZ_PROMPT_B:  wizEnter(WZ_MEASURE_B); break;
        case WZ_MEASURE_B:
            wizB = avg; wizBmin = wizMin; wizBmax = wizMax;
            logEvent("B USB only = %.3fV", wizB);
            wizEnter(WZ_PROMPT_C);
            break;
        case WZ_PROMPT_C:  wizEnter(WZ_MEASURE_C); break;
        case WZ_MEASURE_C:
            wizC = avg; wizCmin = wizMin; wizCmax = wizMax;
            logEvent("C pack only = %.3fV", wizC);
            wizEnter(WZ_DONE);
            break;
        default: break;
    }
}

/* A full-width banner that alternates colour twice a second. The user is
 * looking at the connector, not the screen, so the prompt has to be findable
 * out of the corner of an eye. */
void wizBanner(const char* l1, const char* l2, uint32_t remainMs, bool urgent) {
    const bool on = (millis() / 500) % 2 == 0;
    const uint16_t fill = urgent ? (on ? col::ERR : col::BAR) : (on ? col::ACCENT : col::BAR);
    tft.fillRect(0, HDR_H, 320, 76, fill);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, fill);
    tft.drawString(l1, 160, HDR_H + 22, 4);
    tft.drawString(l2, 160, HDR_H + 52, 2);
    char c[24];
    snprintf(c, sizeof(c), "  %lu s  ", (unsigned long)((remainMs + 999) / 1000));
    tft.setTextColor(urgent ? col::ERR : col::OK, col::BG);
    tft.drawString(c, 160, HDR_H + 96, 6);
    tft.setTextDatum(TL_DATUM);
    led(urgent && on, !urgent, false);
}

void wizResult(int16_t y, const char* label, float v, float lo, float hi, uint16_t c) {
    char b[40];
    if (v <= 0.0f) { field(y, label, "--", col::MUTED); return; }
    snprintf(b, sizeof(b), "%.3f V (%.2f-%.2f)", v, lo, hi);
    field(y, label, b, c);
}

void pageWizard() {
    const uint32_t elapsed = millis() - wizStageStartMs;
    const uint32_t remain = wizStage == WZ_DONE ? 0
                          : (elapsed >= WZ_MS[wizStage] ? 0 : WZ_MS[wizStage] - elapsed);

    if (wizStage != wizLastDrawn) {
        tft.fillRect(0, HDR_H, 320, FTR_Y - HDR_H, col::BG);
        wizLastDrawn = wizStage;
    }

    switch (wizStage) {
        case WZ_INTRO:
            wizBanner("BATTERY + USB", "both connected, starting...", remain, false);
            return;
        case WZ_MEASURE_A:
            wizBanner("MEASURING A", "pack + USB -- do not touch", remain, false);
            return;
        case WZ_PROMPT_B:
            wizBanner("UNPLUG BATTERY", "leave USB connected", remain, true);
            return;
        case WZ_MEASURE_B:
            wizBanner("MEASURING B", "USB only, no pack", remain, false);
            return;
        case WZ_PROMPT_C:
            /* Ordering is not a style point: reversing it removes both power
             * sources at once and the board dies mid-test. */
            wizBanner("BATTERY IN FIRST", "then unplug USB", remain, true);
            return;
        case WZ_MEASURE_C:
            wizBanner("MEASURING C", "battery only", remain, false);
            return;
        default: break;
    }

    led(false, false, true);
    wizResult(40,  "A pack+USB", wizA, wizAmin, wizAmax, col::OK);
    wizResult(62,  "B USB only", wizB, wizBmin, wizBmax, col::WARN);
    wizResult(84,  "C pack only", wizC, wizCmin, wizCmax, col::OK);

    char b[44];
    const bool fires = wizB >= cfg::V_NO_BATTERY;
    snprintf(b, sizeof(b), fires ? "fires (%.2f)" : "NEVER (%.2f)", cfg::V_NO_BATTERY);
    field(112, "V_NO_BATTERY", b, fires ? col::OK : col::ERR);

    /* The whole question in one line: if the no-pack reading sits BETWEEN the
     * two with-pack readings, then no single threshold can separate "missing"
     * from "present", however it is chosen. */
    const float lo = wizA < wizC ? wizA : wizC;
    const float hi = wizA < wizC ? wizC : wizA;
    const bool between = wizB > lo && wizB < hi;
    field(134, "B separable?", between ? "NO - sits between" : "yes, outside A..C",
          between ? col::ERR : col::OK);

    if (!between && wizB > 0) {
        snprintf(b, sizeof(b), "%.3f V", wizB > hi ? (wizB + hi) / 2 : (wizB + lo) / 2);
        field(156, "Threshold", b, col::OK);
    } else {
        field(156, "Verdict", "presence undetectable", col::ERR);
    }

    tft.setTextColor(col::MUTED, col::BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("BOOT = leave wizard    serial 'w' = run again        ", 8, 186, 1);
    tft.drawString("A/B/C are averages over 8s; brackets are min-max.    ", 8, 200, 1);
    tft.setTextDatum(TL_DATUM);
}

void drawPage() {
    if (pageDirty) {
        drawChrome();
        pageDirty = false;
    }
    switch (page) {
        case PAGE_WIZARD: pageWizard(); break;
        case PAGE_LIVE:   pageLive();   break;
        case PAGE_NOISE:  pageNoise();  break;
        case PAGE_FILTER: pageFilter(); break;
        case PAGE_SOC:    pageSoc();    break;
        case PAGE_CHARGE: pageCharge(); break;
        case PAGE_NOBAT:  pageNobat();  break;
        case PAGE_CAL:    pageCal();    break;
        case PAGE_LOG:    pageLog();    break;
        default: break;
    }
}

void setPage(uint8_t p) {
    page = p % PAGE_COUNT;
    pageDirty = true;
    if (page != PAGE_WIZARD) {
        led(false, false, false);
    }
    wizLastDrawn = WZ_DONE;   // force the wizard to repaint when re-entered
    /* Announced because it was not: locating a page over serial previously
     * meant probing blind, one keypress at a time. */
    Serial.printf("# page %u/%u %s\n", page + 1, (unsigned)PAGE_COUNT, PAGE_NAME[page]);
}

// -------------------------------------------------------------------- input

void pollButton() {
    // IO0 has an external pull-up; pressed reads LOW. Simple debounce.
    static bool wasDown = false;
    static uint32_t lastEdgeMs = 0;
    const bool down = digitalRead(0) == LOW;
    const uint32_t now = millis();
    if (down != wasDown && now - lastEdgeMs > 40) {
        lastEdgeMs = now;
        wasDown = down;
        if (down) setPage(page + 1);
    }
}

void pollSerial() {
    while (Serial.available()) {
        switch (Serial.read()) {
            case 'n': setPage(page + 1); break;
            case 'p': setPage(page + PAGE_COUNT - 1); break;
            case 'c': csvOn = !csvOn; pageDirty = true; break;
            case 'w': setPage(PAGE_WIZARD); wizReset(); break;
            case 'r':
                sessRawMin = 0xFFFF; sessRawMax = 0;
                sessCellMin = 99.0f; sessCellMax = 0.0f;
                sampleCount = 0; startedMs = millis();
                eventCount = 0; emaSeeded = false;
                logEvent("statistics reset");
                pageDirty = true;
                break;
            case 'm':
                /* On the NO-BATT page the mark doubles as the experiment's
                 * two captures; anywhere else it is just a log annotation. */
                if (page == PAGE_NOBAT) {
                    if (nobatWithPack <= 0) {
                        nobatWithPack = cellVolts;
                        logEvent("captured WITH pack: %.3fV", cellVolts);
                    } else {
                        nobatWithout = cellVolts;
                        logEvent("captured WITHOUT pack: %.3fV", cellVolts);
                    }
                } else {
                    logEvent("mark at %.3fV", cellVolts);
                }
                pageDirty = true;
                break;
            default: break;
        }
    }
}

// --------------------------------------------------------------------- main

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=== E32R28T-1 battery diagnostics ===");
    Serial.printf("BAT_ADC=IO34 (ADC1_CH6)  divider x%.2f  cal x%.3f\n",
                  cfg::DIVIDER_RATIO, cfg::CAL_FACTOR);
    Serial.println("Charger TP4054, 500mA max. No CHRG pin, no VBUS pin:");
    Serial.println("charging is INFERRED from cell voltage alone.");
    Serial.println("Keys: n/p page, r reset, m mark, c csv, w rerun wizard");
    Serial.println("Wizard: A pack+USB -> B USB only -> C pack only");
    Serial.println("ms,raw,adc_mv,cell_mv,pct,state");

    pinMode(0, INPUT_PULLUP);
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_BLUE, OUTPUT);
    led(false, false, false);
    pinMode(PIN_BAT_ADC, INPUT);
    analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
    /* Same characterisation the product uses: eFuse-backed, not a nominal
     * 3.3V reference. At 11dB the converter is linear only to ~2.45V, and a
     * 4.2V cell through a 2:1 divider lands at 2.1V -- comfortably inside it. */
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                             cfg::ADC_VREF_MV, &adcChars);

    tft.init();
    tft.setRotation(CYD_SCREEN_ROTATION);
    pinMode(PIN_TFT_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_TFT_BACKLIGHT, HIGH);

    startedMs = millis();
    sampleBurst();
    wizReset();   // boot straight into the guided A/B/C test
    lastProdSampleMs = millis();
    updateChargeState(cellVolts, lastProdSampleMs);
    drawChrome();
}

void loop() {
    pollButton();
    pollSerial();

    static uint32_t lastFastMs = 0;
    const uint32_t now = millis();
    if (now - lastFastMs >= cfg::FAST_SAMPLE_MS) {
        lastFastMs = now;
        sampleBurst();
        if (page == PAGE_WIZARD) {
            wizTick();
        }

        /* The charge inference runs on the product's 2s cadence, not this
         * tool's 250ms one -- its step threshold is calibrated to that
         * interval, and feeding it faster samples would make plug detection
         * behave differently here than it does in the app. */
        if (now - lastProdSampleMs >= cfg::PROD_SAMPLE_MS) {
            lastProdSampleMs = now;
            updateChargeState(cellVolts, now);
            if (csvOn) {
                Serial.printf("%lu,%u,%.1f,%.1f,%d,%s\n",
                              (unsigned long)now, rawTrimmed, adcVolts * 1000.0f,
                              cellVolts * 1000.0f, socFromCurve(emaVolts),
                              CS_NAME[chargeState]);
            }
        }
        drawPage();
    }
}
