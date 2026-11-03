// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "PowerProbe.h"

/* Where the battery went: the analysis half of the drain diagnostic.
 *
 * Time on battery is divided between the states the device was in -- awake or
 * asleep, how bright the lamp was, which radios were up, whether the case
 * lights were lit -- and each state collects what it cost. Comparing two rows
 * that differ in one respect is the measurement: the difference between
 * "Asleep" and "Asleep W" is what keeping Wi-Fi up while asleep costs.
 *
 * Percentages are summed as deltas, so measurement noise cancels instead of
 * accumulating: a reading that flickers down and up nets to nothing, and the
 * sum over a state telescopes to the real change. A step landing just after a
 * state change is charged to the new state, which is why a percentage rate is
 * withheld until a state has MIN_RATE_SECONDS behind it. Current has no such
 * problem and is reported at once.
 *
 * Pure: no Arduino and no clock, so test/host/power_ledger_test.cpp can drive
 * it. Fixed size and allocation-free, like everything else that lives for the
 * whole of a run. */
class PowerLedger {
public:
    static constexpr uint8_t MAX_STATES = 12;
    static constexpr uint32_t MIN_RATE_SECONDS = 20 * 60;
    static constexpr uint8_t LABEL_SIZE = 16;

    enum class Activity : uint8_t { Awake, Saver, Locked, Asleep };
    enum class Lamp : uint8_t { Off, Dim, Mid, Full };

    struct State {
        Activity activity = Activity::Awake;
        Lamp lamp = Lamp::Off;
        bool wifi = false;
        bool bluetooth = false;
        int8_t light = -1;       // as PowerReading::lightOn
        bool other = false;      // the overflow row: the table ran out

        bool operator==(const State& o) const {
            return activity == o.activity && lamp == o.lamp && wifi == o.wifi &&
                   bluetooth == o.bluetooth && light == o.light && other == o.other;
        }
    };

    struct Row {
        State state;
        uint32_t seconds = 0;
        uint32_t milliampSeconds = 0;
        uint32_t currentSeconds = 0;   // of `seconds`, how many had a current reading
        int32_t percentDrop = 0;       // negative if it somehow rose
        uint32_t cpuSum = 0;           // percent x seconds
        uint32_t cpuSeconds = 0;

        /* Average draw, or -1 when no current was measured here. */
        int32_t milliamps() const;
        /* Tenths of a percent per hour, or -1 until MIN_RATE_SECONDS have passed
         * or if the charge did not fall. */
        int32_t tenthsPercentPerHour() const;
        int16_t cpuPercent() const;
    };

    static Lamp lampBand(int16_t percent);
    /* "Awake full WBL". W Wi-Fi, B Bluetooth, L the case lights. */
    static void label(const State& state, char* out, size_t size);

    void reset();
    /* `seconds` of time that ended with this reading, spent in `state`. Time on
     * a charger is counted but charged to nothing. */
    void add(const State& state, const PowerReading& reading, uint32_t seconds);

    uint8_t count() const { return count_; }
    const Row& row(uint8_t i) const { return rows_[i]; }
    const Row* find(const State& state) const;
    /* Row indices, most costly first: by current wherever every row has one,
     * otherwise by the charge lost, otherwise by time. */
    void ranked(uint8_t out[MAX_STATES]) const;
    /* The share of everything recorded that this row cost, 0..100, on the same
     * measure ranked() chose. */
    uint8_t sharePercent(uint8_t index) const;

    uint32_t batterySeconds() const { return batterySeconds_; }
    uint32_t chargerSeconds() const { return chargerSeconds_; }

private:
    uint64_t costOf(const Row& row, uint8_t measure) const;
    uint8_t measure() const;
    Row* rowFor(const State& state);

    Row rows_[MAX_STATES];
    uint8_t count_ = 0;
    uint32_t batterySeconds_ = 0;
    uint32_t chargerSeconds_ = 0;
    int16_t lastPercent_ = -1;
};
