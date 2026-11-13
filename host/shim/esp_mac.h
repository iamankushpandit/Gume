// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* A host has no eFuse MAC, and the firmware must not be handed a real one
 * from the PC either: the rule in CLAUDE.md is that a hardware identifier
 * never leaves the machine that owns it. esp_read_mac() therefore answers
 * with bytes drawn at random once per run. Board::deviceId() hashes them with
 * everything else and persists the result, so the id is stable across runs
 * the same way it is on a board. */

#include <cstdint>
#include "esp_system.h"

typedef enum {
    ESP_MAC_WIFI_STA,
    ESP_MAC_WIFI_SOFTAP,
    ESP_MAC_BT,
    ESP_MAC_ETH,
} esp_mac_type_t;

esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t type);
esp_err_t esp_efuse_mac_get_default(uint8_t* mac);
