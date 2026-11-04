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

#include <dirent.h>
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

#include "HostDrm.h"
#include "HostWifiOs.h"

namespace HostSleep {
namespace {

constexpr int MAX_CPUS = 8;
/* How often a polled input device -- the RG40XX V's joypad -- is read while
 * asleep. A press still wakes the console; it only has to last this long. */
constexpr unsigned SLEEP_POLL_MS = 50;
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
/* Holds each slowed device's own interval, so a restart can put it back. */
constexpr const char* POLL_MARK = "/run/braino/sleep-poll";

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
    bool input = true;
};

bool switchedOn(const char* name) {
    const char* v = std::getenv(name);
    return !(v && std::strcmp(v, "0") == 0);
}

#if !GUME_HOST_DRM
bool optedIn(const char* name) {
    const char* v = std::getenv(name);
    return v && std::strcmp(v, "1") == 0;
}
#endif

const Config& config() {
    static const Config c = [] {
        Config k;
        k.wifi = switchedOn("BRAINO_SLEEP_WIFI");
#if GUME_HOST_DRM
        k.lcd = switchedOn("BRAINO_SLEEP_LCD");   // the mainline driver's CRTC, not muOS's lcd0
#else
        k.lcd = optedIn("BRAINO_SLEEP_LCD");   // see HostSleep.h: it broke the panel
#endif
        k.cores = switchedOn("BRAINO_SLEEP_CORES");
        k.input = switchedOn("BRAINO_SLEEP_INPUT");
        std::printf("[sleep] host: asleep turns off wifi=%s lcd=%s cores=%s, slows input=%s\n",
                    k.wifi ? "yes" : "no", k.lcd ? "yes" : "no", k.cores ? "yes" : "no",
                    k.input ? "yes" : "no");
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

long readNumber(const std::string& path) {
    long v = -1;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
        if (std::fscanf(f, "%ld", &v) != 1) v = -1;
        std::fclose(f);
    }
    return v;
}

/* ------------------------------------------------------ polled input devices */

/* A polled input device is read on a timer whether or not anything is
 * pressed. The RG40XX V's joypad is one: every 10 ms, three ADC conversions,
 * about 300 interrupts a second, and the largest single thing waking a
 * sleeping console. The kernel's input poller lets its interval be changed in
 * sysfs (`poll`, bounded by `max`); asleep it is set to SLEEP_POLL_MS. */
void slowInputs() {
    DIR* dir = ::opendir("/sys/class/input");
    if (!dir) return;
    std::string saved;
    int n = 0;
    while (dirent* e = ::readdir(dir)) {
        if (std::strncmp(e->d_name, "input", 5) != 0) continue;
        const std::string base = std::string("/sys/class/input/") + e->d_name;
        const long now = readNumber(base + "/poll");
        const long max = readNumber(base + "/max");
        if (now <= 0 || max <= 0 || now >= static_cast<long>(SLEEP_POLL_MS)) continue;
        const long slow = max < static_cast<long>(SLEEP_POLL_MS) ? max : SLEEP_POLL_MS;
        if (slow <= now) {
            std::printf("[sleep] host: %s polls every %ld ms and allows no slower\n", e->d_name, now);
            continue;
        }
        if (writeText(base + "/poll", std::to_string(slow).c_str())) {
            saved += base + "/poll " + std::to_string(now) + "\n";
            ++n;
        }
    }
    ::closedir(dir);
    if (saved.empty()) return;
    ::mkdir(RUN_DIR, 0755);
    if (FILE* f = std::fopen(POLL_MARK, "w")) {
        std::fputs(saved.c_str(), f);
        std::fclose(f);
    }
    std::printf("[sleep] host: %d input device(s) polled every %u ms\n", n, SLEEP_POLL_MS);
    std::fflush(stdout);
}

/* Puts back every interval slowInputs() saved; true if there were any. */
bool restoreInputs() {
    FILE* f = std::fopen(POLL_MARK, "r");
    if (!f) return false;
    char path[128];
    long interval = 0;
    while (std::fscanf(f, "%127s %ld", path, &interval) == 2) {
        if (interval > 0) writeText(path, std::to_string(interval).c_str());
    }
    std::fclose(f);
    ::unlink(POLL_MARK);
    return true;
}

/* ----------------------------------------------------- the display output */

std::mutex lcdMutex;
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

#if !GUME_HOST_DRM   // the DRM build switches its own output (HostDrm::setPower)
bool lcdOff = false;

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
#endif

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
    if (restoreInputs()) {
        std::printf("[sleep] host: input polling a previous run left slowed is back to normal\n");
    }
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
    if (c.input) slowInputs();
#if GUME_HOST_DRM
    /* The lamp is already off, so nothing the output shows on its way down
     * can be seen, and this is the caller's thread -- the one that draws. */
    (void)generation;
    if (c.lcd) HostDrm::setPower(false);
#else
    if (c.lcd) lcdOffLater(generation);
#endif
}

void leave() {
    const Config& c = config();
    ++sleepGeneration;   // a pending display-off is now stale
    if (c.cores) unparkCores();
    if (c.input) restoreInputs();
#if GUME_HOST_DRM
    if (c.lcd) HostDrm::setPower(true);
#else
    if (c.lcd) lcdOn();
#endif
    if (c.wifi) HostWifiOs::sleepRadio(false);
}

}   // namespace HostSleep
