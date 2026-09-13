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

#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

/* The seam between the three halves of the audio subsystem.
 *
 * BoardAudio.cpp is the synthesiser -- the script, the oscillator, the
 * resonators, and the task that turns them into samples. BoardAudioBackend.cpp
 * is the hardware underneath it: the ES8311's registers, the I2S peripheral,
 * the amplifier enable and the built-in DAC's idle level. BoardAudioCues.cpp
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

/* See BoardAudio.cpp for why the two backends generate at different rates. */
#if GUME_HAS_AUDIO_DAC
constexpr int AUDIO_RATE = 24000;
#else
constexpr int AUDIO_RATE = 16000;
#endif

/* The longest script the firmware can arm -- the spoken boot phrase sets it. */
constexpr int MAX_SEGMENTS = 36;

constexpr int BLOCK_FRAMES = 128;
constexpr int DMA_BUF_COUNT = 6;
constexpr int DMA_BUF_LEN = 256;

enum class Wave : uint8_t {
    Silence,
    Tone,       // sine, optionally swept from a to b
    Square,     // brighter, for the chiptune-ish cues
    Noise,      // band-passed white noise: a = centre Hz, b = bandwidth Hz
    Voiced,     // formant resonators driven by a monotone impulse train
    Unvoiced,   // the same resonators driven by noise -- fricatives and stops
};

/* One step of a sound. What `a`, `b` and `c` mean depends on the wave, which
 * is the only way to keep a segment at ten bytes; a union with named members
 * would read better and would double the size of the const tables it exists
 * for. The shorthands in BoardAudioCues.cpp are what the cue tables actually
 * use, so no caller sees the positional form. */
struct Segment {
    uint16_t a;
    uint16_t b;
    uint16_t c;
    uint16_t ms;
    uint8_t  amp;    // 0-100, beneath the codec's own volume setting
    Wave     wave;
};

/* ---- state the three files share ---- */

extern bool codecUp;
extern bool dacUp;
extern bool ampOn;
extern bool playing;
extern bool releasing;
extern float gainTarget;
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

#endif  /* GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC */
