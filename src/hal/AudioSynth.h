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
    Note,       // struck and left to ring: a = Hz, b = Hz it glides to, c = Timbre
};

/* What a Note is played on: how it starts, how it dies away, and how it
 * glides. A Note segment names one in `c`. Every timbre here is a pure sine
 * except the Bell's, which carries one soft octave above it so it can be heard
 * through a cushion -- this is the "smooth" set the owner chose in the
 * listening page on 2026-11-15. Add at the end: the number is what the cue
 * tables store. */
enum Timbre : uint16_t {
    PURE,      // over in a few tens of milliseconds: a tap, a tick
    SOFT,      // a slower attack and a longer fall, for the glides
    GLOW,      // between the two, for runs of notes that bloom into a chord
    RING,      // left to ring, for the last note of a fanfare or a doorbell
    SWISH,     // a slow swell and a fast fall, gliding on its own clock
    PAD,       // Cinnamon's pads: round, and gone before the pad goes dark
    KEYS,      // the piano: holds while a key is down, falls when it comes up
    BELL,      // the find alert: rings long, with a soft octave for carry
    TICK,      // shorter than PURE: a Ludo hop, gone well inside 110ms
    TIMBRE_COUNT
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
 *  stopping it dead. Notes still ringing fade in a few milliseconds. */
void synthFadeOut();

/** Keep the note last struck held a segment longer, without striking it
 *  again: a piano key still down. Does nothing once the note is let go. */
void synthHold();

/** The level below which a ringing note is finished. On the 8-bit DAC a note
 *  left to fall further only gets coarser, so that backend ends it sooner. */
void synthSetNoteFloor(float floor);

/** Generate one sample. False once the script and its release are spent. */
bool nextSample(int16_t& out);

/* For the host test only: the envelope and the oscillator, as they stand
 * after the last sample. Nothing on the device reads them. */
float synthGain();
float synthPhase();
/** How many notes are ringing just now. */
int synthNotesSounding();

}   // namespace audio
