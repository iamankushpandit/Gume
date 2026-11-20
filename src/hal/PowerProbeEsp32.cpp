// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The ESP32 boards' answer to PowerProbe.
 *
 * These boards have one voltage divider and no current sensor, so the charge
 * itself comes through PowerMonitor's fallback to the firmware's gauge, and a
 * drain shows up as a percentage slope over tens of minutes. What this adds is
 * the state of the things that are suspected and invisible from anywhere else.
 * There is no charge-status line either (see "No charge detection" in
 * hal/CLAUDE.md), so onCharger stays unknown rather than being inferred. */

#include "PowerProbe.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_wifi.h>

namespace {

class Esp32PowerProbe : public PowerProbe {
public:
    const char* source() const override { return "Cell voltage only"; }

    void read(PowerReading& out) override {
        out.cpuMhz = static_cast<int32_t>(getCpuFrequencyMhz());

        /* Modem sleep off keeps the radio listening continuously, which is the
         * leading suspect for a console that drains overnight with Wi-Fi set. */
        if (WiFi.getMode() == WIFI_OFF) {
            out.addDetail("Wi-Fi radio", "Off");
        } else {
            wifi_ps_type_t ps = WIFI_PS_NONE;
            const bool known = esp_wifi_get_ps(&ps) == ESP_OK;
            out.addDetail("Wi-Fi radio", !known ? "On"
                                         : ps == WIFI_PS_NONE ? "On, no power save"
                                                              : "On, modem sleep");
        }
        /* Up is not the same as advertising: the teardown is deferred while
         * Wi-Fi runs (see BleBeacon in hal/CLAUDE.md). */
        out.addDetail("BT control", esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED
                                        ? "Up" : "Down");
    }
};

}   // namespace

PowerProbe& platformPowerProbe() {
    static Esp32PowerProbe probe;
    return probe;
}
