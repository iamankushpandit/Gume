// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* i2s_write() into an SDL audio queue.
 *
 * The queue stands in for the DMA ring, and it is kept about as deep: the
 * codec board holds 6 x 256 frames at 16 kHz, 96 ms. A write blocks while
 * the queue is fuller than that, which is the back-pressure that paces the
 * braino-audio task on a board -- and keeps a cue from being queued so far
 * ahead that Mute, or a newer cue, takes a quarter of a second to be heard. */

#include "HostI2s.h"

#include <SDL.h>
#include <driver/i2s.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace {

SDL_AudioDeviceID device = 0;
int sampleRate = 16000;
std::atomic<uint8_t> volumePercent{100};

uint32_t queueLimitBytes() {
    return static_cast<uint32_t>(sampleRate * 2 * sizeof(int16_t) * 96 / 1000);
}

}   // namespace

namespace HostI2s {

bool open(int rate) {
    SDL_AudioSpec want{};
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 256;
    SDL_AudioSpec have{};
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device == 0) {
        SDL_Log("[audio] %s", SDL_GetError());
        return false;
    }
    sampleRate = rate;
    SDL_PauseAudioDevice(device, 0);
    return true;
}

void setVolume(uint8_t percent) { volumePercent = percent > 100 ? 100 : percent; }

}   // namespace HostI2s

esp_err_t i2s_write(i2s_port_t, const void* src, size_t size, size_t* written, TickType_t ticks) {
    *written = 0;
    if (device == 0) return ESP_FAIL;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ticks);
    while (SDL_GetQueuedAudioSize(device) > queueLimitBytes()) {
        if (ticks == 0 || std::chrono::steady_clock::now() >= deadline) return ESP_OK;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    int16_t scaled[1024];
    const auto* in = static_cast<const int16_t*>(src);
    const size_t samples = size / sizeof(int16_t);
    const int32_t vol = volumePercent;
    for (size_t done = 0; done < samples;) {
        const size_t n = samples - done < 1024 ? samples - done : 1024;
        for (size_t i = 0; i < n; ++i) {
            scaled[i] = static_cast<int16_t>(static_cast<int32_t>(in[done + i]) * vol / 100);
        }
        SDL_QueueAudio(device, scaled, static_cast<uint32_t>(n * sizeof(int16_t)));
        done += n;
    }
    *written = size;
    return ESP_OK;
}

esp_err_t i2s_zero_dma_buffer(i2s_port_t) {
    if (device != 0) SDL_ClearQueuedAudio(device);
    return ESP_OK;
}
