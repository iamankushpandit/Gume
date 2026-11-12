// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* Watchdog for the host: the frame statistics, without the reboot.
 *
 * On a board the hardware watchdog restarts a hung loop so a player is never
 * left at a frozen screen. A host has a window manager and a terminal for
 * that, and a debugger stopped at a breakpoint must not be "rescued". What is
 * kept is the part System Info shows -- frames, worst frame, worst work
 * slice -- because those numbers are as true here as anywhere. */

#include "hal/Watchdog.h"

#include <cstring>

namespace Watchdog {
namespace {
Stats stats_;
LastRun lastRun_;
char context_[CONTEXT_MAX] = "boot";
uint32_t lastFeedMs = 0;
uint32_t bootMs = 0;
int pauseDepth = 0;
}   // namespace

void begin() {
    bootMs = millis();
    stats_.bootCount = 1;
    stats_.freeHeap = ESP.getFreeHeap();
    stats_.minFreeHeap = ESP.getMinFreeHeap();
    stats_.largestBlock = ESP.getMaxAllocHeap();
}

void feed() {
    const uint32_t now = millis();
    if (lastFeedMs != 0 && pauseDepth == 0) {
        stats_.lastFrameMs = now - lastFeedMs;
        if (stats_.lastFrameMs > stats_.maxFrameMs) stats_.maxFrameMs = stats_.lastFrameMs;
        if (stats_.lastFrameMs > STALL_WARN_MS) ++stats_.stalls;
    }
    lastFeedMs = now;
    ++stats_.loops;
    stats_.uptimeSeconds = (now - bootMs) / 1000;
}

void recordFrameWork(uint32_t workMs) {
    stats_.lastWorkMs = workMs;
    if (workMs > stats_.maxWorkMs) stats_.maxWorkMs = workMs;
}

void setContext(const char* tag) {
    std::strncpy(context_, tag ? tag : "", CONTEXT_MAX - 1);
    context_[CONTEXT_MAX - 1] = '\0';
}

const char* context() { return context_; }

void pause() { ++pauseDepth; }

void resume() {
    if (pauseDepth > 0) --pauseDepth;
    lastFeedMs = millis();   // a paused stretch is not a slow frame
}

void noteScreenLeft(const char*, uint32_t) {}

uint8_t heapFragmentation() { return 0; }

Stats stats() { return stats_; }

const LastRun& lastRun() { return lastRun_; }

const char* resetReasonText(int) { return "Host start"; }

void printReport() {
    Serial.printf("[wdt] host build: frames=%lu worst=%lums work=%lums\n",
                  static_cast<unsigned long>(stats_.loops),
                  static_cast<unsigned long>(stats_.maxFrameMs),
                  static_cast<unsigned long>(stats_.maxWorkMs));
}

}   // namespace Watchdog
