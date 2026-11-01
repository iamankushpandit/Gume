// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* FreeRTOS on std::thread. See host/shim/freertos/FreeRTOS.h.
 *
 * Priorities and core pinning are accepted and ignored: a desktop scheduler
 * has plenty of cores, and the reason the audio task is pinned above the loop
 * on a board -- to preempt a long SPI repaint -- does not arise when the two
 * are simply on different cores. */

#include <freertos/FreeRTOS.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

struct HostTask {
    std::mutex mutex;
    std::condition_variable cv;
    uint32_t notifications = 0;
};

struct HostSemaphore {
    std::timed_mutex mutex;
};

namespace {
thread_local HostTask* currentTask = nullptr;
const auto startTime = std::chrono::steady_clock::now();
}   // namespace

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t, void* arg,
                                   UBaseType_t, TaskHandle_t* handle, BaseType_t) {
    /* Tasks here never end (the firmware's do not either), so the handle is
     * deliberately never freed: it must outlive the thread that holds it. */
    auto* task = new HostTask();
    if (handle) *handle = task;
    std::thread([fn, arg, task] {
        currentTask = task;
        fn(arg);
    }).detach();
    return pdPASS;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                       UBaseType_t priority, TaskHandle_t* handle) {
    return xTaskCreatePinnedToCore(fn, name, stack, arg, priority, handle, tskNO_AFFINITY);
}

void vTaskDelete(TaskHandle_t) {}

void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

TickType_t xTaskGetTickCount() {
    return static_cast<TickType_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - startTime)
                                       .count());
}

TaskHandle_t xTaskGetCurrentTaskHandle() { return currentTask; }

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 1024; }

void xTaskNotifyGive(TaskHandle_t task) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lock(task->mutex);
        ++task->notifications;
    }
    task->cv.notify_one();
}

uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticks) {
    HostTask* task = currentTask;
    if (!task) {
        vTaskDelay(ticks == portMAX_DELAY ? 1 : ticks);
        return 0;
    }
    std::unique_lock<std::mutex> lock(task->mutex);
    auto ready = [task] { return task->notifications > 0; };
    if (ticks == portMAX_DELAY) {
        task->cv.wait(lock, ready);
    } else {
        task->cv.wait_for(lock, std::chrono::milliseconds(ticks), ready);
    }
    const uint32_t value = task->notifications;
    if (value > 0) task->notifications = clearOnExit ? 0 : value - 1;
    return value;
}

SemaphoreHandle_t xSemaphoreCreateMutex() { return new HostSemaphore(); }

SemaphoreHandle_t xSemaphoreCreateBinary() { return new HostSemaphore(); }

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) {
    if (!sem) return pdFALSE;
    if (ticks == portMAX_DELAY) {
        sem->mutex.lock();
        return pdTRUE;
    }
    return sem->mutex.try_lock_for(std::chrono::milliseconds(ticks)) ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    if (!sem) return pdFALSE;
    sem->mutex.unlock();
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t sem) { delete sem; }
