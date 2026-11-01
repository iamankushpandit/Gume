// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The I2S driver as the synthesiser's codec path uses it, played into an SDL
 * audio device by host/src/HostI2s.cpp. i2s_write() blocks while the device
 * queue is full -- the same back-pressure the DMA gives on a board, which is
 * what paces the braino-audio task. */

#include <cstddef>
#include <cstdint>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

typedef enum { I2S_NUM_0 = 0, I2S_NUM_1 = 1 } i2s_port_t;

esp_err_t i2s_write(i2s_port_t port, const void* src, size_t size, size_t* written,
                    TickType_t ticks);
esp_err_t i2s_zero_dma_buffer(i2s_port_t port);
