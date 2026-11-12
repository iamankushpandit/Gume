// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The FreeRTOS calls the firmware makes, on std::thread. One tick is one
 * millisecond, as configured on the device.
 *
 * Only what is reached exists. In practice that is the audio task -- which is
 * a real thread here, so the synthesiser runs concurrently with the frame
 * loop exactly as it does on the ESP32 -- and the critical sections around
 * shared snapshots. A critical section is a recursive mutex: on the device it
 * also masks interrupts, which a host has no use for. */

#include <cstdint>
#include <mutex>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void (*TaskFunction_t)(void*);

struct HostTask;
typedef HostTask* TaskHandle_t;

struct HostSemaphore;
typedef HostSemaphore* SemaphoreHandle_t;

struct portMUX_TYPE {
    std::recursive_mutex mutex;
};
#define portMUX_INITIALIZER_UNLOCKED portMUX_TYPE{}
#define portENTER_CRITICAL(mux) ((mux)->mutex.lock())
#define portEXIT_CRITICAL(mux) ((mux)->mutex.unlock())
#define portENTER_CRITICAL_ISR(mux) portENTER_CRITICAL(mux)
#define portEXIT_CRITICAL_ISR(mux) portEXIT_CRITICAL(mux)
#define taskENTER_CRITICAL(mux) portENTER_CRITICAL(mux)
#define taskEXIT_CRITICAL(mux) portEXIT_CRITICAL(mux)

#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define tskNO_AFFINITY 0x7FFFFFFF

#include "freertos/task.h"
#include "freertos/semphr.h"
