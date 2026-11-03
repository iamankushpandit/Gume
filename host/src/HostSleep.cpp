// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* See HostSleep.h. */

#include "HostSleep.h"

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "HostWifiOs.h"

namespace HostSleep {
namespace {

constexpr int MAX_CPUS = 8;
/* After the lamp, so the black frame the window presents on the way into
 * sleep has reached the panel before its output stops. A page flip waiting
 * on a display that is not scanning would hold the main thread. */
constexpr int LCD_OFF_DELAY_MS = 1000;
/* What muOS waits after enabling the output before trusting it. */
constexpr int LCD_ON_SETTLE_MS = 150;
constexpr const char* DISPDBG = "/sys/kernel/debug/dispdbg";
constexpr const char* RUN_DIR = "/run/braino";
constexpr const char* LCD_MARK = "/run/braino/sleep-lcd-off";
constexpr const char* CORES_MARK = "/run/braino/sleep-cores-off";

void mark(const char* path, bool on) {
    if (on) {
        ::mkdir(RUN_DIR, 0755);
        const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) ::close(fd);
    } else {
        ::unlink(path);
    }
}

struct Config {
    bool wifi = true;
    bool lcd = true;
    bool cores = true;
};

bool switchedOn(const char* name) {
    const char* v = std::getenv(name);
    return !(v && std::strcmp(v, "0") == 0);
}

bool optedIn(const char* name) {
    const char* v = std::getenv(name);
    return v && std::strcmp(v, "1") == 0;
}

const Config& config() {
    static const Config c = [] {
        Config k;
        k.wifi = switchedOn("BRAINO_SLEEP_WIFI");
        k.lcd = optedIn("BRAINO_SLEEP_LCD");   // see HostSleep.h: it broke the panel
        k.cores = switchedOn("BRAINO_SLEEP_CORES");
        std::printf("[sleep] host: asleep turns off wifi=%s lcd=%s cores=%s\n", k.wifi ? "yes" : "no",
                    k.lcd ? "yes" : "no", k.cores ? "yes" : "no");
        std::fflush(stdout);
        return k;
    }();
    return c;
}

bool writeText(const std::string& path, const char* text) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_TRUNC);
    if (fd < 0) return false;
    const ssize_t n = ::write(fd, text, std::strlen(text));
    ::close(fd);
    return n == static_cast<ssize_t>(std::strlen(text));
}

/* ----------------------------------------------------- the display output */

std::mutex lcdMutex;
bool lcdOff = false;
std::atomic<uint32_t> sleepGeneration{0};

/* The Allwinner display driver's debug interface, as muOS's own LCD_DISABLE
 * and LCD_ENABLE drive it: a name, a command, a parameter, then start. */
bool dispdbg(const char* command) {
    if (::access(DISPDBG, F_OK) != 0) {
        ::mount("debugfs", "/sys/kernel/debug", "debugfs", 0, nullptr);
        if (::access(DISPDBG, F_OK) != 0) return false;
    }
    const std::string base(DISPDBG);
    return writeText(base + "/name", "lcd0") && writeText(base + "/command", command) &&
           writeText(base + "/param", "0") && writeText(base + "/start", "1");
}

void lcdOffLater(uint32_t generation) {
    std::thread([generation] {
        std::this_thread::sleep_for(std::chrono::milliseconds(LCD_OFF_DELAY_MS));
        std::lock_guard<std::mutex> lock(lcdMutex);
        if (generation != sleepGeneration || lcdOff) return;   // woken meanwhile
        mark(LCD_MARK, true);   // before, so a crash in between still recovers
        lcdOff = dispdbg("disable");
        if (!lcdOff) mark(LCD_MARK, false);
        std::printf("[sleep] host: display output %s\n", lcdOff ? "off" : "could not be turned off");
        std::fflush(stdout);
    }).detach();
}

void lcdOn() {
    std::lock_guard<std::mutex> lock(lcdMutex);
    if (!lcdOff) return;
    lcdOff = false;
    const bool ok = dispdbg("enable");
    mark(LCD_MARK, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(LCD_ON_SETTLE_MS));
    std::printf("[sleep] host: display output %s\n", ok ? "on" : "did not answer enable");
    std::fflush(stdout);
}

/* ---------------------------------------------------------------- cores */

bool parked[MAX_CPUS] = {false};

void parkCores() {
    mark(CORES_MARK, true);
    int n = 0;
    for (int cpu = 1; cpu < MAX_CPUS; ++cpu) {
        const std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/online";
        if (::access(path.c_str(), W_OK) != 0) continue;
        if (writeText(path, "0")) {
            parked[cpu] = true;
            ++n;
        }
    }
    std::printf("[sleep] host: %d core(s) parked\n", n);
    std::fflush(stdout);
}

void unparkCores() {
    for (int cpu = 1; cpu < MAX_CPUS; ++cpu) {
        if (!parked[cpu]) continue;
        writeText("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/online", "1");
        parked[cpu] = false;
    }
    mark(CORES_MARK, false);
}

}   // namespace

void recover() {
    if (::access(CORES_MARK, F_OK) == 0) {
        for (int cpu = 1; cpu < MAX_CPUS; ++cpu) {
            writeText("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/online", "1");
        }
        mark(CORES_MARK, false);
        std::printf("[sleep] host: brought back the cores a previous run left parked\n");
    }
    if (::access(LCD_MARK, F_OK) == 0) {
        const bool ok = dispdbg("enable");
        mark(LCD_MARK, false);
        std::printf("[sleep] host: display output a previous run left off %s\n",
                    ok ? "is back on" : "did not answer enable");
    }
    std::fflush(stdout);
}

void enter() {
    const Config& c = config();
    const uint32_t generation = ++sleepGeneration;
    if (c.wifi) HostWifiOs::sleepRadio(true);
    if (c.cores) parkCores();
    if (c.lcd) lcdOffLater(generation);
}

void leave() {
    const Config& c = config();
    ++sleepGeneration;   // a pending display-off is now stale
    if (c.cores) unparkCores();
    if (c.lcd) lcdOn();
    if (c.wifi) HostWifiOs::sleepRadio(false);
}

}   // namespace HostSleep
