// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>

/* The synthesiser itself: a script of segments in, one sample at a time out.
 *
 * Pure C++ -- no Arduino, no FreeRTOS, no board -- for the same reason
 * PowerLedger is: what a cue sounds like is arithmetic, and arithmetic can be
 * run off the device. test/host/audio_synth_test.cpp renders every shape of
 * cue through this file and fails on a step in the waveform, which is the
 * only way a click can be found before somebody hears it.
 *
 * Everything around it -- the lock, the task, the DMA, the amplifier, the
 * word format each backend wants -- stays in BoardAudio.cpp and
 * BoardAudioBackend.cpp. The cue tables stay in BoardAudioCues.cpp. Nothing
 * here knows which board it is on: the sample rate is handed in.
 *
 * Not thread-safe on its own. On the device every call is made under
 * `audioLock`.
 */
namespace audio {

/* The longest script the firmware can arm -- the spoken boot phrase sets it. */
constexpr int MAX_SEGMENTS = 36;

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

/* True from load() until the last sample of the script and its fade-out has
 * been generated. */
extern bool playing;

/** The rate every later sample is generated at. Cheap when unchanged. */
void synthSetRate(int samplesPerSecond);

/** Copy a script in and start it. A script identical to the one still
 *  sounding is a continuation -- see the comment at the definition. */
void synthLoad(const Segment* segments, uint8_t count);

/** Fade whatever is sounding to silence over the release, rather than
 *  stopping it dead. */
void synthFadeOut();

/** Generate one sample. False once the script and its release are spent. */
bool nextSample(int16_t& out);

/* For the host test only: the envelope and the oscillator, as they stand
 * after the last sample. Nothing on the device reads them. */
float synthGain();
float synthPhase();

}   // namespace audio
