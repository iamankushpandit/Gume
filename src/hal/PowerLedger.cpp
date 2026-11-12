// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "PowerLedger.h"

#include <stdio.h>
#include <string.h>

namespace {
void copyText(char* out, size_t size, const char* text) {
    strncpy(out, text, size - 1);
    out[size - 1] = '\0';
}
}   // namespace

void PowerReading::addDetail(const char* label, const char* value) {
    if (detailCount >= MAX_DETAILS) return;
    copyText(details[detailCount].label, DETAIL_LABEL, label);
    copyText(details[detailCount].value, DETAIL_VALUE, value);
    ++detailCount;
}

int32_t PowerLedger::Row::milliamps() const {
    if (currentSeconds == 0) return -1;
    return static_cast<int32_t>(milliampSeconds / currentSeconds);
}

int32_t PowerLedger::Row::tenthsPercentPerHour() const {
    if (seconds < MIN_RATE_SECONDS || percentDrop <= 0) return -1;
    return static_cast<int32_t>(static_cast<int64_t>(percentDrop) * 36000 / seconds);
}

int16_t PowerLedger::Row::cpuPercent() const {
    if (cpuSeconds == 0) return -1;
    return static_cast<int16_t>(cpuSum / cpuSeconds);
}

PowerLedger::Lamp PowerLedger::lampBand(int16_t percent) {
    if (percent <= 0) return Lamp::Off;
    if (percent < 50) return Lamp::Dim;
    if (percent < 85) return Lamp::Mid;
    return Lamp::Full;
}

void PowerLedger::label(const State& state, char* out, size_t size) {
    if (state.other) {
        copyText(out, size, "Other states");
        return;
    }
    static constexpr const char* ACTIVITY[] = {"Awake", "Saver", "Lock", "Asleep"};
    static constexpr const char* LAMP[] = {"", " dim", " mid", " full"};
    char radios[6] = {0};
    size_t n = 0;
    if (state.wifi) radios[n++] = 'W';
    if (state.bluetooth) radios[n++] = 'B';
    if (state.light == 1) radios[n++] = 'L';
    snprintf(out, size, "%s%s%s%s", ACTIVITY[static_cast<uint8_t>(state.activity)],
             LAMP[static_cast<uint8_t>(state.lamp)], n ? " " : "", radios);
}

void PowerLedger::reset() {
    for (Row& r : rows_) r = Row{};
    count_ = 0;
    batterySeconds_ = 0;
    chargerSeconds_ = 0;
    lastPercent_ = -1;
}

PowerLedger::Row* PowerLedger::rowFor(const State& state) {
    for (uint8_t i = 0; i < count_; ++i) {
        if (rows_[i].state == state) return &rows_[i];
    }
    /* The last row is kept for whatever does not fit, so running out of room
     * loses the detail of a rare state rather than its cost. */
    if (count_ < MAX_STATES - 1) {
        rows_[count_].state = state;
        return &rows_[count_++];
    }
    State other;
    other.other = true;
    if (count_ == MAX_STATES - 1) {
        rows_[count_].state = other;
        ++count_;
    }
    return &rows_[MAX_STATES - 1];
}

void PowerLedger::add(const State& state, const PowerReading& reading, uint32_t seconds) {
    const int16_t was = lastPercent_;
    if (reading.percent >= 0) lastPercent_ = reading.percent;

    if (reading.onCharger == 1) {
        chargerSeconds_ += seconds;
        return;
    }
    batterySeconds_ += seconds;

    Row* row = rowFor(state);
    row->seconds += seconds;
    if (was >= 0 && reading.percent >= 0) row->percentDrop += was - reading.percent;
    if (reading.milliamps != PowerReading::UNKNOWN) {
        /* A negative current with no charger reported is a charge this platform
         * could not name; it is not a cost, and it must not cancel one. */
        if (reading.milliamps > 0) {
            row->milliampSeconds += static_cast<uint32_t>(reading.milliamps) * seconds;
        }
        row->currentSeconds += seconds;
    }
    if (reading.cpuPercent >= 0) {
        row->cpuSum += static_cast<uint32_t>(reading.cpuPercent) * seconds;
        row->cpuSeconds += seconds;
    }
}

const PowerLedger::Row* PowerLedger::find(const State& state) const {
    for (uint8_t i = 0; i < count_; ++i) {
        if (rows_[i].state == state) return &rows_[i];
    }
    return nullptr;
}

/* 0 current, 1 charge lost, 2 time. Current only when every row has it: one row
 * without a reading would otherwise rank as costing nothing. */
uint8_t PowerLedger::measure() const {
    bool allCurrent = count_ > 0;
    bool anyDrop = false;
    for (uint8_t i = 0; i < count_; ++i) {
        if (rows_[i].currentSeconds == 0) allCurrent = false;
        if (rows_[i].percentDrop > 0) anyDrop = true;
    }
    return allCurrent ? 0 : (anyDrop ? 1 : 2);
}

uint64_t PowerLedger::costOf(const Row& row, uint8_t how) const {
    if (how == 0) return row.milliampSeconds;
    if (how == 1) return row.percentDrop > 0 ? static_cast<uint64_t>(row.percentDrop) : 0;
    return row.seconds;
}

void PowerLedger::ranked(uint8_t out[MAX_STATES]) const {
    const uint8_t how = measure();
    for (uint8_t i = 0; i < count_; ++i) {
        uint8_t at = i;
        while (at > 0 && costOf(rows_[out[at - 1]], how) < costOf(rows_[i], how)) {
            out[at] = out[at - 1];
            --at;
        }
        out[at] = i;
    }
}

uint8_t PowerLedger::sharePercent(uint8_t index) const {
    const uint8_t how = measure();
    uint64_t total = 0;
    for (uint8_t i = 0; i < count_; ++i) total += costOf(rows_[i], how);
    if (total == 0) return 0;
    return static_cast<uint8_t>((costOf(rows_[index], how) * 100 + total / 2) / total);
}
