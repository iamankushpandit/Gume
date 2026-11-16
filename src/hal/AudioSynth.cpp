// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "AudioSynth.h"

#include <math.h>
#include <string.h>

/* The per-sample half of the sound engine, kept apart from BoardAudio.cpp so
 * that it can be rendered on a PC. The reasoning behind each constant is
 * beside the constant; the struck notes the cues are made of are under
 * "notes" below. */
namespace audio {

namespace {

constexpr double PI_D = 3.14159265358979323846;

Segment script[MAX_SEGMENTS];
uint8_t scriptLength = 0;
uint8_t scriptAt = 0;
int32_t segTotal = 0;        // samples in the segment being played
int32_t segLeft = 0;         // samples of it still to generate
/* The script is spent and the last segment is fading out. See nextSample(). */
bool releasing = false;

/* A REST IS FADED INTO, NOT FALLEN INTO.
 *
 * A Silence segment used to output zero from its first sample. The gain
 * slewed towards the rest's level of 0 perfectly smoothly -- and multiplied
 * a waveform that was already 0, so the slew did nothing and the sound
 * stopped dead wherever its waveform stood. A sine stopped at its crest is a
 * step of the whole amplitude; Wrong's first buzz is 160 Hz at amp 100, so
 * that step was 30,000 of 32,000 counts. Every cue with a rest in it clicked
 * at the rest -- Wrong, Your Turn, High Score and the Bell -- and so did the
 * spoken phrase at its pauses, and at its very end, which is a rest.
 *
 * So a rest keeps the last sounding segment's waveform running, at that
 * segment's final pitch, while the gain fades to the rest's level, exactly as
 * the release at the end of a script does. `voiceAt` is that segment. By the
 * time the rest is over the gain is a few parts in a hundred thousand, so the
 * next sound starts from silence as it always did. */
uint8_t voiceAt = 0;
bool hasVoice = false;

/* A two-pole resonator. The input coefficient is sin(theta) rather than the
 * textbook (1 - b1 - b2): that normalises the PEAK of the impulse response to
 * roughly 1.0 whatever the centre frequency. Without it F2 comes out several
 * times louder than F1 purely because it sits higher up, and the vowels stop
 * being vowels. */
struct Resonator {
    float in = 0.0f, b1 = 0.0f, b2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

Resonator formant[3];
float phase = 0.0f;          // normalised 0-1, for Tone and Square
float gain = 0.0f;           // slewed towards the current segment's amplitude
float gainTarget = 0.0f;
int pitchCounter = 0;
uint32_t noiseState = 0x13579BDFu;

/* The rate, and the three things derived from it. */
int rateInt = 0;
float rate = 16000.0f;
int voicePeriod = 0;
float gainSlew = 0.0f;

/* A monotone excitation is what makes the voice robotic rather than merely
 * synthetic. Real speech moves its pitch on every syllable; holding it
 * perfectly still is the entire effect, and it costs nothing to do. */
constexpr float VOICE_PITCH_HZ = 112.0f;

/* Formant bandwidth, and it is a LOUDNESS control as much as a timbre one.
 *
 * The excitation is an impulse train: one full-scale sample every 143, and
 * silence in between. What fills that gap is the resonators ringing, and how
 * long they ring is set by this. At 90 Hz the ring had decayed to a tenth
 * before the next pulse arrived, so most of a vowel was near-silence and the
 * speech was far quieter than its peak amplitude suggested. Narrowing to 60 Hz
 * roughly doubles the ring time, which raises the average level without
 * touching the peak -- the resonator's input coefficient normalises peak
 * response, so this is close to free.
 *
 * It also sounds more like a voice: a real vocal tract's F1 bandwidth is
 * 50-80 Hz. Do not narrow it much further, or the formants start to whistle. */
constexpr float FORMANT_BANDWIDTH = 60.0f;

/* Amplitude reaches a new segment's level over about 4ms. On a small driver
 * the click of an abrupt start is louder than the note that follows it, and
 * in speech an abrupt boundary between two phonemes is audible as a tick.
 * Slewing one gain fixes both, and means no segment needs its own envelope
 * or its own ramp length. */
constexpr float GAIN_SLEW_SECONDS = 0.004f;

/* THE END OF A SOUND IS FADED TOO, AND FOR THE SAME REASON.
 *
 * The slew above only ever ran towards a segment's level: when a script ran
 * out, the next sample was simply zero, wherever the waveform happened to be.
 * A sine stopped at its crest is a step of the whole amplitude, which is a
 * click -- and it was recorded on the device as one at the end of every piano
 * note, about 300ms after the one at its start. So a finished script keeps its
 * last segment's waveform running while the gain slews to zero, and only
 * stops once it is below this. Six time constants of the 4ms slew, ~25ms,
 * and at OUTPUT_SCALE the residue is under a quarter of one step of the 8-bit
 * DAC: inaudible by construction rather than by ear. */
constexpr float RELEASE_FLOOR = 0.002f;

/* Noise through a resonator is far quieter than an impulse train through the
 * same one, because its energy is spread rather than concentrated at the
 * pitch harmonics. This is a fudge factor and is meant to be one; it is the
 * knob to turn if the fricatives are lost under the vowels. */
constexpr float NOISE_MAKEUP = 5.0f;

/* Full-scale amplitude for a segment at amp 100, and the hard ceiling.
 *
 * These are close together on purpose: this is a 1-inch driver behind a
 * plastic case, and headroom that is never used is just quietness. A cue at
 * amp 100 is meant to reach the clip point and a little past it -- the mild
 * squaring that results adds harmonics, which on a speaker this small is
 * audibly LOUDER rather than audibly distorted. The cue tables sit under 100
 * so that the loud ones have somewhere to be loud relative to the quiet ones,
 * which is where the actual dynamic range lives. */
constexpr float OUTPUT_SCALE = 32000.0f;
constexpr int32_t OUTPUT_CLIP = 32000;

float whiteNoise() {
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return static_cast<int32_t>(noiseState) * (1.0f / 2147483648.0f);
}

void tuneResonator(Resonator& r, float hz, float bandwidthHz) {
    /* Above about 7.5kHz there is no resonator worth having at this sample
     * rate, and a formant slot a phoneme does not use is passed as 0. Both
     * become silence rather than a filter ringing at Nyquist. */
    if (hz <= 20.0f || hz >= rate * 0.47f) {
        r.in = 0.0f;
        r.b1 = 0.0f;
        r.b2 = 0.0f;
        return;
    }
    const float radius = expf(-static_cast<float>(PI_D) * bandwidthHz / rate);
    const float theta = 2.0f * static_cast<float>(PI_D) * hz / rate;
    r.b1 = 2.0f * radius * cosf(theta);
    r.b2 = -radius * radius;
    r.in = sinf(theta);
}

float runResonator(Resonator& r, float x) {
    const float y = r.in * x + r.b1 * r.y1 + r.b2 * r.y2;
    r.y2 = r.y1;
    r.y1 = y;
    return y;
}

/* ------------------------------------------------------------------ notes
 *
 * A Note is STRUCK AND LEFT TO RING, where every other wave here is switched
 * on, held at one level and switched off. A held level is heard as a box, and
 * a box is a beep however pure the tone inside it, which is most of what made
 * the old cues sound dated.
 *
 *   - IT RISES AND DIES AWAY. A few milliseconds up, then an exponential fall
 *     towards the timbre's sustain -- zero for everything but the piano,
 *     whose held keys have to keep sounding. One multiply a sample.
 *   - IT CAN GLIDE, from `a` to `b`, linearly in pitch, across the segment --
 *     or across the timbre's own `glideMs` where it has one, so a note can go
 *     on gliding after the next segment has begun.
 *   - IT RINGS ON UNDER THE NEXT ONE. A Note takes a voice of its own and the
 *     one before carries on falling, so a run of notes blooms into a chord.
 *     There are NOTE_VOICES; with every one busy the quietest is taken, and
 *     what its waveform leaves behind is faded out by `declick`, not cut.
 *
 * None of this touches the other waves. A script with no Note in it -- the
 * spoken phrase, GameOver -- generates exactly the samples it always did. */
struct TimbreSpec {
    uint16_t decayMs;          // the fall from the strike, as a time constant
    uint16_t releaseMs;        // the fall once let go, if the note sustains
    uint16_t overtoneRatio;    // its one overtone, as a multiple of the note x100
    uint16_t overtoneDecayMs;  // how fast that overtone dies
    uint16_t glideMs;          // a glide's length; 0 = the whole segment
    uint8_t  attackMs;         // the rise to full level
    uint8_t  sustain;          // % a held note levels out at; 0 rings out
    uint8_t  overtone;         // the overtone at the strike, % of the note
};

/* Every fall is a TIME CONSTANT, not a length: a note is at about a third of
 * its level one decay after the strike. A Note segment's `ms` says when the
 * NEXT thing starts, not when this one stops.
 *
 *                decay rel ratio odec glide att sus over */
constexpr TimbreSpec TIMBRES[] = {
    /* PURE  */ {40,    0, 100,   1,   0,   2,  0,  0},
    /* SOFT  */ {150,   0, 100,   1,   0,   4,  0,  0},
    /* GLOW  */ {100,   0, 100,   1,   0,   3,  0,  0},
    /* RING  */ {240,   0, 100,   1,   0,   3,  0,  0},
    /* SWISH */ {60,    0, 100,   1, 150,  30,  0,  0},
    /* PAD   */ {110,   0, 100,   1,   0,   4,  0,  0},
    /* KEYS  */ {340,  60, 100,   1,   0,   4, 40,  0},
    /* BELL  */ {220,   0, 200,  90,   0,   2,  0, 25},
    /* TICK  */ {20,    0, 100,   1,   0,   2,  0,  0},
};
static_assert(sizeof(TIMBRES) / sizeof(TIMBRES[0]) == TIMBRE_COUNT,
              "a Timbre without a TIMBRES row, or the other way round");

constexpr int NOTE_VOICES = 4;

/* Below this a voice is finished: -66 dB of full scale by default, under
 * anything a 1-inch speaker reproduces. synthSetNoteFloor() raises it on the
 * built-in DAC. */
float noteFloor = 0.0005f;

/* Mute fades a note this fast rather than cutting it. */
constexpr uint16_t NOTE_MUTE_MS = 4;

enum class NoteState : uint8_t { Idle, Rising, Held, LetGo };

struct NoteVoice {
    float phase = 0.0f;        // turns, 0-1
    float overPhase = 0.0f;
    float step = 0.0f;         // turns per sample: the pitch
    float stepGlide = 0.0f;    // and what it changes by each sample
    int32_t glideLeft = 0;
    float ratio = 1.0f;        // the overtone's
    float level = 0.0f;        // the envelope, 0-1
    float rise = 0.0f;
    float fall = 1.0f;
    float sustain = 0.0f;
    float letGo = 1.0f;
    float overtone = 0.0f;
    float overFall = 1.0f;
    float amp = 0.0f;
    float last = 0.0f;         // the last output, in case the voice is taken
    NoteState state = NoteState::Idle;
};

NoteVoice notes[NOTE_VOICES];
int8_t heldNote = -1;          // the voice the current segment struck
float declick = 0.0f;          // what a taken voice left behind, falling away
float declickFall = 0.0f;      // per sample: about a millisecond and a half

/* sin(2*pi*t), t in turns. A seventh-order polynomial on the quarter wave,
 * good to -76 dB: under the 8-bit DAC's own step and a 1-inch driver's
 * distortion. Four notes can be sounding, so this rather than sinf(), which
 * the Tone wave keeps so the cues built on it generate what they always did. */
float sinTurns(float t) {
    t -= static_cast<float>(static_cast<int32_t>(t));
    if (t < 0.0f) t += 1.0f;
    if (t > 0.5f) t -= 1.0f;
    if (t > 0.25f) {
        t = 0.5f - t;
    } else if (t < -0.25f) {
        t = -0.5f - t;
    }
    const float x = t * (2.0f * static_cast<float>(PI_D));
    const float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f - x2 * (1.0f / 5040.0f))));
}

/* The per-sample factor that falls by 1/e in `ms`. Called when a note is
 * struck, never per sample. */
float fallFactor(uint16_t ms) {
    return ms == 0 ? 0.0f : expf(-1000.0f / (static_cast<float>(ms) * rate));
}

/* Start the current segment's note on a voice of its own. segTotal must
 * already be the segment's length: a glide with no clock of its own runs
 * across exactly that. */
void strikeNote(const Segment& s) {
    const TimbreSpec& t = TIMBRES[s.c < TIMBRE_COUNT ? s.c : 0];

    int pick = -1;
    float quietest = 0.0f;
    for (int i = 0; i < NOTE_VOICES; ++i) {
        if (notes[i].state == NoteState::Idle) {
            pick = i;
            break;
        }
        const float loud = notes[i].level * notes[i].amp;
        if (pick < 0 || loud < quietest) {
            pick = i;
            quietest = loud;
        }
    }
    NoteVoice& v = notes[pick];
    if (v.state != NoteState::Idle) declick += v.last;

    v.state = t.attackMs > 0 ? NoteState::Rising : NoteState::Held;
    v.level = t.attackMs > 0 ? 0.0f : 1.0f;
    v.rise = t.attackMs > 0 ? 1000.0f / (static_cast<float>(t.attackMs) * rate) : 0.0f;
    v.fall = fallFactor(t.decayMs);
    v.sustain = t.sustain * 0.01f;
    v.letGo = (t.sustain > 0 && t.releaseMs > 0) ? fallFactor(t.releaseMs) : v.fall;
    v.ratio = t.overtoneRatio * 0.01f;
    /* An overtone that would fold back from above Nyquist is left out. */
    const float top = static_cast<float>(s.a > s.b ? s.a : s.b) * v.ratio;
    v.overtone = top < rate * 0.45f ? t.overtone * 0.01f : 0.0f;
    v.overFall = fallFactor(t.overtoneDecayMs);
    /* `amp` is the peak, overtone included. */
    v.amp = s.amp * 0.01f / (1.0f + v.overtone);
    v.phase = 0.0f;
    v.overPhase = 0.0f;
    v.step = static_cast<float>(s.a) / rate;
    const int32_t glide =
        t.glideMs > 0 ? static_cast<int32_t>(rate * t.glideMs / 1000.0f) : segTotal;
    v.glideLeft = glide;
    v.stepGlide = (static_cast<float>(s.b) - s.a) / rate / static_cast<float>(glide > 0 ? glide : 1);
    v.last = 0.0f;
    heldNote = static_cast<int8_t>(pick);
}

/* The current segment's note is over: it rings on, or, for a timbre that
 * sustains, starts its release. Nothing is cut. */
void letGoOfNote() {
    if (heldNote < 0) return;
    NoteVoice& v = notes[heldNote];
    if (v.state == NoteState::Rising || v.state == NoteState::Held) v.state = NoteState::LetGo;
    heldNote = -1;
}

bool notesSounding() {
    for (const NoteVoice& v : notes) {
        if (v.state != NoteState::Idle) return true;
    }
    return declick != 0.0f;
}

/* Every voice, one sample on, summed. */
float runNotes() {
    float sum = 0.0f;
    for (NoteVoice& v : notes) {
        if (v.state == NoteState::Idle) continue;
        if (v.state == NoteState::Rising) {
            v.level += v.rise;
            if (v.level >= 1.0f) {
                v.level = 1.0f;
                v.state = NoteState::Held;
            }
        } else if (v.state == NoteState::Held) {
            v.level = v.sustain + (v.level - v.sustain) * v.fall;
        } else {
            v.level *= v.letGo;
        }
        if (v.state != NoteState::Rising && v.level * v.amp < noteFloor) {
            v.state = NoteState::Idle;
            declick += v.last;
            continue;
        }
        if (v.glideLeft > 0) {
            v.step += v.stepGlide;
            --v.glideLeft;
        }
        v.phase += v.step;
        if (v.phase >= 1.0f) v.phase -= 1.0f;
        float y = sinTurns(v.phase);
        if (v.overtone > 0.0f) {
            v.overtone *= v.overFall;
            v.overPhase += v.step * v.ratio;
            if (v.overPhase >= 1.0f) v.overPhase -= 1.0f;
            y += v.overtone * sinTurns(v.overPhase);
        }
        v.last = v.amp * v.level * y;
        sum += v.last;
    }
    if (declick != 0.0f) {
        sum += declick;
        declick *= declickFall;
        if (declick > -0.00002f && declick < 0.00002f) declick = 0.0f;
    }
    return sum;
}

/* Mute: every note fades in a few milliseconds instead of ringing out. */
void muteNotes() {
    const float quick = fallFactor(NOTE_MUTE_MS);
    for (NoteVoice& v : notes) {
        if (v.state == NoteState::Idle) continue;
        v.state = NoteState::LetGo;
        v.letGo = quick;
    }
    heldNote = -1;
}

/* Notes that ring on under each other add up, and a chord can pass full
 * scale where no single note does. A hard clip there squares the very peaks
 * the notes were meant to make round, so the sum is bent smoothly towards
 * the ceiling instead, from three quarters of the way up: untouched below
 * that, which is nearly always, and never quite reaching OUTPUT_CLIP. */
float noteCeiling(float x) {
    constexpr float KNEE = OUTPUT_SCALE * 0.75f;
    constexpr float ROOM = OUTPUT_CLIP - KNEE;
    if (x > KNEE) {
        const float over = x - KNEE;
        return KNEE + ROOM * over / (over + ROOM);
    }
    if (x < -KNEE) {
        const float over = -x - KNEE;
        return -(KNEE + ROOM * over / (over + ROOM));
    }
    return x;
}

void startSegment() {
    const Segment& s = script[scriptAt];
    segTotal = static_cast<int32_t>(rateInt) * s.ms / 1000;
    if (segTotal < 1) segTotal = 1;
    segLeft = segTotal;
    /* A Note has a level of its own, so the gain the other waves share is not
     * raised for one: a Tone that was sounding fades away under the note
     * exactly as it would into a rest. */
    gainTarget = s.wave == Wave::Note ? 0.0f : s.amp * 0.01f;
    if (s.wave != Wave::Silence && s.wave != Wave::Note) {
        voiceAt = scriptAt;
        hasVoice = true;
    }

    switch (s.wave) {
        case Wave::Noise:
            tuneResonator(formant[0], s.a, s.b > 0 ? s.b : 1500.0f);
            break;
        case Wave::Voiced:
        case Wave::Unvoiced:
            tuneResonator(formant[0], s.a, FORMANT_BANDWIDTH);
            tuneResonator(formant[1], s.b, FORMANT_BANDWIDTH * 1.5f);
            tuneResonator(formant[2], s.c, FORMANT_BANDWIDTH * 2.4f);
            break;
        case Wave::Note:
            strikeNote(s);
            break;
        default:
            break;
    }
}

}   // namespace

bool playing = false;

void synthSetRate(int samplesPerSecond) {
    if (samplesPerSecond == rateInt) return;
    rateInt = samplesPerSecond;
    rate = static_cast<float>(samplesPerSecond);
    voicePeriod = static_cast<int>(rate / VOICE_PITCH_HZ);
    gainSlew = 1.0f / (GAIN_SLEW_SECONDS * rate);
    /* What a taken voice leaves behind falls away over about 1.5ms. */
    declickFall = expf(-1.0f / (0.0015f * rate));
}

void synthSetNoteFloor(float floor) { noteFloor = floor; }

void synthLoad(const Segment* segments, uint8_t count) {
    if (count > MAX_SEGMENTS) count = MAX_SEGMENTS;

    /* Is this the SAME script arriving again while it is still sounding?
     *
     * That is a held piano key, not a new cue. The synthesiser has no note-on
     * and no sustain, so PianoGame keeps a held note alive by asking for it
     * again before the last one runs out -- and snapping `phase` back to
     * zero at that seam is a step discontinuity in the waveform, which is a
     * click. Which is heard, correctly, as a tone that is not continuous.
     *
     * So a continuation keeps the oscillator running and leaves the resonators
     * alone; only a genuinely different sound resets them. Comparing the whole
     * script rather than trusting a flag from the caller means no game has to
     * know this exists, and a game that asks for a different note gets the
     * reset it needs. */
    bool continuation = playing && count == scriptLength;
    for (uint8_t i = 0; continuation && i < count; ++i) {
        if (memcmp(&script[i], &segments[i], sizeof(Segment)) != 0) {
            continuation = false;
        }
    }
    /* A Note is never continued this way: it is STRUCK, so the same note
     * asked for again is a key pressed again, and it is struck again. A key
     * still down says so with synthHold() instead -- the two arrive looking
     * identical, so the game that knows which it is has to say. */
    if (count > 0 && segments[0].wave == Wave::Note) continuation = false;

    for (uint8_t i = 0; i < count; ++i) script[i] = segments[i];
    scriptLength = count;
    scriptAt = 0;

    /* What must NOT be reset: `gain`, so a cue arriving on top of one still
     * sounding slews between the two instead of clicking. What must be: the
     * resonators, which otherwise ring the previous phoneme into the new
     * sound. */
    if (!continuation) {
        for (Resonator& r : formant) {
            r.y1 = 0.0f;
            r.y2 = 0.0f;
        }
        phase = 0.0f;
        pitchCounter = 0;
        hasVoice = false;
        letGoOfNote();   // the old cue's note rings out under the new one
    }
    playing = true;
    releasing = false;
    startSegment();
}

void synthFadeOut() {
    if (playing) {
        releasing = true;
        gainTarget = 0.0f;
        muteNotes();
    }
}

/* A key is still down: give the note it struck its whole segment again, so
 * it stays held rather than being let go. Nothing is struck and nothing
 * resets, which is what makes it seamless. It has to arrive before the
 * segment runs out as the GENERATOR counts it, a DMA ahead of the speaker --
 * BoardAudio.cpp static_asserts that against SOUND_NOTE_REARM_MS. A note
 * already let go stays let go: holding cannot bring back a key that came up. */
void synthHold() {
    if (!playing || releasing || heldNote < 0) return;
    if (script[scriptAt].wave != Wave::Note) return;
    const NoteState st = notes[heldNote].state;
    if (st == NoteState::Rising || st == NoteState::Held) segLeft = segTotal;
}

/* Generates one sample and advances the script. Returns false once the whole
 * script is spent, at which point the caller stops asking. */
bool nextSample(int16_t& out) {
    while (segLeft <= 0 && !releasing) {
        letGoOfNote();   // its time is up; it rings on under what comes next
        if (scriptAt + 1 >= scriptLength) {
            releasing = true;
            gainTarget = 0.0f;
            break;
        }
        ++scriptAt;
        startSegment();
    }
    /* Over once the last segment has faded AND every note it struck has
     * rung out. */
    if (releasing && gain < RELEASE_FLOOR && !notesSounding()) {
        playing = false;
        releasing = false;
        gain = 0.0f;
        out = 0;
        return false;
    }

    if (!releasing) --segLeft;   // a release holds the segment's last pitch

    /* What sounds is the segment being played, or in a rest the one before
     * it, fading. `carried` is true in the rest, and holds the pitch where
     * that segment ended. */
    const bool carried = voiceAt != scriptAt;
    const Segment& s = script[voiceAt];

    float value = 0.0f;
    switch (hasVoice ? s.wave : Wave::Silence) {
        case Wave::Silence:
        case Wave::Note:     // never voiceAt; sounds from its voice, below
            break;

        case Wave::Tone:
        case Wave::Square: {
            /* A sweep is linear across the segment. `progress` runs 0 to 1,
             * so a segment with a == b costs the same arithmetic and simply
             * does not move. */
            const float progress =
                carried ? 1.0f : 1.0f - static_cast<float>(segLeft) / segTotal;
            const float hz = s.a + (static_cast<float>(s.b) - s.a) * progress;
            phase += hz / rate;
            if (phase >= 1.0f) phase -= 1.0f;
            value = (s.wave == Wave::Tone)
                        ? sinf(phase * 2.0f * static_cast<float>(PI_D))
                        : (phase < 0.5f ? 0.75f : -0.75f);
            break;
        }

        case Wave::Noise:
            value = s.a > 0 ? runResonator(formant[0], whiteNoise()) * NOISE_MAKEUP
                            : whiteNoise() * 0.6f;
            break;

        case Wave::Voiced: {
            /* An impulse train: one full-scale sample per pitch period and
             * silence in between. Its spectrum is a flat comb, which is
             * exactly what a bank of formant filters wants to be handed. */
            float excitation = 0.0f;
            if (--pitchCounter <= 0) {
                pitchCounter = voicePeriod;
                excitation = 1.0f;
            }
            value = (runResonator(formant[0], excitation)
                     + runResonator(formant[1], excitation) * 0.70f
                     + runResonator(formant[2], excitation) * 0.40f) * 0.80f;
            break;
        }

        case Wave::Unvoiced: {
            const float n = whiteNoise();
            value = (runResonator(formant[0], n)
                     + runResonator(formant[1], n) * 0.8f
                     + runResonator(formant[2], n) * 0.5f) * NOISE_MAKEUP * 0.7f;
            break;
        }
    }

    gain += (gainTarget - gain) * gainSlew;
    /* The notes are added after, and only when there are any, so a script
     * with none produces bit for bit what it did before notes existed. */
    float mixed = value * gain * OUTPUT_SCALE;
    if (notesSounding()) mixed = noteCeiling(mixed + runNotes() * OUTPUT_SCALE);
    int32_t sample = static_cast<int32_t>(mixed);
    if (sample > OUTPUT_CLIP) sample = OUTPUT_CLIP;
    if (sample < -OUTPUT_CLIP) sample = -OUTPUT_CLIP;
    out = static_cast<int16_t>(sample);
    return true;
}

float synthGain() { return gain; }
float synthPhase() { return phase; }
int synthNotesSounding() {
    int n = 0;
    for (const NoteVoice& v : notes) n += v.state != NoteState::Idle;
    return n;
}

}   // namespace audio
