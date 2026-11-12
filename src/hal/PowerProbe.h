// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>

/* What a platform can tell the battery diagnostic, and the one thing a new
 * platform has to write to take part in it.
 *
 * The recorder (PowerMonitor) and the analysis (PowerLedger) are the same code
 * everywhere. They ask `platformPowerProbe()` for a reading every few seconds
 * and work with whatever comes back, because what can be measured differs by
 * an order of magnitude between platforms: a handheld's PMIC reports the
 * current leaving the cell, which answers "what is this costing" in seconds,
 * while an ESP32 board has one voltage divider and needs an hour to see a
 * slope. A field a platform cannot answer stays at its default, and every
 * consumer says "not measured" rather than showing a zero.
 *
 * To add a platform: implement this class in one .cpp that only that platform
 * builds, and define platformPowerProbe() returning a static instance. An
 * implementation that fills in nothing still works. PowerMonitor falls back to
 * the firmware's own gauge and loop load, so such a platform gets the
 * percentage-per-hour analysis and only lacks the detail.
 *
 * read() runs on the monitor's task, never on a render path, so it may read
 * files or registers. It must not allocate from the firmware's heap on a board
 * and must not transmit anything: this reading stays on the device. */
struct PowerReading {
    static constexpr int32_t UNKNOWN = INT32_MIN;
    static constexpr uint8_t MAX_DETAILS = 8;
    static constexpr uint8_t DETAIL_LABEL = 12;
    static constexpr uint8_t DETAIL_VALUE = 22;

    /* Whatever else this platform can see that might explain a drain, as text
     * for a person to read. Never identifies a device, a network or a player. */
    struct Detail {
        char label[DETAIL_LABEL] = {0};
        char value[DETAIL_VALUE] = {0};
    };

    int32_t milliamps = UNKNOWN;        // out of the cell positive, into it negative
    int32_t cellMillivolts = UNKNOWN;
    int16_t percent = -1;               // -1: not known
    int8_t onCharger = -1;              // 1, 0, -1: this platform cannot tell
    int8_t lightOn = -1;                // case LEDs lit: 1, 0, -1: not measured
    int16_t cpuPercent = -1;            // whole-system load, or -1
    int32_t cpuMhz = UNKNOWN;
    /* The lamp as it really is, where that can differ from what the firmware
     * asked for. -1 means the firmware's own setting is the truth. */
    int16_t lampPercent = -1;
    Detail details[MAX_DETAILS];
    uint8_t detailCount = 0;

    void addDetail(const char* label, const char* value);
};

class PowerProbe {
public:
    virtual ~PowerProbe() = default;
    /* One short phrase saying how trustworthy the numbers are. */
    virtual const char* source() const = 0;
    virtual void read(PowerReading& out) = 0;
};

/* Defined once per platform: hal/PowerProbeEsp32.cpp, host/src/HostPowerProbe.cpp. */
PowerProbe& platformPowerProbe();
