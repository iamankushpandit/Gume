// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of the synthesiser, compiled against the real
// src/hal/AudioSynth.cpp. It renders cues to samples and fails on a click: a
// step in the waveform that the waveform itself cannot make, a sound that
// stops dead instead of fading, and a held piano key that drops out.
//
// Not run by CI and not picked up by pio test. From the repository root:
//   g++ -std=c++17 -O2 -Isrc/hal test/host/audio_synth_test.cpp src/hal/AudioSynth.cpp -o /tmp/synth && /tmp/synth
// With a directory as its argument it also writes every render there as
// <cue>_<rate>.csv (one sample a line), for plotting.
//
// THE SCRIPTS BELOW ARE COPIES OF THE SHAPES IN src/hal/BoardAudioCues.cpp.
// That file needs the board to compile, and what is being tested here is the
// engine, not the tables: each copy stands for a shape the engine has to play
// without a click -- a tone into a rest, a square into a rest, a bell's
// decay, a swept fall, the spoken phrase. If a cue changes, its copy here
// does not have to; if a NEW shape appears there, add one here.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "AudioSynth.h"
#include "Sound.h"

using audio::Segment;
using audio::Wave;

#define CHECK(cond, ...)                                                \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAILED line %d: %s -- ", __LINE__, #cond);     \
            std::printf(__VA_ARGS__);                                   \
            std::printf("\n");                                          \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static int failures = 0;

constexpr Segment hush(uint16_t ms) { return Segment{0, 0, 0, ms, 0, Wave::Silence}; }
constexpr Segment tone(uint16_t hz, uint16_t ms, uint8_t amp) {
    return Segment{hz, hz, 0, ms, amp, Wave::Tone};
}
constexpr Segment sweep(uint16_t from, uint16_t to, uint16_t ms, uint8_t amp) {
    return Segment{from, to, 0, ms, amp, Wave::Tone};
}
constexpr Segment blip(uint16_t hz, uint16_t ms, uint8_t amp) {
    return Segment{hz, hz, 0, ms, amp, Wave::Square};
}
constexpr Segment air(uint16_t centreHz, uint16_t bandwidthHz, uint16_t ms, uint8_t amp) {
    return Segment{centreHz, bandwidthHz, 0, ms, amp, Wave::Noise};
}
constexpr Segment vox(uint16_t f1, uint16_t f2, uint16_t f3, uint16_t ms, uint8_t amp) {
    return Segment{f1, f2, f3, ms, amp, Wave::Voiced};
}
constexpr Segment hiss(uint16_t f1, uint16_t f2, uint16_t f3, uint16_t ms, uint8_t amp) {
    return Segment{f1, f2, f3, ms, amp, Wave::Unvoiced};
}

constexpr Segment WRONG[] = {tone(160, 130, 100), hush(40), tone(160, 150, 100)};
constexpr Segment YOUR_TURN[] = {tone(784, 70, 80), hush(45), tone(784, 120, 80)};
constexpr Segment HIGH_SCORE[] = {
    blip(1047, 65, 80), blip(1319, 65, 80), blip(1568, 65, 80),
    blip(2093, 75, 88), hush(40), blip(2093, 170, 88),
};
constexpr Segment BELL[] = {
    tone(1319, 60, 95), tone(1319, 90, 62), tone(1319, 110, 30), hush(40),
    tone(1046, 60, 95), tone(1046, 90, 62), tone(1046, 130, 28),
};
constexpr Segment CORRECT[] = {tone(880, 95, 88), tone(1320, 120, 88)};
constexpr Segment VICTORY[] = {
    blip(523, 95, 85), blip(659, 95, 85), blip(784, 95, 85), blip(1047, 250, 92),
};
constexpr Segment GAME_OVER[] = {tone(392, 150, 92), tone(330, 150, 92), sweep(311, 110, 400, 96)};
constexpr Segment WHOOSH[] = {air(2600, 1200, 32, 60), air(1700, 1000, 32, 68), air(950, 800, 42, 55)};
constexpr Segment NOTE_A4[] = {tone(440, SOUND_NOTE_MS, 85)};
constexpr Segment PHRASE[] = {
    vox(360, 1050, 2600, 110, 92),    vox(560, 1750, 2500, 190, 100),  hush(45),
    hiss(3000, 4200, 5200, 28, 95),   hiss(4800, 6200, 7000, 200, 100), hush(110),
    hush(45),                         hiss(800, 1600, 2400, 22, 85),   vox(360, 1050, 2600, 100, 92),
    vox(600, 1700, 2450, 150, 100),   vox(450, 1980, 2650, 110, 96),   vox(330, 2250, 2900, 100, 88),
    hush(120),                        hush(35),                        vox(250, 900, 2200, 50, 90),
    vox(350, 1050, 1500, 110, 96),    vox(600, 1700, 2450, 150, 100),  vox(450, 1980, 2650, 110, 96),
    vox(330, 2250, 2900, 100, 88),    vox(250, 1400, 2600, 130, 78),   vox(500, 950, 2400, 150, 100),
    vox(430, 820, 2400, 130, 92),     vox(370, 730, 2400, 120, 78),    hush(60),
};

struct Cue {
    const char* name;
    const Segment* segs;
    uint8_t count;
};

#define CUE(n, a) Cue{n, a, static_cast<uint8_t>(sizeof(a) / sizeof(a[0]))}
static const Cue CUES[] = {
    CUE("wrong", WRONG),        CUE("yourturn", YOUR_TURN), CUE("highscore", HIGH_SCORE),
    CUE("bell", BELL),          CUE("correct", CORRECT),    CUE("victory", VICTORY),
    CUE("gameover", GAME_OVER), CUE("whoosh", WHOOSH),      CUE("note", NOTE_A4),
    CUE("phrase", PHRASE),
};

constexpr int MAX_SAMPLES = 24000 * 4;
static int16_t out[MAX_SAMPLES];
static float gainAt[MAX_SAMPLES];
static float phaseAt[MAX_SAMPLES];

static bool sounding(const Segment& s) { return s.wave != Wave::Silence; }
static bool tonal(const Segment& s) { return s.wave == Wave::Tone || s.wave == Wave::Square; }

/* Render a whole cue from silence, recording the engine's own envelope and
 * oscillator beside every sample. Returns the number of samples. */
static int render(const Cue& cue, int rate) {
    audio::synthSetRate(rate);
    /* Start from a settled engine, as a cue armed on a quiet console does. */
    for (int16_t s; audio::nextSample(s);) {}
    audio::synthLoad(cue.segs, cue.count);
    int n = 0;
    for (int16_t s; n < MAX_SAMPLES && audio::nextSample(s); ++n) {
        out[n] = s;
        gainAt[n] = audio::synthGain();
        phaseAt[n] = audio::synthPhase();
    }
    return n;
}

static double rms(int from, int to) {
    double sum = 0.0;
    int count = 0;
    for (int i = from; i < to; ++i) {
        sum += static_cast<double>(out[i]) * out[i];
        ++count;
    }
    return count ? std::sqrt(sum / count) : 0.0;
}

static void writeCsv(const char* dir, const char* name, int rate, int n) {
    if (dir == nullptr) return;
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s_%d.csv", dir, name, rate);
    FILE* f = std::fopen(path, "w");
    if (f == nullptr) return;
    for (int i = 0; i < n; ++i) std::fprintf(f, "%d\n", out[i]);
    std::fclose(f);
}

/* A SOUND NEVER STOPS DEAD.
 *
 * At every place a sounding segment gives way to a rest, and at the end of
 * the script, the 2ms after must still carry a good part of the 2ms before:
 * the gain fades with a 4ms time constant, so a fade leaves about three
 * quarters and a cut leaves nothing. The threshold is loose because a vowel's
 * level moves inside a pitch period; a cut is exactly zero. Returns the worst
 * ratio seen. */
static double checkNoDeadStop(const Cue& cue, int rate, int n) {
    const int window = rate / 500;
    double worst = 1.0;
    int at = 0;
    for (uint8_t i = 0; i < cue.count; ++i) {
        const int len = static_cast<int>(static_cast<int32_t>(rate) * cue.segs[i].ms / 1000);
        at += len;
        const bool intoRest = (i + 1 < cue.count) ? !sounding(cue.segs[i + 1]) : true;
        if (!sounding(cue.segs[i]) || !intoRest) continue;
        if (at + window > n || at < window) continue;
        const double before = rms(at - window, at);
        if (before < 200.0) continue;   // already quiet: nothing to cut
        const double ratio = rms(at, at + window) / before;
        if (ratio < worst) worst = ratio;
        CHECK(ratio > 0.25, "%s @%d Hz: segment %u stops dead at sample %d (%.0f -> %.0f rms)",
              cue.name, rate, i, at, before, rms(at, at + window));
    }
    return worst;
}

/* NO STEP THE WAVEFORM CANNOT MAKE ITSELF.
 *
 * For a tone the largest change between two samples of a sine at frequency f
 * and amplitude A is 2*pi*f/rate*A; for a square it is nothing between its
 * edges, and its edges fall where the oscillator's phase crosses a half. So
 * at every sample, with the frequency and wave of whatever segment is (or
 * was last) sounding and the engine's own gain, a larger step is a click.
 * Only for cues made of tones and squares: noise and the voice have no such
 * bound. Returns the worst step seen as a multiple of its bound. */
static double checkNoStep(const Cue& cue, int rate, int n) {
    for (uint8_t i = 0; i < cue.count; ++i) {
        if (sounding(cue.segs[i]) && !tonal(cue.segs[i])) return 0.0;
    }
    double worst = 0.0;
    int seg = 0;
    int segEnd = static_cast<int>(static_cast<int32_t>(rate) * cue.segs[0].ms / 1000);
    const Segment* carried = &cue.segs[0];
    for (int i = 1; i < n; ++i) {
        while (i >= segEnd && seg + 1 < cue.count) {
            ++seg;
            segEnd += static_cast<int>(static_cast<int32_t>(rate) * cue.segs[seg].ms / 1000);
        }
        /* A boundary sample belongs to both sides: take the larger bound. */
        const Segment* now = sounding(cue.segs[seg]) ? &cue.segs[seg] : carried;
        const Segment* prev = carried;
        if (sounding(cue.segs[seg])) carried = &cue.segs[seg];
        const float g = std::fmax(gainAt[i], gainAt[i - 1]);
        const double step = std::fabs(static_cast<double>(out[i]) - out[i - 1]);
        /* A few LSB of rounding, plus what the gain's own slew moves a
         * full-scale sample by: that is the 4ms attack, and it is meant. */
        const double slew = std::fabs(static_cast<double>(gainAt[i]) - gainAt[i - 1]) * 32000.0;
        double bound = 8.0 + slew;
        for (const Segment* s : {now, prev}) {
            if (s->wave == Wave::Tone) {
                const double f = s->a > s->b ? s->a : s->b;
                bound = std::fmax(bound, 2.0 * M_PI * f / rate * g * 32000.0 * 1.02 + 8.0 + slew);
            } else if (s->wave == Wave::Square) {
                const bool edge = (phaseAt[i - 1] < 0.5f) != (phaseAt[i] < 0.5f) ||
                                  phaseAt[i] < phaseAt[i - 1];
                if (edge) bound = std::fmax(bound, 1.5 * g * 32000.0 + 8.0 + slew);
            }
        }
        const double over = step / bound;
        if (over > worst) worst = over;
        if (step > bound) {
            CHECK(false, "%s @%d Hz: step of %.0f at sample %d where the wave allows %.0f",
                  cue.name, rate, step, i, bound);
            return worst;   // one report per cue is enough
        }
    }
    return worst;
}

/* A HELD PIANO KEY NEVER DROPS OUT, ON THE DEVICE'S OWN TIMELINE.
 *
 * PianoGame re-arms the held note every SOUND_NOTE_REARM_MS of wall time.
 * But the generator does not run at wall time: it fills the DMA the moment a
 * note is armed and then stays one DMA ahead of the speaker. So by the time
 * of the first re-arm the engine has generated that much MORE of the note
 * than the wall clock says, and if that is past the end of the note it has
 * already faded out. This replays a held key frame by frame against that
 * model -- 20ms frames, and again with every frame at the 40ms this firmware
 * calls a bug -- and requires the note's level never to dip while the key is
 * down. Returns the lowest level seen as a fraction of the steady one. */
static double checkHeldNote(int rate, int frameMs) {
    constexpr int DMA_FRAMES = 6 * 256 + 128;   // BoardAudioInternal.h: DMA plus one block
    const int perMs = rate / 1000;
    const int holdMs = 2000;
    static int16_t played[24000 * 3];
    int heard = 0;      // samples the speaker has taken
    int made = 0;       // samples generated
    static int16_t made_[24000 * 3];
    audio::synthSetRate(rate);
    for (int16_t s; audio::nextSample(s);) {}
    uint32_t lastArm = 0;
    for (int ms = 0; ms < holdMs; ++ms) {
        if (ms % frameMs == 0) {
            const uint32_t now = static_cast<uint32_t>(ms);
            if (ms == 0 || now - lastArm >= SOUND_NOTE_REARM_MS) {
                lastArm = now;
                audio::synthLoad(NOTE_A4, 1);
            }
        }
        /* The generator keeps one DMA ahead of the speaker. Whatever it has
         * not made by the time the speaker gets there is silence. */
        if (made < heard) {
            while (made < heard) made_[made++] = 0;
        }
        for (int16_t s; made < heard + DMA_FRAMES && audio::nextSample(s);) made_[made++] = s;
        for (int k = 0; k < perMs; ++k, ++heard) played[heard] = heard < made ? made_[heard] : 0;
    }
    /* From 50ms in (past the attack) to the end of the hold, in 3ms windows:
     * longer than a period of A4, shorter than the 4ms fade, so a dip shows. */
    const int window = 3 * perMs;
    double steady = 0.0;
    for (int i = 50 * perMs; i + window <= 150 * perMs; ++i) {
        steady = std::fmax(steady, std::fabs(static_cast<double>(played[i])));
    }
    double lowest = 1.0;
    int lowestAt = 0;
    for (int i = 50 * perMs; i + window <= heard - 100 * perMs; i += window) {
        double peak = 0.0;
        for (int k = 0; k < window; ++k) peak = std::fmax(peak, std::fabs(static_cast<double>(played[i + k])));
        const double level = peak / steady;
        if (level < lowest) {
            lowest = level;
            lowestAt = i / perMs;
        }
    }
    CHECK(lowest > 0.9, "held A4 @%d Hz, %dms frames: level falls to %.2f at %dms", rate, frameMs,
          lowest, lowestAt);
    return lowest;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : nullptr;
    for (int rate : {16000, 24000}) {
        for (const Cue& cue : CUES) {
            const int n = render(cue, rate);
            writeCsv(dir, cue.name, rate, n);
            CHECK(n > 0 && n < MAX_SAMPLES, "%s rendered %d samples", cue.name, n);
            /* The release stops at RELEASE_FLOOR of the gain: under 1% of
             * full scale even on the loudest noise, which is -40 dB. */
            CHECK(std::abs(out[n - 1]) <= 320, "%s ends on %d", cue.name, out[n - 1]);
            const double dead = checkNoDeadStop(cue, rate, n);
            const double step = checkNoStep(cue, rate, n);
            std::printf("%-10s %5d Hz  %6d samples  quietest stop %.2f  worst step %.2fx bound\n",
                        cue.name, rate, n, dead, step);
        }
        for (int frameMs : {20, 40}) {
            std::printf("held A4    %5d Hz  %dms frames  lowest level %.2f\n", rate, frameMs,
                        checkHeldNote(rate, frameMs));
        }
    }
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("audio synth: all checks passed\n");
    return 0;
}
