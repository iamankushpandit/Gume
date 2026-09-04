#include "Board.h"

/* GUME_HAS_AUDIO_DAC is defined in board headers that wire a speaker to a GPIO
 * that is also an ESP32 built-in DAC output. Board headers that do not define
 * it get 0 here so the #if blocks below are always valid. */
#ifndef GUME_HAS_AUDIO_DAC
#define GUME_HAS_AUDIO_DAC 0
#endif

#if GUME_HAS_AUDIO_CODEC
#include <Wire.h>
#endif

#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
#include <driver/i2s.h>
#include <math.h>
#endif

/* ------------------------------------------------------------------ audio
 *
 * On a board with an audio codec the console makes sound. On a board without
 * one every entry point here stays the no-op it always was and the RGB pulse
 * is the whole of the feedback -- `beepOk()` has never promised a noise.
 *
 * NOTHING HERE IS A RECORDING, AND NOTHING HERE MAY BECOME ONE.
 *
 * Every sound Braino makes is generated one sample at a time from the segment
 * scripts below: a few oscillators, a noise source and three formant
 * resonators. There is no WAV, no PCM table, no sample bank. That is a flash
 * decision before it is an aesthetic one -- the app partition is 3 MB and
 * already three quarters full, and a second of 16-bit 16kHz mono audio is
 * 32 KB, so the fourteen cues in `Sound` would cost more than the entire game
 * catalogue's artwork. As synthesis they cost a few hundred bytes of const
 * data between them, and the spoken boot phrase costs about two hundred.
 *
 * WHY A SOUND IS NOT SIMPLY PLAYED.
 *
 * A cue is triggered from game code, in the middle of a frame, and the loop
 * has a 20ms budget. The bring-up probe in src/s3_diag.cpp plays its tones by
 * blocking on i2s_write until the whole note has been handed over -- 90 to
 * 300ms -- which is correct there, because a probe has no frame budget and
 * nothing else to do. Doing that here would blow the budget by an order of
 * magnitude on every correct answer in every game, and `Watchdog` would log
 * the stall.
 *
 * So playing a sound only ARMS a script. `tickAudio()`, which the runtime
 * calls once per frame beside `tickRgb()`, generates as many samples as the
 * I2S DMA will accept without blocking and then stops. The DMA holds 1536
 * samples -- 96ms at 16kHz -- so the audio is always well ahead of the frame
 * that needs it, and no frame is ever spent waiting. A sound outliving the
 * screen that started it is normal: a script is device state, not screen
 * state, and nothing in leaveActiveGame() needs to know about it.
 *
 * This is also why length is free. The buffered version this replaced
 * rendered a whole sound into 10 KB of static PCM up front, which capped a
 * cue at 320ms and made a spoken phrase impossible outright. Generating on
 * demand costs about 800 bytes of synthesiser state and has no upper bound.
 *
 * The codec register values, and three findings that are easy to get wrong,
 * come from bring-up on the Freenove FNK0104B with env:s3diag:
 *
 *   - The amplifier enable is ACTIVE LOW. Driving it high gives perfect
 *     silence from a codec that reports itself correctly configured, which
 *     looks exactly like a dead codec or an unplugged speaker.
 *   - MCLK must come from the APLL. The codec is an I2S slave clocked from
 *     MCLK at 384x the sample rate, and 160MHz does not divide to 6.144MHz.
 *   - Register 0x17, the ADC volume, resets to minimum. It is only relevant
 *     to recording, which this firmware does not do, but it is noted here
 *     because the vendor's own init leaves it unset and the next person to
 *     add capture will lose an afternoon to it.
 */

#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
namespace {

constexpr int AUDIO_RATE = 16000;

/* The longest script the firmware can arm. The spoken boot phrase is the one
 * that sets this, at twenty-four segments; every cue is six or fewer. A fixed
 * array that is usually three quarters empty is the right trade on a device
 * with no compacting heap -- see CLAUDE.md's memory rule. */
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
 * for. The shorthands further down are what the cue tables actually use, so
 * no caller sees the positional form. */
struct Segment {
    uint16_t a;
    uint16_t b;
    uint16_t c;
    uint16_t ms;
    uint8_t  amp;    // 0-100, beneath the codec's own volume setting
    Wave     wave;
};

/* ---------------------------------------------------------------- engine */

Segment script[MAX_SEGMENTS];
uint8_t scriptLength = 0;
uint8_t scriptAt = 0;
int32_t segTotal = 0;        // samples in the segment being played
int32_t segLeft = 0;         // samples of it still to generate
bool playing = false;

bool codecUp = false;
bool dacUp = false;
bool ampOn = false;

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

/* A monotone excitation is what makes the voice robotic rather than merely
 * synthetic. Real speech moves its pitch on every syllable; holding it
 * perfectly still is the entire effect, and it costs nothing to do. */
constexpr float VOICE_PITCH_HZ = 112.0f;
constexpr int VOICE_PERIOD = static_cast<int>(AUDIO_RATE / VOICE_PITCH_HZ);

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
constexpr float GAIN_SLEW = 1.0f / (0.004f * AUDIO_RATE);

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
    if (hz <= 20.0f || hz >= AUDIO_RATE * 0.47f) {
        r.in = 0.0f;
        r.b1 = 0.0f;
        r.b2 = 0.0f;
        return;
    }
    const float radius = expf(-static_cast<float>(PI) * bandwidthHz / AUDIO_RATE);
    const float theta = 2.0f * static_cast<float>(PI) * hz / AUDIO_RATE;
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

void startSegment() {
    const Segment& s = script[scriptAt];
    segTotal = static_cast<int32_t>(AUDIO_RATE) * s.ms / 1000;
    if (segTotal < 1) segTotal = 1;
    segLeft = segTotal;
    gainTarget = s.amp * 0.01f;

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
        default:
            break;
    }
}

/* Generates one sample and advances the script. Returns false once the whole
 * script is spent, at which point the caller stops asking. */
bool nextSample(int16_t& out) {
    while (segLeft <= 0) {
        if (scriptAt + 1 >= scriptLength) {
            playing = false;
            out = 0;
            return false;
        }
        ++scriptAt;
        startSegment();
    }

    const Segment& s = script[scriptAt];
    --segLeft;

    float value = 0.0f;
    switch (s.wave) {
        case Wave::Silence:
            break;

        case Wave::Tone:
        case Wave::Square: {
            /* A sweep is linear across the segment. `progress` runs 0 to 1,
             * so a segment with a == b costs the same arithmetic and simply
             * does not move. */
            const float progress = 1.0f - static_cast<float>(segLeft) / segTotal;
            const float hz = s.a + (static_cast<float>(s.b) - s.a) * progress;
            phase += hz / AUDIO_RATE;
            if (phase >= 1.0f) phase -= 1.0f;
            value = (s.wave == Wave::Tone)
                        ? sinf(phase * 2.0f * static_cast<float>(PI))
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
                pitchCounter = VOICE_PERIOD;
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

    gain += (gainTarget - gain) * GAIN_SLEW;
    int32_t sample = static_cast<int32_t>(value * gain * OUTPUT_SCALE);
    if (sample > OUTPUT_CLIP) sample = OUTPUT_CLIP;
    if (sample < -OUTPUT_CLIP) sample = -OUTPUT_CLIP;
    out = static_cast<int16_t>(sample);
    return true;
}

void setAmp(bool on) {
    if (BOARD.audio.ampEnablePin == PIN_NONE || ampOn == on) return;
    ampOn = on;
    const bool low = BOARD.audio.ampEnableActiveLow ? on : !on;
    digitalWrite(BOARD.audio.ampEnablePin, low ? LOW : HIGH);
}

#if GUME_HAS_AUDIO_CODEC
void esWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(static_cast<uint8_t>(BOARD.audio.codecI2cAddress));
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

/* Register 0x32 is the DAC volume, and it is linear in DECIBELS: half a
 * decibel per step, with 0xBF as unity gain and 0x00 as silence.
 *
 * THIS IS WHY THE CONSOLE WAS ALMOST INAUDIBLE, and the mistake is worth
 * spelling out because it reads as obviously correct. Scaling the percentage
 * straight onto the byte -- (percent * 256 / 100) - 1, which is what both this
 * and src/s3_diag.cpp did -- treats a logarithmic register as a linear one.
 * The default 60% landed on 0x98, which is **-19.5 dB**: a tenth of the
 * amplitude the number implies, and far too quiet to hear across a room. The
 * error is worst exactly where people leave a volume control, in the middle.
 * At the other end it fails the other way: 100% mapped to 0xFF, **+32 dB**,
 * which would have clipped every sound into a square wave.
 *
 * So the percentage is treated as a fraction of AMPLITUDE and converted:
 * 100% is unity, 50% is -6 dB, 10% is -20 dB. That is what a volume control is
 * normally taken to mean, and it is monotonic and safe across the whole range.
 *
 * The result is clamped at unity. Above 0 dB this register is digital gain on
 * a signal that already peaks near full scale, so the only thing louder buys
 * is clipping. If the console is still too quiet at 100%, the fix is the
 * synthesiser's own OUTPUT_SCALE below, or the amplifier -- not here. */
constexpr int ES8311_UNITY_REG = 0xBF;      // 0 dB
constexpr float ES8311_STEPS_PER_DB = 2.0f; // 0.5 dB per step

void applyCodecVolume(uint8_t percent) {
    if (percent == 0) {
        esWrite(0x32, 0x00);
        return;
    }
    if (percent > 100) percent = 100;
    const float dB = 20.0f * log10f(percent / 100.0f);
    int reg = static_cast<int>(lroundf(ES8311_UNITY_REG + dB * ES8311_STEPS_PER_DB));
    if (reg < 1) reg = 1;
    if (reg > ES8311_UNITY_REG) reg = ES8311_UNITY_REG;
    esWrite(0x32, static_cast<uint8_t>(reg));
}
#endif  /* GUME_HAS_AUDIO_CODEC */

/* Samples that were generated but that the DMA would not take this frame.
 *
 * i2s_write() with a zero timeout reports how much it accepted only after the
 * fact, so a full DMA leaves the tail of a generated block in hand. Throwing
 * it away is audible as a stutter on anything long enough to fill the DMA --
 * which is precisely the spoken phrase -- and the synthesiser cannot be run
 * backwards to regenerate it, so it is held here and written first next
 * frame. 512 bytes, static, like everything else in this file. */
uint8_t pending[128 * 2 * sizeof(int16_t)];
size_t pendingLength = 0;
size_t pendingAt = 0;

/* ------------------------------------------------------------- authoring
 *
 * A cue is a const Segment array; `arm()` copies it in and starts it. Copying
 * rather than keeping a pointer is deliberate -- a script outlives the call
 * that started it by up to a second and a half, and a pointer into a caller's
 * frame would be a use-after-free waiting for someone to write a cue with a
 * local table. */
void arm(const Segment* segments, uint8_t count) {
    if ((!codecUp && !dacUp) || count == 0) return;
    if (count > MAX_SEGMENTS) count = MAX_SEGMENTS;
    for (uint8_t i = 0; i < count; ++i) script[i] = segments[i];
    scriptLength = count;
    scriptAt = 0;

    /* What must NOT be reset: `gain`, so a cue arriving on top of one still
     * sounding slews between the two instead of clicking. What must be: the
     * resonators, which otherwise ring the previous phoneme into the new
     * sound, and the pending tail, which belongs to a sound that has just
     * been superseded. */
    for (Resonator& r : formant) {
        r.y1 = 0.0f;
        r.y2 = 0.0f;
    }
    phase = 0.0f;
    pitchCounter = 0;
    pendingLength = 0;
    pendingAt = 0;
    playing = true;
    startSegment();
    setAmp(true);
}

/* Shorthands, so the cue tables below read as music rather than as struct
 * initialisers. Durations are milliseconds, amplitudes are percentages. */
constexpr Segment hush(uint16_t ms) {
    return Segment{0, 0, 0, ms, 0, Wave::Silence};
}
constexpr Segment tone(uint16_t hz, uint16_t ms, uint8_t amp) {
    return Segment{hz, hz, 0, ms, amp, Wave::Tone};
}
constexpr Segment sweep(uint16_t from, uint16_t to, uint16_t ms, uint8_t amp) {
    return Segment{from, to, 0, ms, amp, Wave::Tone};
}
constexpr Segment blip(uint16_t hz, uint16_t ms, uint8_t amp) {
    return Segment{hz, hz, 0, ms, amp, Wave::Square};
}
constexpr Segment slide(uint16_t from, uint16_t to, uint16_t ms, uint8_t amp) {
    return Segment{from, to, 0, ms, amp, Wave::Square};
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

/* ------------------------------------------------------------------ cues
 *
 * Pitches are named where they are notes, because the cues that are runs have
 * to agree with each other: Coin, LevelUp, Victory and HighScore are all
 * built from the same C major set, so two of them landing close together
 * sounds like one instrument rather than like a fault.
 *
 * Three rules shape them, and they are the ones to hold to when adding more:
 *
 *   - A cue a player hears hundreds of times in a session (Tap, Coin) is
 *     SHORT and quiet. Past about 120ms it stops being feedback and starts
 *     being something the game is waiting for.
 *   - Cues that mean opposite things differ in DIRECTION, not only in pitch.
 *     Rising is good, falling is not. A player who is not listening carefully
 *     -- which is every player -- hears the contour before the notes.
 *   - The amplitudes are RELATIVE, and the top of the range is meant to be
 *     used. The first set of these topped out at 55 and the console was
 *     inaudible across a room -- partly because of the volume-curve bug in
 *     applyCodecVolume(), but partly because leaving 45% of the range unused
 *     on a 1-inch driver is just quietness. What matters is the spacing: Tap
 *     at 55 against Victory at 92 is what makes one feel incidental and the
 *     other final. Raising them all together changes nothing about that.
 */

constexpr Segment CUE_TAP[] = {
    blip(1400, 30, 55),
};

constexpr Segment CUE_SELECT[] = {
    slide(900, 1500, 50, 62),
};

constexpr Segment CUE_CORRECT[] = {
    tone(880, 95, 88),
    tone(1320, 120, 88),
};

constexpr Segment CUE_WRONG[] = {
    tone(160, 130, 100),
    hush(40),
    tone(160, 150, 100),
};

constexpr Segment CUE_REVEAL[] = {
    sweep(500, 1100, 75, 72),
};

constexpr Segment CUE_COIN[] = {
    blip(1568, 50, 72),     // G6
    blip(2093, 95, 72),     // C7
};

constexpr Segment CUE_LEVEL_UP[] = {
    blip(784, 75, 80),      // G5
    blip(988, 75, 80),      // B5
    blip(1175, 140, 80),    // D6
};

constexpr Segment CUE_VICTORY[] = {
    blip(523, 95, 85),      // C5
    blip(659, 95, 85),      // E5
    blip(784, 95, 85),      // G5
    blip(1047, 250, 92),    // C6
};

/* The only cue that falls the whole way, and the longest -- losing is the one
 * moment in a game that is allowed to take a beat. */
constexpr Segment CUE_GAME_OVER[] = {
    tone(392, 150, 92),     // G4
    tone(330, 150, 92),     // E4
    sweep(311, 110, 400, 96),
};

constexpr Segment CUE_HIGH_SCORE[] = {
    blip(1047, 65, 80),     // C6
    blip(1319, 65, 80),     // E6
    blip(1568, 65, 80),     // G6
    blip(2093, 75, 88),     // C7
    hush(40),
    blip(2093, 170, 88),
};

constexpr Segment CUE_COUNTDOWN[] = {
    tone(1000, 45, 68),
};

/* Three noise bands in sequence, sliding down: the cheapest thing that reads
 * as movement. One band on its own does not -- it just sounds like static. */
constexpr Segment CUE_WHOOSH[] = {
    air(2600, 1200, 32, 60),
    air(1700, 1000, 32, 68),
    air(950, 800, 42, 55),
};

constexpr Segment CUE_POP[] = {
    slide(300, 1300, 45, 76),
};

/* The four pads. Sine rather than square: they are the only cue that plays
 * repeatedly at a steady pulse, and a square wave becomes wearing after two
 * dozen of them. 260ms fits inside Cinnamon's 600ms lit period with room, so
 * the note has stopped before the pad goes dark rather than being cut off by
 * the next one. */
constexpr Segment CUE_PAD_1[] = {tone(392, 260, 85)};    // G4
constexpr Segment CUE_PAD_2[] = {tone(523, 260, 85)};    // C5
constexpr Segment CUE_PAD_3[] = {tone(659, 260, 85)};    // E5
constexpr Segment CUE_PAD_4[] = {tone(784, 260, 85)};    // G5

/* -------------------------------------------------------------- the voice
 *
 * "Let's play Braino!", as twenty-one segments of formant synthesis.
 *
 * There is no text-to-speech here and no dictionary. The phrase is spelled
 * out as the phonemes it is made of, and each phoneme is the first three
 * formant frequencies of a vocal tract shaped to say it. Drive three
 * resonators tuned to those frequencies with a buzz and a vowel comes out;
 * drive the same three with noise and a consonant does. That is the whole
 * technique, and it is roughly forty years old -- it is what made a 1980s
 * home computer talk, which is exactly why this sounds like one. The ask was
 * for a robotic voice, and the honest way to get one is to build the thing
 * that is genuinely robotic rather than to compress a recording of a person.
 *
 *   L  EH T S     P L  EY     B R  EY N  OW
 *
 * Three things about the transcription are load-bearing:
 *
 *   - A DIPHTHONG IS TWO SEGMENTS. The "EY" in "play" is not one sound: the
 *     tongue moves from EH towards IY while it is being said and F2 climbs
 *     around 500Hz doing it. Written as a single steady segment it comes out
 *     as "pleh". Both EYs below are split, and so is the closing OW.
 *   - A STOP IS A SILENCE AND THEN A BURST. T, P and B are not sounds; they
 *     are the moment a closed mouth opens. The silence is not padding and
 *     cannot be trimmed -- without it the burst has nothing to contrast
 *     against and the word simply begins with a click.
 *   - THE FRICATIVE IS THE LONG ONE. The S is 120ms, longer than any vowel
 *     here. Fricatives carry little energy at these amplitudes, so an S of
 *     "correct" length is inaudible on a small driver and the phrase lands
 *     as "let play".
 *
 * FOUR THINGS ARE SET BY EAR ON HARDWARE, and all four moved after the first
 * listen on a real device, where the phrase came out too fast and too quiet to
 * make out:
 *
 *   - IT IS SLOW. 2.4 seconds for four syllables, roughly twice a person's
 *     conversational rate. Synthetic speech with no pitch movement carries
 *     none of the prosody a listener leans on, so the only cue left for where
 *     one sound ends and the next begins is duration. Speeding this up is the
 *     first thing that makes it unintelligible again.
 *   - THE STRESSED VOWELS ARE THE LONG ONES. "LET'S", "PLAY", "BRAY" get
 *     150-190ms; the unstressed N and the final OW taper off. Equal durations
 *     read as a robot spelling out letters rather than saying words.
 *   - THE DIPHTHONGS ARE THREE STEPS, NOT TWO. F2 climbs about 550 Hz across
 *     an "EY" and the ear tracks that sweep; in two steps the jump is audible
 *     as a click at the join and the vowel reads as two vowels.
 *   - THE AMPLITUDES ARE NEAR THE TOP. Speech is much quieter than a tone at
 *     the same nominal amplitude, because the excitation is impulses and most
 *     of a vowel is the decay between them -- which is also why
 *     FORMANT_BANDWIDTH matters here. See the note on it above.
 *
 * The remaining knob is VOICE_PITCH_HZ: lower is more menacing, higher is more
 * toy. Nothing else in the file needs to change. */
constexpr Segment PHRASE_LETS_PLAY_BRAINO[] = {
    vox(360, 1050, 2600, 110, 92),     // L
    vox(560, 1750, 2500, 190, 100),    // EH  -- the stressed vowel of "Let's"
    hush(45),                          // T   -- the closure
    hiss(3000, 4200, 5200, 28, 95),    // T   -- the burst
    hiss(4800, 6200, 7000, 200, 100),  // S
    hush(110),                         //     -- between words

    hush(45),                          // P   -- the closure
    hiss(800, 1600, 2400, 22, 85),     // P   -- the burst
    vox(360, 1050, 2600, 100, 92),     // L
    vox(600, 1700, 2450, 150, 100),    // EY  -- open, where the stress sits
    vox(450, 1980, 2650, 110, 96),     // EY  -- mid-glide
    vox(330, 2250, 2900, 100, 88),     // EY  -- closed
    hush(120),                         //     -- between words

    hush(35),                          // B   -- the closure
    vox(250, 900, 2200, 50, 90),       // B   -- the burst, and it is voiced
    vox(350, 1050, 1500, 110, 96),     // R   -- a low F3 is the whole of an R
    vox(600, 1700, 2450, 150, 100),    // EY
    vox(450, 1980, 2650, 110, 96),     // EY  -- mid-glide
    vox(330, 2250, 2900, 100, 88),     // EY  -- closed
    vox(250, 1400, 2600, 130, 78),     // N   -- nasal, so quieter
    vox(500, 950, 2400, 150, 100),     // OW
    vox(430, 820, 2400, 130, 92),      // OW  -- rounding
    vox(370, 730, 2400, 120, 78),      // OW  -- and closing
    hush(60),
};

template <size_t N>
void armCue(const Segment (&segments)[N]) {
    arm(segments, static_cast<uint8_t>(N));
}

}  // namespace
#endif  /* GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC */

void Board::beginAudio() {
#if GUME_HAS_AUDIO_CODEC
    if (BOARD.audio.ampEnablePin != PIN_NONE) {
        pinMode(BOARD.audio.ampEnablePin, OUTPUT);
        /* Start with the amplifier OFF. It is switched on only while
         * something is playing, so an idle console is not holding a class-D
         * amplifier awake on battery. */
        ampOn = true;
        setAmp(false);
    }

    /* The codec shares the touch I2C bus, which Board::begin() has already
     * brought up -- do not call Wire.begin() again here. */
    uint8_t v = 0;
    esWrite(0x00, 0x1F);           /* reset                                */
    esWrite(0x00, 0x00);
    esWrite(0x00, 0x80);           /* power on                             */
    esWrite(0x01, 0x3F);           /* all clocks, MCLK from the MCLK pin   */

    /* Dividers for 6.144MHz MCLK at 16kHz, from the ES8311 coefficient
     * table: pre_div 3, pre_multi 1, adc/dac_div 1, lrck 0x00FF, bclk_div 4,
     * osr 0x10. Hardcoded because this firmware plays at exactly one rate. */
    Wire.beginTransmission(static_cast<uint8_t>(BOARD.audio.codecI2cAddress));
    Wire.write(0x02);
    Wire.endTransmission(false);
    if (Wire.requestFrom(static_cast<int>(BOARD.audio.codecI2cAddress), 1) == 1) {
        v = Wire.read();
    }
    esWrite(0x02, static_cast<uint8_t>((v & 0x07) | (2 << 5) | (1 << 3)));
    esWrite(0x03, 0x10);
    esWrite(0x04, 0x10);
    esWrite(0x05, 0x00);
    esWrite(0x06, 0x03);
    esWrite(0x07, 0x00);
    esWrite(0x08, 0xFF);

    esWrite(0x09, 3 << 2);         /* 16-bit in                            */
    esWrite(0x0A, 3 << 2);         /* 16-bit out                           */
    esWrite(0x0D, 0x01);           /* power up analogue                    */
    esWrite(0x0E, 0x02);
    esWrite(0x12, 0x00);           /* power up DAC                         */
    esWrite(0x13, 0x10);           /* enable output drive                  */
    esWrite(0x37, 0x08);           /* bypass DAC equaliser                 */

    /* The owner's stored level, not the compiled-in default: a console that
     * came back at 60% after every reboot would make the setting pointless.
     * Preferences is already open by this point -- Board::begin() calls
     * prefs_.begin() several lines above beginAudio(). */
    const uint8_t level = volume();
    applyCodecVolume(level);

    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = AUDIO_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.tx_desc_auto_clear = true;
    cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    cfg.use_apll = true;           /* 160MHz will not divide to 6.144MHz   */

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        cfg.use_apll = false;
        if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
            Serial.println("[audio] I2S install failed; console will be silent");
            return;
        }
        Serial.println("[audio] APLL unavailable; MCLK may be off-frequency");
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num = BOARD.audio.i2sMclk;
    pins.bck_io_num = BOARD.audio.i2sBclk;
    pins.ws_io_num = BOARD.audio.i2sWordSelect;
    pins.data_out_num = BOARD.audio.i2sDataOut;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
        Serial.println("[audio] I2S pins rejected; console will be silent");
        return;
    }

    i2s_zero_dma_buffer(I2S_NUM_0);
    codecUp = true;
    Serial.printf("[audio] codec 0x%02X up at %d Hz, volume %u%%%s\n",
                  BOARD.audio.codecI2cAddress, AUDIO_RATE, level,
                  soundEnabled() ? "" : " (muted)");

#elif GUME_HAS_AUDIO_DAC
    /* CYD-family boards drive the speaker straight from an ESP32 built-in DAC.
     * There is no external codec and no amplifier enable; the I2S peripheral
     * drives the DAC via I2S_DAC_BUILT_IN mode with no pin configuration of
     * its own -- which is exactly why the channel has to be chosen correctly,
     * because nothing here can be told a pin number.
     *
     * WHICH CHANNEL IS NOT A FREE CHOICE, AND THE NAMES ARE A TRAP.
     * From the IDF's own hal/i2s_types.h:
     *
     *   I2S_DAC_CHANNEL_RIGHT_EN = 1   maps to DAC channel 1 on GPIO25
     *   I2S_DAC_CHANNEL_LEFT_EN  = 2   maps to DAC channel 2 on GPIO26
     *
     * So "RIGHT" is GPIO25 and "LEFT" is GPIO26 -- the opposite of what the
     * numbering suggests, since DAC *channel 2* is the *left* enum. Picking
     * RIGHT for a speaker on GPIO26 puts the audio on the wrong pin and leaves
     * the speaker silent, which is indistinguishable from a wiring fault on a
     * board whose speaker path has never been measured. Deriving it from the
     * profile is what stops that being a matter of memory.
     *
     * NOTE: I2S_DAC_BUILT_IN and i2s_set_dac_mode() are IDF 4.4 (Arduino core
     * 2.0.17) APIs. They were removed in IDF 5.x. If the toolchain version is
     * ever bumped, this path will need rewriting with the new driver API. */
    constexpr int8_t DAC1_GPIO = 25;   // ESP32 DAC channel 1
    constexpr int8_t DAC2_GPIO = 26;   // ESP32 DAC channel 2
    static_assert(BOARD.audio.speakerPin == DAC1_GPIO ||
                  BOARD.audio.speakerPin == DAC2_GPIO,
                  "GUME_HAS_AUDIO_DAC needs a speakerPin on GPIO25 or GPIO26 -- "
                  "those are the only two ESP32 pins the built-in DAC reaches. "
                  "A speaker on any other pin needs a different backend, not a "
                  "different constant here.");
    const i2s_dac_mode_t dacChannel = (BOARD.audio.speakerPin == DAC2_GPIO)
                                          ? I2S_DAC_CHANNEL_LEFT_EN
                                          : I2S_DAC_CHANNEL_RIGHT_EN;
    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(
        I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    cfg.sample_rate = AUDIO_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.tx_desc_auto_clear = true;
    cfg.use_apll = false;          /* built-in DAC does not need APLL       */

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        Serial.println("[audio] I2S DAC install failed; console will be silent");
        return;
    }

    /* Route I2S output to the DAC channel this board's speaker actually hangs
     * off -- see the mapping above. */
    if (i2s_set_dac_mode(dacChannel) != ESP_OK) {
        Serial.println("[audio] I2S DAC mode failed; console will be silent");
        i2s_driver_uninstall(I2S_NUM_0);
        return;
    }

    /* Write mid-scale (0x8000) to silence the DAC output without snapping it
     * to zero (which would DC-bias the speaker through the ground reference)
     * or to full scale (which would pop). Mid-scale is the natural "nothing
     * playing" level for an unsigned 8-bit DAC driven by offset-binary 16-bit
     * words. The DMA will hold this value until the first real sample arrives,
     * rather than leaving the DAC in the undefined state from boot. */
    {
        const uint16_t silence = 0x8000;
        uint16_t buf[32];
        for (int i = 0; i < 32; ++i) buf[i] = silence;
        size_t written = 0;
        i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, 0);
    }

    dacUp = true;
    /* The pin comes from the profile rather than the format string: this line
     * is the first thing anybody reads when the speaker is silent, and a log
     * that names a pin the firmware did not actually drive sends them to a
     * meter instead of to the mapping above. */
    Serial.printf("[audio] DAC up at %d Hz (GPIO%d, DAC ch%d), volume %u%%%s\n",
                  AUDIO_RATE, static_cast<int>(BOARD.audio.speakerPin),
                  BOARD.audio.speakerPin == DAC2_GPIO ? 2 : 1,
                  volume(), soundEnabled() ? "" : " (muted)");
#endif
}

void Board::tickAudio() {
#if GUME_HAS_AUDIO_CODEC
    if (!codecUp) return;

    if (!playing && pendingAt >= pendingLength) {
        /* Nothing armed and nothing held back. Drop the amplifier so an idle
         * console is not powering it. */
        setAmp(false);
        return;
    }

    /* Non-blocking on purpose: whatever the DMA will take this frame it
     * takes, and the rest waits for the next one. At 96ms of DMA depth
     * against a 20ms frame there is always a comfortable margin, and a frame
     * is never spent waiting on audio.
     *
     * Samples are generated straight into the outgoing block, so the
     * synthesiser costs one static half-kilobyte buffer and nothing on the
     * heap or the stack. */
    for (;;) {
        if (pendingAt >= pendingLength) {
            if (!playing) break;
            int16_t* slots = reinterpret_cast<int16_t*>(pending);
            int n = 0;
            for (; n < 128; ++n) {
                int16_t sample = 0;
                if (!nextSample(sample)) break;
                slots[n * 2] = sample;
                slots[n * 2 + 1] = sample;   // one mono stream on both slots
            }
            if (n == 0) break;
            pendingLength = static_cast<size_t>(n) * 2 * sizeof(int16_t);
            pendingAt = 0;
        }

        size_t written = 0;
        if (i2s_write(I2S_NUM_0, pending + pendingAt, pendingLength - pendingAt,
                      &written, 0) != ESP_OK) {
            break;
        }
        pendingAt += written;
        if (pendingAt < pendingLength) break;   /* DMA full; finish next frame */
    }

#elif GUME_HAS_AUDIO_DAC
    if (!dacUp) return;

    if (!playing && pendingAt >= pendingLength) {
        /* Nothing to do this frame. The DAC holds the last mid-scale value
         * written by beginAudio(), which is correct idle behaviour. */
        return;
    }

    /* Same non-blocking DMA pattern as the codec path, but the DAC requires
     * unsigned 16-bit words in offset-binary format (sample + 32768), not
     * signed I2S. Volume is applied here via linear amplitude scaling -- there
     * is no register to write, unlike the ES8311.
     *
     * The 16-bit word layout for I2S_DAC_BUILT_IN with RIGHT_LEFT format is:
     * two 16-bit words per sample pair (L then R), and only the high 8 bits
     * of each reach the DAC. So we write the DAC value in the high byte and
     * zeros in the low byte, giving 8 bits of resolution. */
    const uint8_t vol = cachedVolume_;
    for (;;) {
        if (pendingAt >= pendingLength) {
            if (!playing) break;
            uint16_t* slots = reinterpret_cast<uint16_t*>(pending);
            int n = 0;
            for (; n < 128; ++n) {
                int16_t sample = 0;
                if (!nextSample(sample)) break;
                /* Apply software gain (linear amplitude, not dB), then shift
                 * to unsigned offset-binary. The DAC only sees the top 8 bits
                 * of a 16-bit word, so the +32768 offset goes in the high byte.
                 * This is genuinely linear -- do NOT copy the dB conversion
                 * from applyCodecVolume(); that is correct for a logarithmic
                 * register, not for a linear multiplier. */
                const int32_t scaled = static_cast<int32_t>(sample) * vol / 100;
                const uint16_t dac = static_cast<uint16_t>(
                    static_cast<int32_t>(scaled) + 32768);
                slots[n * 2] = dac;
                slots[n * 2 + 1] = dac;   // mono on both DAC channels
            }
            if (n == 0) break;
            pendingLength = static_cast<size_t>(n) * 2 * sizeof(uint16_t);
            pendingAt = 0;
        }

        size_t written = 0;
        if (i2s_write(I2S_NUM_0, pending + pendingAt, pendingLength - pendingAt,
                      &written, 0) != ESP_OK) {
            break;
        }
        pendingAt += written;
        if (pendingAt < pendingLength) break;   /* DMA full; finish next frame */
    }
#endif
}

void Board::beep(uint16_t frequency, uint16_t ms) {
#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
    if (!soundEnabled()) return;
    const Segment one[] = {tone(frequency, ms, 90)};
    armCue(one);
#else
    (void)frequency;
    (void)ms;
#endif
}

/* The whole vocabulary, in one place. A switch rather than a table indexed by
 * the enum because the arrays have different lengths and a table of
 * {pointer, count} pairs is the same size with one more thing to get wrong;
 * the compiler folds this into a jump table either way. There is no default
 * case on purpose -- adding a Sound without a cue is then a build warning
 * rather than a screen that silently makes no noise. */
void Board::playSound(Sound cue) {
#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
    /* The mute gate is here, at the one door every sound goes through --
     * including the boot phrase and both beeps. A switch labelled Mute that
     * left something still audible would be exactly the kind of half-truth
     * the About-page rules are written against. The RGB pulse is deliberately
     * NOT gated: beepOk() and beepError() pulse before they call this, so
     * muting takes the sound and leaves the light. */
    if (!soundEnabled()) return;
    switch (cue) {
        case Sound::Tap:       armCue(CUE_TAP); break;
        case Sound::Select:    armCue(CUE_SELECT); break;
        case Sound::Correct:   armCue(CUE_CORRECT); break;
        case Sound::Wrong:     armCue(CUE_WRONG); break;
        case Sound::Reveal:    armCue(CUE_REVEAL); break;
        case Sound::Coin:      armCue(CUE_COIN); break;
        case Sound::LevelUp:   armCue(CUE_LEVEL_UP); break;
        case Sound::Victory:   armCue(CUE_VICTORY); break;
        case Sound::GameOver:  armCue(CUE_GAME_OVER); break;
        case Sound::HighScore: armCue(CUE_HIGH_SCORE); break;
        case Sound::Countdown: armCue(CUE_COUNTDOWN); break;
        case Sound::Whoosh:    armCue(CUE_WHOOSH); break;
        case Sound::Pop:       armCue(CUE_POP); break;
        case Sound::Pad1:      armCue(CUE_PAD_1); break;
        case Sound::Pad2:      armCue(CUE_PAD_2); break;
        case Sound::Pad3:      armCue(CUE_PAD_3); break;
        case Sound::Pad4:      armCue(CUE_PAD_4); break;
        case Sound::Boot:      armCue(PHRASE_LETS_PLAY_BRAINO); break;
    }
#else
    (void)cue;
#endif
}

/* ------------------------------------------------------- sound settings
 *
 * Defined here rather than in Board.cpp because changing the level has to
 * reach the codec, and the codec lives in this file. They compile on every
 * board: a console with no speaker still has to answer "is sound on?" for the
 * settings screen, and answering it with a stored preference rather than with
 * `false` means the screen can distinguish "muted" from "this board cannot
 * make a sound", which are different things to tell an owner.
 *
 * Both mirror in RAM on first read and write through on change, so a stale
 * value is not reachable -- the pattern CLAUDE.md's responsiveness rule asks
 * for, and the reason is the same: `soundEnabled()` is on the path of every
 * cue in every game. */
bool Board::soundEnabled() {
    if (!soundCached_) {
        cachedSound_ = prefs_.getBool("sndOn", true);
        soundCached_ = true;
    }
    return cachedSound_;
}

void Board::setSoundEnabled(bool on) {
    cachedSound_ = on;
    soundCached_ = true;
    prefs_.putBool("sndOn", on);
#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
    /* Muting stops what is already sounding rather than letting it finish.
     * The boot phrase is a second and a half long and Mute is exactly the
     * control somebody reaches for while it is playing; "it will stop shortly"
     * is not what that press means. The amplifier drops on the next
     * tickAudio(), which is one frame away. */
    if (!on) {
        playing = false;
        pendingLength = 0;
        pendingAt = 0;
    }
#endif
}

uint8_t Board::volume() {
    if (!volumeCached_) {
        uint8_t stored = prefs_.getUChar("sndVol", AUDIO_VOLUME_DEFAULT);
        /* Clamp on the way out as well as on the way in: the ceiling is a
         * product decision that could be lowered in a later build, and a value
         * stored under the old one must not survive it. */
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
#if GUME_HAS_AUDIO_CODEC
    if (codecUp) applyCodecVolume(percent);
    /* DAC backend: volume is applied at sample-generation time in tickAudio()
     * from cachedVolume_, so no register write is needed here. */
#endif
}

/* The two beeps are the ones bring-up settled on in src/s3_diag.cpp: a rising
 * two-tone for yes, a low double buzz for no. They differ in pitch, length AND
 * shape, so they stay distinguishable to a player who is not listening
 * carefully -- which is every player.
 *
 * They keep their own names rather than becoming playSound(Sound::Correct) at
 * the call site: they are called from a few hundred places, they pulse the
 * RGB LED as well, and on a board with no codec that pulse is all they are. */
void Board::beepOk() {
    pulseRgb(0, 255, 40, 450);
    playSound(Sound::Correct);
}

void Board::beepError() {
    pulseRgb(255, 0, 0, 450);
    playSound(Sound::Wrong);
}
