// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The host's answer to SystemSuspend (src/hal/SystemSuspend.h): suspend to
 * RAM through /sys/power/state, woken by the power button.
 *
 * Only a handheld build suspends (GUME_HOST_SCREEN_POWER, on whenever
 * cross-compiling): a desktop build that put the computer to sleep because a
 * window had been idle would be a bad surprise. BRAINO_FAKE_SUSPEND=1 makes
 * any build pretend, three seconds at a time, so the Settings row and the
 * runtime's flow can be seen without a handheld. BRAINO_SUSPEND=0 turns it off
 * on a handheld.
 *
 * Three things around the write itself are load-bearing:
 *
 * - **The power press that wakes the console must not also turn it off.** The
 *   Braino image's hotkeys.sh turns the console off on POWER_SHORT, and the
 *   press that ended the suspend reaches muhotkey too. So /run/braino/resumed
 *   is stamped just before suspending and again on waking, and hotkeys.sh
 *   ignores the power key within five seconds of that stamp.
 * - **The beacon and the scan stop first.** The Bluetooth controller is its
 *   own chip and can keep advertising and listening through a suspend, which
 *   is battery spent for a console that cannot answer. Both come back as they
 *   were on waking.
 * - **On the charger it declines.** The battery is not the question there, and
 *   the light ring is showing the charge, which a suspended console cannot. */

#include "hal/SystemSuspend.h"

#include <NimBLEDevice.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "HostBattery.h"

namespace {

constexpr const char* POWER_STATE = "/sys/power/state";
constexpr const char* RESUMED_STAMP = "/run/braino/resumed";
constexpr int FAKE_SECONDS = 3;

bool envIs(const char* name, const char* value) {
    const char* v = std::getenv(name);
    return v && std::strcmp(v, value) == 0;
}

bool fake() {
    static const bool f = envIs("BRAINO_FAKE_SUSPEND", "1");
    return f;
}

bool kernelOffersMem() {
    FILE* f = std::fopen(POWER_STATE, "r");
    if (!f) return false;
    char line[128] = {0};
    const bool read = std::fgets(line, sizeof(line), f) != nullptr;
    std::fclose(f);
    return read && std::strstr(line, "mem") != nullptr && ::access(POWER_STATE, W_OK) == 0;
}

void stampResumed() {
    ::mkdir("/run/braino", 0755);
    const int fd = ::open(RESUMED_STAMP, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) ::close(fd);
    ::utimensat(AT_FDCWD, RESUMED_STAMP, nullptr, 0);   // now, even if it existed
}

long bootSeconds() {
    timespec ts{};
    ::clock_gettime(CLOCK_BOOTTIME, &ts);   // counts the time suspended
    return static_cast<long>(ts.tv_sec);
}

void flushLog() {
    std::fflush(stdout);
    ::fsync(STDOUT_FILENO);
}

}   // namespace

namespace SystemSuspend {

bool supported() {
    static const bool can = [] {
        if (fake()) return true;
#if defined(GUME_HOST_SCREEN_POWER)
        if (envIs("BRAINO_SUSPEND", "0")) return false;
        const bool ok = kernelOffersMem();
        std::printf("[suspend] host: %s\n", ok ? "suspend to RAM available"
                                                : "no suspend to RAM here; Deep sleep not offered");
        std::fflush(stdout);
        return ok;
#else
        return false;
#endif
    }();
    return can;
}

bool enter() {
    if (!supported()) return false;
    /* Not when pretending: a PC has no battery and always reads as plugged
     * in, so the pretend flow could never be seen past this line. */
    if (!fake() && HostBattery::read().plugged) {
        std::printf("[suspend] host: on the charger, staying up\n");
        std::fflush(stdout);
        return false;
    }

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    NimBLEScan* scan = NimBLEDevice::getScan();
    const bool wasAdvertising = NimBLEDevice::getInitialized() && advertising->isAdvertising();
    const bool wasScanning = NimBLEDevice::getInitialized() && scan->isScanning();
    if (wasAdvertising) advertising->stop();
    if (wasScanning) scan->stop();

    const long before = bootSeconds();
    bool ok = true;
    if (fake()) {
        std::printf("[suspend] host: pretending to suspend for %d s\n", FAKE_SECONDS);
        flushLog();
        std::this_thread::sleep_for(std::chrono::seconds(FAKE_SECONDS));
    } else {
        stampResumed();
        std::printf("[suspend] host: suspending to RAM; the power button wakes it\n");
        flushLog();
        ::sync();
        /* The write returns once the device has woken. */
        const int fd = ::open(POWER_STATE, O_WRONLY);
        ok = fd >= 0 && ::write(fd, "mem", 3) == 3;
        if (fd >= 0) ::close(fd);
        stampResumed();
    }
    const long slept = bootSeconds() - before;

    if (wasAdvertising) advertising->start();
    if (wasScanning) scan->start(0, nullptr, false);

    if (ok) {
        std::printf("[suspend] host: woke after %ld s\n", slept);
    } else {
        std::printf("[suspend] host: the kernel refused to suspend (%s)\n", std::strerror(errno));
    }
    std::fflush(stdout);
    return ok;
}

const char* wakeHint() { return "power"; }

}   // namespace SystemSuspend
