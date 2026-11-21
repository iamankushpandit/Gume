// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* i2s_write() into an AAudio stream: host/src/HostI2s.cpp's contract, with
 * Android's native audio in place of SDL's queue.
 *
 * The stream's buffer stands in for the DMA ring and is kept about as deep
 * as the codec board's, 96 ms. AAudioStream_write() blocks while that buffer
 * is full, up to the caller's timeout -- the same back-pressure that paces
 * the braino-audio task on a board, and that keeps a cue from being queued so
 * far ahead that Mute, or a newer cue, takes a quarter of a second to be
 * heard. The firmware's 16 kHz is resampled by the system mixer. */

#include "HostI2s.h"

#include <aaudio/AAudio.h>
#include <driver/i2s.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

namespace {

std::mutex deviceLock;
AAudioStream* stream = nullptr;
bool available = false;   // the first open worked, so a reopen is worth trying
std::atomic<bool> released{false};
std::atomic<uint32_t> opens{0};
int sampleRate = 16000;
std::atomic<uint8_t> volumePercent{100};

constexpr int CHANNELS = 2;

/* With deviceLock held. */
bool openDevice() {
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return false;
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setChannelCount(builder, CHANNELS);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    const aaudio_result_t rc = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (rc != AAUDIO_OK) {
        std::fprintf(stderr, "[audio] %s\n", AAudio_convertResultToText(rc));
        stream = nullptr;
        return false;
    }
    /* 96 ms of buffer, as deep as the codec board's DMA ring. */
    AAudioStream_setBufferSizeInFrames(stream, sampleRate * 96 / 1000);
    if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
        AAudioStream_close(stream);
        stream = nullptr;
        return false;
    }
    ++opens;
    return true;
}

void closeDevice() {
    if (!stream) return;
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
    stream = nullptr;
}

}   // namespace

namespace HostI2s {

bool open(int rate) {
    std::lock_guard<std::mutex> lock(deviceLock);
    sampleRate = rate;
    available = openDevice();
    return available;
}

void setVolume(uint8_t percent) { volumePercent = percent > 100 ? 100 : percent; }

void release() {
    if (released.exchange(true)) return;
    std::thread([] {
        std::lock_guard<std::mutex> lock(deviceLock);
        if (!released) return;
        closeDevice();
    }).detach();
}

void hold() { released = false; }

uint32_t openCount() { return opens; }

}   // namespace HostI2s

esp_err_t i2s_write(i2s_port_t, const void* src, size_t size, size_t* written, TickType_t ticks) {
    *written = 0;
    std::lock_guard<std::mutex> lock(deviceLock);
    HostI2s::hold();
    if (!stream && (!available || !openDevice())) return ESP_FAIL;

    int16_t scaled[1024];
    const auto* in = static_cast<const int16_t*>(src);
    const size_t samples = size / sizeof(int16_t);
    const int32_t vol = volumePercent;
    /* The whole write waits at most `ticks` milliseconds, as the queue did. */
    const int64_t timeoutNs = static_cast<int64_t>(ticks) * 1000000LL;
    for (size_t done = 0; done < samples;) {
        size_t n = samples - done < 1024 ? samples - done : 1024;
        n -= n % CHANNELS;
        if (n == 0) break;
        for (size_t i = 0; i < n; ++i) {
            scaled[i] = static_cast<int16_t>(static_cast<int32_t>(in[done + i]) * vol / 100);
        }
        const int32_t frames = static_cast<int32_t>(n / CHANNELS);
        const aaudio_result_t put = AAudioStream_write(stream, scaled, frames, timeoutNs);
        if (put < 0) {
            /* The output went away under us (a route change): drop the
             * stream; the next write opens a new one. */
            closeDevice();
            return ESP_FAIL;
        }
        done += static_cast<size_t>(put) * CHANNELS;
        *written = done * sizeof(int16_t) < size ? done * sizeof(int16_t) : size;
        if (put < frames) break;   // full until the timeout: the rest is dropped, as before
    }
    return ESP_OK;
}

esp_err_t i2s_zero_dma_buffer(i2s_port_t) {
    std::lock_guard<std::mutex> lock(deviceLock);
    if (stream) {
        /* What is queued is dropped: pause, flush, carry on. */
        AAudioStream_requestPause(stream);
        AAudioStream_requestFlush(stream);
        AAudioStream_requestStart(stream);
    }
    return ESP_OK;
}
