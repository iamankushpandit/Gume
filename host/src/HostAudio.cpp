// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The audio backend for the host: what BoardAudioBackend.cpp is on a board,
 * with an SDL audio device where the ES8311 and its I2S link would be.
 *
 * Everything that makes a sound -- the cue scripts, the synthesiser, the
 * braino-audio task, the mute gate -- is the firmware's own, compiled from
 * src/hal/BoardAudio.cpp and BoardAudioCues.cpp unchanged. This file only
 * brings the device up, holds the volume and answers the codec's register
 * writes, which here have nothing to land on. */

#include "hal/BoardAudioInternal.h"

#include <driver/i2s.h>

#include "HostI2s.h"

namespace audio {

constexpr uint32_t AMP_TAIL_MS = 2000;

/* The device is the amplifier here (see HostI2s::release()), so the
 * firmware's own two-second tail is what closes it. */
void setAmp(bool on) {
    if (ampOn == on) return;
    ampOn = on;
    if (on) {
        HostI2s::hold();
    } else {
        HostI2s::release();
    }
}

void tickAmpTail() {
    if (ampIdleSinceMs == 0) {
        ampIdleSinceMs = millis();
    } else if (millis() - ampIdleSinceMs >= AMP_TAIL_MS) {
        setAmp(false);
    }
}

void esWrite(uint8_t, uint8_t) {}

/* On the codec this is a logarithmic register driven from a percentage of
 * amplitude, 100 being unity. The host applies the same percentage to the
 * samples as they leave, which is that amplitude without the register. */
void applyCodecVolume(uint8_t percent) { HostI2s::setVolume(percent); }

}   // namespace audio

using namespace audio;

void Board::beginAudio() {
    if (!HostI2s::open(AUDIO_RATE)) {
        Serial.println("[audio] no host audio device; running silent");
        return;
    }
    codecUp = true;
    applyCodecVolume(volume());
    Serial.printf("[audio] host device up, %d Hz\n", AUDIO_RATE);
    startAudioTask();
    /* Opened once to know there is a device; closed until something sounds,
     * or a console that boots muted would keep it running for good. */
    HostI2s::release();
}

/* The task drains the synthesiser; this is the firmware's fallback for when
 * it could not be created, kept the same shape as on a board. */
void Board::tickAudio() {
    if (!codecUp) return;
    if (audioTask != nullptr) return;
    if (audioLock != nullptr && xSemaphoreTake(audioLock, 0) != pdTRUE) return;
    for (;;) {
        if (pendingAt >= pendingLength) {
            const size_t bytes = generateBlock(pending, BLOCK_FRAMES);
            if (bytes == 0) break;
            pendingLength = bytes;
            pendingAt = 0;
        }
        size_t written = 0;
        if (i2s_write(I2S_NUM_0, pending + pendingAt, pendingLength - pendingAt, &written, 0) !=
            ESP_OK) {
            break;
        }
        pendingAt += written;
        if (pendingAt < pendingLength) break;
    }
    if (!playing && pendingAt >= pendingLength) tickAmpTail();
    if (audioLock != nullptr) xSemaphoreGive(audioLock);
}

uint8_t Board::volume() {
    if (!volumeCached_) {
        uint8_t stored = prefs_.getUChar("sndVol", AUDIO_VOLUME_DEFAULT);
        if (stored > AUDIO_VOLUME_MAX) stored = AUDIO_VOLUME_MAX;
        cachedVolume_ = stored;
        volumeCached_ = true;
    }
    return cachedVolume_;
}

void Board::setVolume(uint8_t percent) {
    if (percent > AUDIO_VOLUME_MAX) percent = AUDIO_VOLUME_MAX;
    cachedVolume_ = percent;
    volumeCached_ = true;
    prefs_.putUChar("sndVol", percent);
    if (codecUp) applyCodecVolume(percent);
}
