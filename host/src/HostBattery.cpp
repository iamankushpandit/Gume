// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* See HostBattery.h. */

#include "HostBattery.h"

#include <dirent.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

/* The firmware's own LiPo curve, LIPO_CURVE in src/hal/BoardPower.cpp, which
 * is private to that file. It is copied here only to be run backwards, and
 * must stay the same table: a point that differs makes the badge disagree
 * with the fuel gauge by however far the two curves part. */
struct CurvePoint {
    uint32_t millivolts;
    float pct;
};
constexpr CurvePoint FIRMWARE_CURVE[] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3930, 70}, {3870, 60},
    {3820, 50},  {3790, 40}, {3770, 30}, {3740, 20}, {3680, 10},
    {3550, 5},   {3200, 0},
};
constexpr int CURVE_COUNT = sizeof(FIRMWARE_CURVE) / sizeof(FIRMWARE_CURVE[0]);

/* The voltage the firmware's curve turns back into `pct`. A quarter point
 * high, so that the millivolt lost to integer division on the way through
 * the virtual ADC never rounds the answer down a whole percent. */
uint32_t millivoltsFor(int pct) {
    if (pct >= 100) return FIRMWARE_CURVE[0].millivolts;
    if (pct <= 0) return FIRMWARE_CURVE[CURVE_COUNT - 1].millivolts;
    const float want = static_cast<float>(pct) + 0.25f;
    for (int i = 1; i < CURVE_COUNT; ++i) {
        const CurvePoint& hi = FIRMWARE_CURVE[i - 1];
        const CurvePoint& lo = FIRMWARE_CURVE[i];
        if (want >= lo.pct) {
            const float frac = (want - lo.pct) / (hi.pct - lo.pct);
            return lo.millivolts +
                   static_cast<uint32_t>(frac * (hi.millivolts - lo.millivolts) + 0.5f);
        }
    }
    return FIRMWARE_CURVE[CURVE_COUNT - 1].millivolts;
}

struct BatteryFiles {
    std::string capacity;   // the fuel gauge's percentage, when it has one
    std::string voltage;    // the cell voltage, otherwise
    std::string status;     // Charging, Discharging, Full, ...
    std::string current;    // the current through the cell, when the PMIC measures it
    std::vector<std::string> chargers;   // every other supply's `online`
};

bool readable(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    std::fclose(f);
    return true;
}

/* The first power supply whose type is "Battery": on the RG40XXV that is the
 * AXP2202's axp2202-battery. Found once; a battery does not come and go. */
BatteryFiles findBattery() {
    BatteryFiles found;
    DIR* dir = ::opendir("/sys/class/power_supply");
    if (!dir) return found;
    bool haveBattery = false;
    while (dirent* e = ::readdir(dir)) {
        if (e->d_name[0] == '.') continue;
        const std::string base = std::string("/sys/class/power_supply/") + e->d_name;
        char type[32] = {0};
        if (FILE* f = std::fopen((base + "/type").c_str(), "r")) {
            if (!std::fgets(type, sizeof(type), f)) type[0] = 0;
            std::fclose(f);
        }
        if (std::strncmp(type, "Battery", 7) != 0) {
            if (readable(base + "/online")) found.chargers.push_back(base + "/online");
            continue;
        }
        if (haveBattery) continue;
        if (readable(base + "/capacity")) found.capacity = base + "/capacity";
        if (readable(base + "/voltage_now")) found.voltage = base + "/voltage_now";
        if (readable(base + "/status")) found.status = base + "/status";
        if (readable(base + "/current_now")) found.current = base + "/current_now";
        haveBattery = !found.capacity.empty() || !found.voltage.empty();
    }
    ::closedir(dir);
    return found;
}

long readLong(const std::string& path) {
    long v = -1;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
        if (std::fscanf(f, "%ld", &v) != 1) v = -1;
        std::fclose(f);
    }
    return v;
}

uint32_t readMillivolts(const std::string& path) {
    const long microvolts = readLong(path);
    return microvolts > 0 ? static_cast<uint32_t>(microvolts / 1000) : 0;
}

/* The gauge's percentage, or -1 when it has none worth believing. */
int readCapacity(const std::string& path) {
    if (path.empty()) return -1;
    const long pct = readLong(path);
    return (pct >= 0 && pct <= 100) ? static_cast<int>(pct) : -1;
}

std::once_flag once;
BatteryFiles files;

/* What the power chip said last, and when. Every read of one of its files is
 * a round of I2C transactions on its bus -- twi5 on the RG40XXV, which the
 * [drain] log showed at 430 interrupts a second -- and this file was asked
 * seven or eight times a second: the firmware's battery task alone samples
 * eight times in a row every two seconds, each sample a capacity read here.
 * The gauge moves a whole percent every few minutes at most, so an answer a
 * second old is the same answer. */
constexpr auto FRESH_FOR = std::chrono::milliseconds(1000);

struct Snapshot {
    int pct = -1;
    bool charging = false;
    bool plugged = false;
};
std::mutex snapshotLock;
Snapshot lastRead;
std::chrono::steady_clock::time_point lastReadAt{};
bool haveRead = false;

/* After `files` has been found. */
Snapshot fresh() {
    std::lock_guard<std::mutex> lock(snapshotLock);
    const auto now = std::chrono::steady_clock::now();
    if (haveRead && now - lastReadAt < FRESH_FOR) return lastRead;
    Snapshot s;
    s.pct = readCapacity(files.capacity);
    if (FILE* f = std::fopen(files.status.c_str(), "r")) {
        char status[24] = {0};
        s.charging = std::fscanf(f, "%23s", status) == 1 && std::strcmp(status, "Charging") == 0;
        std::fclose(f);
    }
    s.plugged = s.charging;
    for (const std::string& online : files.chargers) {
        if (readLong(online) == 1) s.plugged = true;
    }
    lastRead = s;
    lastReadAt = now;
    haveRead = true;
    return s;
}

}   // namespace

namespace HostBattery {

uint32_t cellMillivolts() {
    std::call_once(once, [] {
        files = findBattery();
        /* What was read, not just where: the badge hides itself for anything
         * outside 3.0-4.5 V, so a missing badge is a question this answers. */
        const int pct = readCapacity(files.capacity);
        if (pct >= 0) {
            char status[24] = "unknown";
            if (FILE* f = std::fopen(files.status.c_str(), "r")) {
                if (std::fscanf(f, "%23s", status) != 1) std::strcpy(status, "unknown");
                std::fclose(f);
            }
            std::printf("[battery] host: fuel gauge %s reads %d%% (cell %u mV, %s), "
                        "given to the firmware as %u mV\n",
                        files.capacity.c_str(), pct,
                        static_cast<unsigned>(files.voltage.empty() ? 0 : readMillivolts(files.voltage)),
                        status, static_cast<unsigned>(millivoltsFor(pct)));
        } else if (!files.voltage.empty()) {
            std::printf("[battery] host: no fuel gauge; %s reads %u mV\n", files.voltage.c_str(),
                        static_cast<unsigned>(readMillivolts(files.voltage)));
        } else {
            std::printf("[battery] host: no battery reported\n");
        }
        for (const std::string& online : files.chargers) {
            std::printf("[battery] host: charger %s reads %ld\n", online.c_str(), readLong(online));
        }
        if (files.chargers.empty()) std::printf("[battery] host: no charger supply reported\n");
        std::fflush(stdout);
    });
    const int pct = fresh().pct;
    if (pct >= 0) return millivoltsFor(pct);
    return files.voltage.empty() ? 0 : readMillivolts(files.voltage);
}

Reading read() {
    cellMillivolts();   // finds the files, once
    const Snapshot s = fresh();
    Reading r{s.pct, false, false};
    r.charging = s.charging;
    r.plugged = s.plugged;
    return r;
}

Electrical electrical() {
    cellMillivolts();   // finds the files, once
    Electrical e{INT32_MIN, INT32_MIN};
    if (!files.voltage.empty()) {
        const uint32_t mv = readMillivolts(files.voltage);
        if (mv > 0) e.millivolts = static_cast<int32_t>(mv);
    }
    if (files.current.empty()) return e;
    long raw = 0;
    FILE* f = std::fopen(files.current.c_str(), "r");
    if (!f) return e;
    const bool ok = std::fscanf(f, "%ld", &raw) == 1;
    std::fclose(f);
    if (!ok) return e;
    /* The kernel's unit is microamps, and some PMIC drivers report milliamps
     * regardless. No handheld draws 20 A, and under 20 mA is not a figure
     * anything here turns on, so the size of the number settles which it is. */
    long magnitude = std::labs(raw);
    if (magnitude >= 20000) magnitude /= 1000;
    static std::once_flag said;
    std::call_once(said, [raw, magnitude] {
        std::printf("[power] host: current_now reads %ld, taken as %ld mA\n", raw, magnitude);
    });
    e.milliamps = static_cast<int32_t>(read().charging ? -magnitude : magnitude);
    return e;
}

}   // namespace HostBattery
