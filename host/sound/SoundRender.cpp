// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* Every sound the console makes, written to WAV files on a PC.
 *
 * The samples are the firmware's own. src/hal/BoardAudio.cpp and
 * BoardAudioCues.cpp are compiled unchanged, every cue is asked for through
 * the door every screen uses -- Board::playSound(), mute gate and all -- and
 * the samples are pulled out with audio::generateBlock(), which is what the
 * braino-audio task does on a board. Nothing in this directory makes or
 * shapes a sound; SoundStubs.cpp answers the rest of the firmware with
 * nothing.
 *
 * Two builds (host/CMakeLists.txt), because the backends do not generate the
 * same thing:
 *
 *   braino-sounds      the codec path: signed 16-bit at 16 kHz. What the
 *                      host, the Freenove's ES8311 and a bare I2S amplifier
 *                      are given.
 *   braino-sounds-dac  the E32R28T-1's built-in DAC: 24 kHz, offset-binary
 *                      words scaled by the volume, of which only the high
 *                      byte reaches the speaker. The WAV is that byte, so it
 *                      sounds the way the 8-bit DAC does.
 *
 *   braino-sounds <dir>     one WAV per cue, plus the sequences below, and a
 *                           table of what each measured
 *   braino-sounds --check   the same renders, judged; exit status 1 on a fault
 *   braino-sounds --bench   time per generated sample, on this computer
 */

#include "hal/BoardAudioInternal.h"

#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace audio;

namespace {

struct Word {
    const char* name;
    Sound cue;
};

/* The whole vocabulary, in the enum's order, so that a Sound added without a
 * line here fails to compile rather than going unheard. */
constexpr Word VOCABULARY[] = {
    {"Tap", Sound::Tap},             {"Select", Sound::Select},
    {"Correct", Sound::Correct},     {"Wrong", Sound::Wrong},
    {"Reveal", Sound::Reveal},       {"Coin", Sound::Coin},
    {"LevelUp", Sound::LevelUp},     {"Victory", Sound::Victory},
    {"GameOver", Sound::GameOver},   {"HighScore", Sound::HighScore},
    {"Countdown", Sound::Countdown}, {"Whoosh", Sound::Whoosh},
    {"Pop", Sound::Pop},             {"Step", Sound::Step},
    {"YourTurn", Sound::YourTurn},   {"Pad1", Sound::Pad1},
    {"Pad2", Sound::Pad2},           {"Pad3", Sound::Pad3},
    {"Pad4", Sound::Pad4},           {"NoteC4", Sound::NoteC4},
    {"NoteCs4", Sound::NoteCs4},     {"NoteD4", Sound::NoteD4},
    {"NoteDs4", Sound::NoteDs4},     {"NoteE4", Sound::NoteE4},
    {"NoteF4", Sound::NoteF4},       {"NoteFs4", Sound::NoteFs4},
    {"NoteG4", Sound::NoteG4},       {"NoteGs4", Sound::NoteGs4},
    {"NoteA4", Sound::NoteA4},       {"NoteAs4", Sound::NoteAs4},
    {"NoteB4", Sound::NoteB4},       {"NoteC5", Sound::NoteC5},
    {"NoteHold", Sound::NoteHold},   {"Bell", Sound::Bell},
    {"Boot", Sound::Boot},
};
constexpr size_t VOCABULARY_SIZE = sizeof(VOCABULARY) / sizeof(VOCABULARY[0]);

constexpr bool inEnumOrder() {
    for (size_t i = 0; i < VOCABULARY_SIZE; ++i) {
        if (static_cast<size_t>(VOCABULARY[i].cue) != i) return false;
    }
    return true;
}
static_assert(VOCABULARY_SIZE == static_cast<size_t>(Sound::Boot) + 1,
              "a Sound is missing from VOCABULARY");
static_assert(inEnumOrder(), "VOCABULARY is not in the enum's order");

/* Cues as games actually play them: one press is not how a piano or a Ludo
 * walk is heard. Times are milliseconds from the start of the render. */
struct Event {
    uint32_t atMs;
    Sound cue;
};

struct Sequence {
    const char* name;
    const Event* events;
    size_t count;
    uint32_t lengthMs;   // render at least this long
};

/* A key held down, kept held the way PianoGame does it: NoteHold every
 * SOUND_NOTE_REARM_MS. The seams are where a click or a re-strike would be. */
constexpr uint32_t REARM_MS = SOUND_NOTE_REARM_MS;
constexpr Event PIANO_HELD[] = {
    {0, Sound::NoteE4},              {REARM_MS, Sound::NoteHold},
    {2 * REARM_MS, Sound::NoteHold}, {3 * REARM_MS, Sound::NoteHold},
    {4 * REARM_MS, Sound::NoteHold}, {5 * REARM_MS, Sound::NoteHold},
    {6 * REARM_MS, Sound::NoteHold}, {7 * REARM_MS, Sound::NoteHold},
    {8 * REARM_MS, Sound::NoteHold}, {9 * REARM_MS, Sound::NoteHold},
    {10 * REARM_MS, Sound::NoteHold}, {11 * REARM_MS, Sound::NoteHold},
    {12 * REARM_MS, Sound::NoteHold},
};
/* The same key pressed twice: the second press must be heard. */
constexpr Event PIANO_TWICE[] = {
    {0, Sound::NoteE4}, {300, Sound::NoteE4},
};
/* Every key, low to high, a quarter of a second apart. */
constexpr Event PIANO_SCALE[] = {
    {0, Sound::NoteC4},     {250, Sound::NoteCs4},  {500, Sound::NoteD4},
    {750, Sound::NoteDs4},  {1000, Sound::NoteE4},  {1250, Sound::NoteF4},
    {1500, Sound::NoteFs4}, {1750, Sound::NoteG4},  {2000, Sound::NoteGs4},
    {2250, Sound::NoteA4},  {2500, Sound::NoteAs4}, {2750, Sound::NoteB4},
    {3000, Sound::NoteC5},
};
constexpr Event PIANO_RUN[] = {
    {0, Sound::NoteC4},   {180, Sound::NoteD4}, {360, Sound::NoteE4},
    {540, Sound::NoteF4}, {720, Sound::NoteG4}, {1080, Sound::NoteC5},
};
/* Cinnamon lights a pad for 600 ms. */
constexpr Event CINNAMON[] = {
    {0, Sound::Pad1}, {600, Sound::Pad2}, {1200, Sound::Pad3}, {1800, Sound::Pad4},
};
/* A Ludo counter walking six squares, one hop every 110 ms. */
constexpr Event LUDO_SIX[] = {
    {0, Sound::Step},   {110, Sound::Step}, {220, Sound::Step},
    {330, Sound::Step}, {440, Sound::Step}, {550, Sound::Step},
};
constexpr Event TAPPING[] = {
    {0, Sound::Tap}, {150, Sound::Tap}, {300, Sound::Tap}, {450, Sound::Tap},
};
/* The find alert re-arms every ALERT_CADENCE_MS (engine/NearbyPlay.h). */
constexpr Event BELL_ALERT[] = {
    {0, Sound::Bell}, {1500, Sound::Bell},
};

#define SEQ(name, events, ms) {name, events, sizeof(events) / sizeof(events[0]), ms}
constexpr Sequence SEQUENCES[] = {
    SEQ("seq-piano-held", PIANO_HELD, 0),
    SEQ("seq-piano-twice", PIANO_TWICE, 0),
    SEQ("seq-piano-run", PIANO_RUN, 0),
    SEQ("seq-piano-scale", PIANO_SCALE, 0),
    SEQ("seq-cinnamon", CINNAMON, 0),
    SEQ("seq-ludo-six", LUDO_SIX, 0),
    SEQ("seq-tapping", TAPPING, 0),
    SEQ("seq-bell-alert", BELL_ALERT, 0),
};
#undef SEQ

/* Pull one block the way the task does, decoded to what reaches the speaker.
 * Returns the number of frames; zero once the script is spent. */
size_t pullBlock(std::vector<int16_t>& out) {
    uint8_t block[BLOCK_FRAMES * 2 * sizeof(int16_t)];
    const size_t bytes = generateBlock(block, BLOCK_FRAMES);
    const size_t frames = bytes / (2 * sizeof(int16_t));
    for (size_t n = 0; n < frames; ++n) {
#if GUME_HAS_AUDIO_DAC
        /* Offset binary, of which the DAC takes the high byte: 0x80 is
         * silence, and the speaker hears 256 levels. */
        uint16_t word;
        std::memcpy(&word, block + n * 4, sizeof(word));
        out.push_back(static_cast<int16_t>((static_cast<int>(word >> 8) - 128) * 256));
#else
        int16_t sample;
        std::memcpy(&sample, block + n * 4, sizeof(sample));
        out.push_back(sample);
#endif
    }
    return frames;
}

/* A timeline of arms, as a board meets them. The braino-audio task keeps the
 * DMA full, so what is being generated runs up to DMA_BUF_COUNT x DMA_BUF_LEN
 * frames ahead of what is being heard -- 96 ms on the codec, 64 ms on the DAC
 * -- and a cue a game arms lands that far behind whatever is already queued.
 * A held piano key is where that decides what is heard, so it is modelled
 * rather than assumed away: `atMs` is the loop's clock, the output is the
 * speaker's, and an idle DMA plays silence. `armedAt`, if given, collects
 * where in the output each arm took effect. */
std::vector<int16_t> render(Board& board, const Event* events, size_t count, uint32_t minMs,
                            std::vector<size_t>* armedAt = nullptr) {
    std::vector<int16_t> out;
    const size_t lead = static_cast<size_t>(DMA_BUF_COUNT) * DMA_BUF_LEN;
    size_t next = 0;
    for (uint32_t ms = 0;; ++ms) {
        const size_t heard = static_cast<size_t>(ms) * AUDIO_RATE / 1000;
        if (out.size() < heard) out.resize(heard, 0);   // nothing queued: silence
        while (next < count && events[next].atMs <= ms) {
            if (armedAt != nullptr) armedAt->push_back(out.size());
            board.playSound(events[next++].cue);
        }
        while (out.size() < heard + lead && pullBlock(out) > 0) {
        }
        if (!playing && next >= count && ms >= minMs) break;
    }
    return out;
}

std::vector<int16_t> renderOne(Board& board, Sound cue) {
    const Event one[] = {{0, cue}};
    return render(board, one, 1, 0);
}

bool writeWav(const std::string& path, const std::vector<int16_t>& samples) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t rate = AUDIO_RATE;
    const uint32_t byteRate = rate * sizeof(int16_t);
    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);                 // PCM
    u16(1);                 // mono
    u32(rate);
    u32(byteRate);
    u16(sizeof(int16_t));   // block align
    u16(16);                // bits per sample
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    std::fwrite(samples.data(), 1, dataBytes, f);
    return std::fclose(f) == 0;
}

/* ------------------------------------------------------------ measuring */

struct Measure {
    double ms = 0;          // to the last sample that is not silence
    double audibleMs = 0;   // to the last sample within 40 dB of the peak
    double peakDb = -200;   // dBFS
    double rmsDb = -200;    // dBFS, over the sounding part
    int clipped = 0;        // samples at the synthesiser's ceiling
    int lastSample = 0;     // the size of the step the sound ends on
};

double dbfs(double v) { return v > 0 ? 20.0 * std::log10(v / 32768.0) : -200.0; }

Measure measure(const std::vector<int16_t>& s) {
    Measure m;
    size_t last = 0;
    int peak = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const int v = std::abs(static_cast<int>(s[i]));
        if (v > 0) last = i + 1;
        if (v > peak) peak = v;
    }
    double energy = 0;
    for (size_t i = 0; i < last; ++i) {
        energy += static_cast<double>(s[i]) * s[i];
#if GUME_HAS_AUDIO_DAC
        if (std::abs(static_cast<int>(s[i])) >= 127 * 256) ++m.clipped;
#else
        if (std::abs(static_cast<int>(s[i])) >= 32000) ++m.clipped;
#endif
    }
    m.ms = 1000.0 * last / AUDIO_RATE;
    /* A struck note never quite stops, so how long it is heard for is where it
     * falls 40 dB below its own peak: past that it is under anything else in
     * the room, and on the DAC boards under half their resolution. */
    size_t audible = 0;
    for (size_t i = 0; i < last; ++i) {
        if (std::abs(static_cast<int>(s[i])) * 100 >= peak) audible = i + 1;
    }
    m.audibleMs = 1000.0 * audible / AUDIO_RATE;
    m.peakDb = dbfs(peak);
    m.rmsDb = last ? dbfs(std::sqrt(energy / last)) : -200.0;
    m.lastSample = last ? std::abs(static_cast<int>(s[last - 1])) : 0;
    return m;
}

/* The largest sample-to-sample step within 2 ms of a re-arm, against the
 * largest anywhere else in the note. A seam that resets the oscillator is a
 * step far bigger than the waveform ever takes on its own. */
double seamRatio(const std::vector<int16_t>& s, const std::vector<size_t>& armedAt) {
    const size_t radius = AUDIO_RATE / 500;
    std::vector<bool> nearSeam(s.size(), false);
    for (size_t e = 1; e < armedAt.size(); ++e) {
        const size_t at = armedAt[e];
        for (size_t i = at > radius ? at - radius : 0; i < at + radius && i < s.size(); ++i) {
            nearSeam[i] = true;
        }
    }
    int seam = 0;
    int elsewhere = 0;
    const size_t steadyFrom = AUDIO_RATE / 50;   // past the attack
    for (size_t i = steadyFrom; i < s.size(); ++i) {
        const int step = std::abs(static_cast<int>(s[i]) - s[i - 1]);
        if (nearSeam[i]) {
            if (step > seam) seam = step;
        } else if (step > elsewhere) {
            elsewhere = step;
        }
    }
    return elsewhere > 0 ? static_cast<double>(seam) / elsewhere : 0.0;
}

/* How far the level of a held note climbs, anywhere after its strike, as a
 * ratio of where it was 5 ms before: a held key may hold or fade, never
 * swell, so anything well over 1 is a re-strike or a gap filling back in. */
double heldRise(const std::vector<int16_t>& s, size_t heldUntil) {
    const size_t win = AUDIO_RATE / 200;   // 5 ms
    double prev = -1;
    double worst = 0;
    for (size_t at = AUDIO_RATE / 50; at + win <= heldUntil && at + win <= s.size(); at += win) {
        double peak = 0;
        for (size_t i = at; i < at + win; ++i) {
            const double v = std::fabs(static_cast<double>(s[i]));
            if (v > peak) peak = v;
        }
        if (prev > 0 && peak / prev > worst) worst = peak / prev;
        prev = peak;
    }
    return worst;
}

void printRow(const char* name, const Measure& m) {
    std::printf("%-16s %7.0f %7.0f %8.1f %8.1f %7d %6d\n", name, m.ms, m.audibleMs, m.peakDb,
                m.rmsDb, m.clipped, m.lastSample);
}

void printHeader() {
    std::printf("%s at %d Hz\n", GUME_HAS_AUDIO_DAC ? "built-in DAC (high byte)" : "codec (16-bit)",
                AUDIO_RATE);
    std::printf("%-16s %7s %7s %8s %8s %7s %6s\n", "sound", "ms", "heard", "peak dB", "rms dB",
                "clipped", "last");
}

/* ------------------------------------------------------------- checking */

int faults = 0;

void fault(const char* name, const char* what) {
    std::printf("FAULT %s: %s\n", name, what);
    ++faults;
}

/* What every sound has to be, whatever it is meant to sound like. */
void judge(const char* name, const Measure& m) {
    if (m.ms <= 0) fault(name, "silent");
    /* The release fades to RELEASE_FLOOR before generation stops; a sound
     * that ends on a big step ends on a click. 1% of full scale is far above
     * the floor and far below anything audible as one. */
    if (m.lastSample > 328) fault(name, "ends on a step (a click)");
}

/* Lengths other code depends on, each stated where it is relied on. */
void judgeLengths(const char* name, Sound cue, const Measure& m) {
    char why[96];
    double limit = 0;
    const char* reason = nullptr;
    switch (cue) {
        case Sound::Step:
            limit = 110;
            reason = "a Ludo hop is 110 ms";
            break;
        case Sound::Pad1:
        case Sound::Pad2:
        case Sound::Pad3:
        case Sound::Pad4:
            limit = 600;
            reason = "Cinnamon lights a pad for 600 ms";
            break;
        case Sound::Bell:
            /* The find alert rings again every ALERT_CADENCE_MS (1500), and
             * the generator is up to the DMA's depth ahead of the speaker. */
            limit = 1400;
            reason = "a ring must have died away before the next one";
            break;
        default:
            return;
    }
    if (m.audibleMs >= limit) {
        std::snprintf(why, sizeof(why), "heard for %.0f ms, over %.0f ms: %s", m.audibleMs, limit,
                      reason);
        fault(name, why);
    }
}

int runCheck(Board& board) {
    for (const Word& w : VOCABULARY) {
        const Measure m = measure(renderOne(board, w.cue));
        if (w.cue == Sound::NoteHold) {
            /* Not a sound: with nothing held it must do nothing at all. */
            if (m.ms > 0) fault(w.name, "makes a sound with nothing held");
            continue;
        }
        judge(w.name, m);
        judgeLengths(w.name, w.cue, m);
    }
    for (const Sequence& q : SEQUENCES) {
        const std::vector<int16_t> s = render(board, q.events, q.count, q.lengthMs);
        judge(q.name, measure(s));
    }
    /* The held key: the oscillator must carry across every re-arm, and the
     * note must neither be struck again nor dip and come back. */
    std::vector<size_t> armedAt;
    const std::vector<int16_t> held =
        render(board, PIANO_HELD, sizeof(PIANO_HELD) / sizeof(PIANO_HELD[0]), 0, &armedAt);
    const double ratio = seamRatio(held, armedAt);
    if (ratio > 1.5) {
        char why[80];
        std::snprintf(why, sizeof(why), "a re-arm steps %.1fx the waveform's own largest step",
                      ratio);
        fault("seq-piano-held", why);
    }
    const double rise = heldRise(held, armedAt.back() + AUDIO_RATE / 20);
    if (rise > 1.15) {
        char why[80];
        std::snprintf(why, sizeof(why), "the held note's level climbs %.2fx: struck again, or a gap",
                      rise);
        fault("seq-piano-held", why);
    }
    /* The same key pressed twice: the second press is struck, not held. */
    armedAt.clear();
    const std::vector<int16_t> twice =
        render(board, PIANO_TWICE, sizeof(PIANO_TWICE) / sizeof(PIANO_TWICE[0]), 0, &armedAt);
    if (heldRise(twice, armedAt.back() + AUDIO_RATE / 10) < 1.3) {
        fault("seq-piano-twice", "the second press of the same key is not struck");
    }
    std::printf("%s: %d fault%s\n", faults == 0 ? "ok" : "FAILED", faults, faults == 1 ? "" : "s");
    return faults == 0 ? 0 : 1;
}

/* ------------------------------------------------------------- timing */

/* The synthesiser alone: arm, then generate into one block that is thrown
 * away, as the task would hand it to the DMA. Four seconds of each cue. */
int runBench(Board& board) {
    std::printf("%-16s %10s\n", "sound", "ns/sample");
    double totalNs = 0;
    size_t totalSamples = 0;
    uint8_t block[BLOCK_FRAMES * 2 * sizeof(int16_t)];
    for (const Word& w : VOCABULARY) {
        size_t samples = 0;
        const auto start = std::chrono::steady_clock::now();
        while (samples < static_cast<size_t>(AUDIO_RATE) * 4) {
            board.playSound(w.cue);
            size_t bytes;
            while ((bytes = generateBlock(block, BLOCK_FRAMES)) > 0) {
                samples += bytes / (2 * sizeof(int16_t));
            }
        }
        const double ns = std::chrono::duration<double, std::nano>(
                              std::chrono::steady_clock::now() - start)
                              .count();
        std::printf("%-16s %10.1f\n", w.name, ns / samples);
        totalNs += ns;
        totalSamples += samples;
    }
    std::printf("%-16s %10.1f\n", "all", totalNs / totalSamples);
    return 0;
}

}   // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <dir> | --check | --bench\n", argv[0]);
        return 2;
    }
    static Board board;
#if GUME_HAS_AUDIO_DAC
    dacUp = true;
#else
    codecUp = true;
#endif

    const std::string arg = argv[1];
    if (arg == "--check") return runCheck(board);
    if (arg == "--bench") return runBench(board);

    const std::string dir = arg;
    mkdir(dir.c_str(), 0755);
    printHeader();
    for (const Word& w : VOCABULARY) {
        const std::vector<int16_t> s = renderOne(board, w.cue);
        if (!writeWav(dir + "/" + w.name + ".wav", s)) {
            std::fprintf(stderr, "cannot write %s/%s.wav\n", dir.c_str(), w.name);
            return 1;
        }
        printRow(w.name, measure(s));
    }
    for (const Sequence& q : SEQUENCES) {
        const std::vector<int16_t> s = render(board, q.events, q.count, q.lengthMs);
        if (!writeWav(dir + "/" + q.name + ".wav", s)) {
            std::fprintf(stderr, "cannot write %s/%s.wav\n", dir.c_str(), q.name);
            return 1;
        }
        printRow(q.name, measure(s));
    }
    return 0;
}
