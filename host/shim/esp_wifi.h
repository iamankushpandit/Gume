// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The access point the host's own radio is on, for System Info. Power save is
 * the operating system's business, so those calls are accepted and ignored. */

#include <cstdint>
#include "esp_system.h"

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    uint8_t primary;
    int8_t rssi;
    uint32_t phy_11b : 1;
    uint32_t phy_11g : 1;
    uint32_t phy_11n : 1;
    uint32_t phy_lr : 1;
} wifi_ap_record_t;

typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t* out);
inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t) { return ESP_OK; }
inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t* type) {
    *type = WIFI_PS_NONE;
    return ESP_OK;
}
inline esp_err_t esp_wifi_restore() { return ESP_OK; }
