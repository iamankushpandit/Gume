// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of the battery drain analysis, compiled against the real
// src/hal/PowerLedger.cpp, with readings written by hand the way a platform's
// PowerProbe would produce them. It will not compile if Arduino reaches the
// ledger, which is what keeps the analysis the same on every platform.
//
// Not run by CI and not picked up by pio test. From the repository root:
//   g++ -std=c++17 -Isrc/hal test/host/power_ledger_test.cpp src/hal/PowerLedger.cpp -o /tmp/ledger && /tmp/ledger

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "PowerLedger.h"

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond);       \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

using Ledger = PowerLedger;

static Ledger::State state(Ledger::Activity activity, Ledger::Lamp lamp, bool wifi = false,
                           bool bluetooth = false, int8_t light = -1) {
    Ledger::State s;
    s.activity = activity;
    s.lamp = lamp;
    s.wifi = wifi;
    s.bluetooth = bluetooth;
    s.light = light;
    return s;
}

static PowerReading reading(int percent, int milliamps = PowerReading::UNKNOWN, int charger = -1) {
    PowerReading r;
    r.percent = static_cast<int16_t>(percent);
    r.milliamps = milliamps;
    r.onCharger = static_cast<int8_t>(charger);
    return r;
}

static void testLabels() {
    char text[Ledger::LABEL_SIZE];
    Ledger::label(state(Ledger::Activity::Awake, Ledger::Lamp::Full, true, true, 1), text, sizeof(text));
    CHECK(std::strcmp(text, "Awake full WBL") == 0);
    Ledger::label(state(Ledger::Activity::Asleep, Ledger::Lamp::Off), text, sizeof(text));
    CHECK(std::strcmp(text, "Asleep") == 0);
    /* An unmeasured light and an unlit one read alike: only lit is news. */
    Ledger::label(state(Ledger::Activity::Saver, Ledger::Lamp::Dim, true, false, 0), text, sizeof(text));
    CHECK(std::strcmp(text, "Saver dim W") == 0);
    CHECK(Ledger::lampBand(0) == Ledger::Lamp::Off);
    CHECK(Ledger::lampBand(25) == Ledger::Lamp::Dim);
    CHECK(Ledger::lampBand(60) == Ledger::Lamp::Mid);
    CHECK(Ledger::lampBand(100) == Ledger::Lamp::Full);
}

/* With a current sensor the costs are right at once and the ranking follows the
 * energy, not the time: an hour asleep costs less than ten minutes at full lamp. */
static void testCurrentRanksByEnergy() {
    Ledger ledger;
    const auto awake = state(Ledger::Activity::Awake, Ledger::Lamp::Full, true);
    const auto asleep = state(Ledger::Activity::Asleep, Ledger::Lamp::Off, true);
    ledger.add(asleep, reading(80, 50), 3600);
    ledger.add(awake, reading(78, 800), 600);
    CHECK(ledger.count() == 2);
    uint8_t order[Ledger::MAX_STATES];
    ledger.ranked(order);
    CHECK(ledger.row(order[0]).state == awake);
    CHECK(ledger.row(order[0]).milliamps() == 800);
    CHECK(ledger.row(order[1]).milliamps() == 50);
    /* 480000 mAs against 180000. */
    CHECK(ledger.sharePercent(order[0]) == 73);
    CHECK(ledger.sharePercent(order[1]) == 27);
}

/* Voltage only, the ESP32 case: nothing but charge lost, withheld until there
 * has been time for it to mean anything. */
static void testPercentRateWaitsForTime() {
    Ledger ledger;
    const auto asleep = state(Ledger::Activity::Asleep, Ledger::Lamp::Off, true);
    ledger.add(asleep, reading(90), 10);
    ledger.add(asleep, reading(88), Ledger::MIN_RATE_SECONDS - 100);
    CHECK(ledger.find(asleep)->tenthsPercentPerHour() == -1);
    CHECK(ledger.find(asleep)->milliamps() == -1);
    ledger.add(asleep, reading(86), 3600 - Ledger::MIN_RATE_SECONDS + 90);
    CHECK(ledger.find(asleep)->seconds == 3600);
    CHECK(ledger.find(asleep)->tenthsPercentPerHour() == 40);
}

/* A reading that flickers must net out. Nothing may accumulate from noise. */
static void testNoiseCancels() {
    Ledger ledger;
    const auto awake = state(Ledger::Activity::Awake, Ledger::Lamp::Mid);
    ledger.add(awake, reading(50), 10);
    for (int i = 0; i < 100; ++i) {
        ledger.add(awake, reading(i % 2 ? 50 : 48), 10);
    }
    CHECK(ledger.find(awake)->percentDrop == 0);
    CHECK(ledger.find(awake)->tenthsPercentPerHour() == -1);
}

/* Charger time costs nothing, and the climb on the charger is not a negative
 * drain charged to whatever follows it. */
static void testChargerIsNotDrain() {
    Ledger ledger;
    const auto awake = state(Ledger::Activity::Awake, Ledger::Lamp::Full);
    ledger.add(awake, reading(20, 500, 0), 60);
    ledger.add(awake, reading(60, -900, 1), 3600);
    CHECK(ledger.chargerSeconds() == 3600);
    CHECK(ledger.batterySeconds() == 60);
    ledger.add(awake, reading(59, 500, 0), 60);
    CHECK(ledger.find(awake)->percentDrop == 1);
    CHECK(ledger.find(awake)->milliampSeconds == 60000);
}

/* A current into the cell with no charger reported must not reduce a cost. */
static void testNegativeCurrentIsNotACost() {
    Ledger ledger;
    const auto awake = state(Ledger::Activity::Awake, Ledger::Lamp::Full);
    ledger.add(awake, reading(50, 400), 10);
    ledger.add(awake, reading(50, -400), 10);
    CHECK(ledger.find(awake)->milliampSeconds == 4000);
}

/* One row with no current means ranking cannot use current for any of them. */
static void testMixedCurrentFallsBackToCharge() {
    Ledger ledger;
    const auto a = state(Ledger::Activity::Awake, Ledger::Lamp::Full);
    const auto b = state(Ledger::Activity::Asleep, Ledger::Lamp::Off);
    ledger.add(a, reading(90, 100), 10);
    ledger.add(b, reading(90), 10);
    ledger.add(b, reading(80), 10);
    uint8_t order[Ledger::MAX_STATES];
    ledger.ranked(order);
    CHECK(ledger.row(order[0]).state == b);
    CHECK(ledger.sharePercent(order[0]) == 100);
}

/* Running out of rows loses detail, never cost. */
static void testOverflowKeepsTheCost() {
    Ledger ledger;
    uint32_t total = 0;
    for (int band = 0; band < 4; ++band) {
        for (int activity = 0; activity < 4; ++activity) {
            for (int wifi = 0; wifi < 2; ++wifi) {
                ledger.add(state(static_cast<Ledger::Activity>(activity),
                                 static_cast<Ledger::Lamp>(band), wifi == 1),
                           reading(50, 100), 10);
                total += 10;
            }
        }
    }
    CHECK(ledger.count() == Ledger::MAX_STATES);
    CHECK(ledger.row(Ledger::MAX_STATES - 1).state.other);
    uint32_t seen = 0, cost = 0;
    for (uint8_t i = 0; i < ledger.count(); ++i) {
        seen += ledger.row(i).seconds;
        cost += ledger.row(i).milliampSeconds;
    }
    CHECK(seen == total);
    CHECK(cost == total * 100);
}

static void testUnknownEverythingStillCountsTime() {
    Ledger ledger;
    const auto a = state(Ledger::Activity::Awake, Ledger::Lamp::Full);
    PowerReading empty;
    ledger.add(a, empty, 10);
    ledger.add(a, empty, 10);
    CHECK(ledger.batterySeconds() == 20);
    CHECK(ledger.find(a)->cpuPercent() == -1);
    uint8_t order[Ledger::MAX_STATES];
    ledger.ranked(order);
    CHECK(ledger.sharePercent(order[0]) == 100);
    ledger.reset();
    CHECK(ledger.count() == 0);
    CHECK(ledger.batterySeconds() == 0);
}

static void testDetailsAreBounded() {
    PowerReading r;
    for (int i = 0; i < PowerReading::MAX_DETAILS + 3; ++i) {
        r.addDetail("A label far too long for it", "A value very much too long for it to fit");
    }
    CHECK(r.detailCount == PowerReading::MAX_DETAILS);
    CHECK(std::strlen(r.details[0].label) == PowerReading::DETAIL_LABEL - 1);
    CHECK(std::strlen(r.details[0].value) == PowerReading::DETAIL_VALUE - 1);
}

int main() {
    testLabels();
    testCurrentRanksByEnergy();
    testPercentRateWaitsForTime();
    testNoiseCancels();
    testChargerIsNotDrain();
    testNegativeCurrentIsNotACost();
    testMixedCurrentFallsBackToCharge();
    testOverflowKeepsTheCost();
    testUnknownEverythingStillCountsTime();
    testDetailsAreBounded();
    std::printf("power ledger: all passed\n");
    return 0;
}
