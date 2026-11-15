// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The boards' answer to SleepPower. See SleepPower.h for why each of these is
 * what it is; the comments here are about the calls themselves. */

#include "SleepPower.h"

#include <Arduino.h>
#include <esp_wifi.h>

namespace SleepPower {

namespace {
/* The clock to go back to, or 0 while it has not been lowered. Kept rather
 * than assumed to be 240, because a board's default is the build's choice. */
uint32_t savedCpuMhz = 0;
}  // namespace

void lowerClock() {
    if (savedCpuMhz != 0) return;
    const uint32_t current = getCpuFrequencyMhz();
    if (current <= ASLEEP_CPU_MHZ) return;
    if (setCpuFrequencyMhz(ASLEEP_CPU_MHZ)) {
        savedCpuMhz = current;
        Serial.printf("[sleep] cpu %u -> %u MHz\n",
                      (unsigned)current, (unsigned)ASLEEP_CPU_MHZ);
    }
}

void restoreClock() {
    if (savedCpuMhz == 0) return;
    setCpuFrequencyMhz(savedCpuMhz);
    Serial.printf("[sleep] cpu back to %u MHz\n", (unsigned)savedCpuMhz);
    savedCpuMhz = 0;
}

bool stopStation() {
    /* Not initialised (no network was ever configured, or the Wi-Fi screen
     * left it off) answers an error here, and there is nothing to stop. */
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK || (mode & WIFI_MODE_STA) == 0) {
        return false;
    }
    /* A disconnect we asked for arrives as ASSOC_LEAVE, which is the one
     * reason the core never answers with a reconnect of its own. */
    esp_wifi_disconnect();
    /* esp_wifi_stop(), not WiFi.mode(WIFI_OFF): the core's mode() follows the
     * stop with esp_wifi_deinit(), and this keeps the driver. The core's own
     * record that the radio was started is left set on purpose, so that the
     * next WiFi.mode(WIFI_STA) still returns early and nothing in the core
     * tries to start it a second time behind our back. */
    return esp_wifi_stop() == ESP_OK;
}

bool startStation() {
    /* STA_START, which this raises, is where the core re-applies modem sleep
     * (WiFi.setSleep()'s stored choice) and resets the status to
     * disconnected. */
    return esp_wifi_start() == ESP_OK;
}

}  // namespace SleepPower
