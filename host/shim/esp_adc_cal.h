// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The ADC calibration BoardPower.cpp applies to its battery reading. On the
 * host, analogRead() on the battery pin already answers in millivolts (see
 * host/src/HostBattery.h), so the conversion is exact: raw IS millivolts. */

#include <cstdint>

typedef enum { ADC_UNIT_1 = 1, ADC_UNIT_2 = 2 } adc_unit_t;
typedef enum {
    ADC_ATTEN_DB_0,
    ADC_ATTEN_DB_2_5,
    ADC_ATTEN_DB_6,
    ADC_ATTEN_DB_11,
    ADC_ATTEN_DB_12 = ADC_ATTEN_DB_11,
} adc_atten_t;
typedef enum { ADC_WIDTH_BIT_12 = 3 } adc_bits_width_t;
typedef enum { ESP_ADC_CAL_VAL_EFUSE_VREF, ESP_ADC_CAL_VAL_EFUSE_TP, ESP_ADC_CAL_VAL_DEFAULT_VREF } esp_adc_cal_value_t;

typedef struct {
    uint32_t vref;
} esp_adc_cal_characteristics_t;

inline esp_adc_cal_value_t esp_adc_cal_characterize(adc_unit_t, adc_atten_t, adc_bits_width_t,
                                                     uint32_t vref,
                                                     esp_adc_cal_characteristics_t* chars) {
    chars->vref = vref;
    return ESP_ADC_CAL_VAL_DEFAULT_VREF;
}
inline uint32_t esp_adc_cal_raw_to_voltage(uint32_t raw, const esp_adc_cal_characteristics_t*) {
    return raw;
}
