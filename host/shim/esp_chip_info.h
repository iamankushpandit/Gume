// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once
#include <cstdint>

typedef enum { CHIP_ESP32 = 1, CHIP_HOST = 0xFF } esp_chip_model_t;
#define CHIP_FEATURE_WIFI_BGN (1 << 1)
#define CHIP_FEATURE_BLE (1 << 4)
#define CHIP_FEATURE_BT (1 << 5)

typedef struct {
    esp_chip_model_t model;
    uint32_t features;
    uint16_t revision;
    uint8_t cores;
} esp_chip_info_t;

inline void esp_chip_info(esp_chip_info_t* info) {
    info->model = CHIP_HOST;
    info->features = 0;
    info->revision = 0;
    info->cores = 1;
}
