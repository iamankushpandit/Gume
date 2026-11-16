// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "Board.h"

#if GUME_HAS_AUDIO

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "AudioSynth.h"

/* The seam between the three halves of the audio subsystem.
 *
 * AudioSynth.cpp is the synthesiser -- the script, the oscillator, the
 * resonators -- and is pure C++, so a PC can render it. BoardAudio.cpp arms
 * it and runs the task that turns it into samples. BoardAudioBackend.cpp
 * is the hardware underneath it: the ES8311's registers, the I2S peripheral,
 * the amplifier enable, the built-in DAC's idle level and the bare I2S
 * amplifier's plain setup. BoardAudioCues.cpp
 * is the vocabulary: every sound the console can make, and the spoken boot
 * phrase, as const tables.
 *
 * The cut is worth stating, because it is what keeps the vocabulary readable:
 * a cue is a const Segment array and `arm()` is the ONLY thing it needs from
 * the other two files. Adding a sound touches one file and no hardware.
 *
 * Everything here was `namespace {` in one 1456-line file. It is a named
 * namespace now only because three translation units have to share it -- it is
 * still private to those three, and nothing outside hal/BoardAudio*.cpp may
 * include this header.
 */
namespace audio {

/* See BoardAudioBackend.cpp for why the backends generate at different
 * rates: 24000 on the built-in DAC, 16000 on the codec and on a bare I2S
 * amplifier. */
#if GUME_HAS_AUDIO_DAC
constexpr int AUDIO_RATE = 24000;
#else
constexpr int AUDIO_RATE = 16000;
#endif

constexpr int BLOCK_FRAMES = 128;
constexpr int DMA_BUF_COUNT = 6;
constexpr int DMA_BUF_LEN = 256;

/* Wave, Segment and MAX_SEGMENTS are AudioSynth.h's. */

/* ---- state the three files share ---- */

extern bool codecUp;
extern bool dacUp;
/* Only a bare-I2S board can bring that backend up, so everywhere else it is
 * a constant and the image carries no flag for it. */
#if GUME_HAS_AUDIO_I2S
extern bool i2sUp;
#else
constexpr bool i2sUp = false;
#endif
extern bool ampOn;
extern uint32_t ampIdleSinceMs;
extern SemaphoreHandle_t audioLock;
extern TaskHandle_t audioTask;
extern uint8_t outputVolume;
extern uint8_t pending[BLOCK_FRAMES * 2 * sizeof(int16_t)];
extern size_t pendingLength;
extern size_t pendingAt;
#if GUME_HAS_AUDIO_DAC
extern int dacIdleLeft;
#endif

/* ---- the synthesiser (BoardAudio.cpp) ---- */

/** Copy a script in and start it. The only thing a cue needs. */
void arm(const Segment* segments, uint8_t count);
/** Keep the note last struck held a segment longer: Sound::NoteHold. */
void holdNote();
size_t generateBlock(uint8_t* dst, int frames);
void startAudioTask();

/* ---- the hardware (BoardAudioBackend.cpp) ---- */

void setAmp(bool on);
void tickAmpTail();
#if GUME_HAS_AUDIO_CODEC
void esWrite(uint8_t reg, uint8_t value);
void applyCodecVolume(uint8_t percent);
#endif
#if GUME_HAS_AUDIO_DAC
void fillDacIdle(TickType_t wait);
#endif

}   // namespace audio

#endif  /* GUME_HAS_AUDIO */
