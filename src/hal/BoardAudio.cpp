#include "BoardAudioInternal.h"

#if GUME_HAS_AUDIO_CODEC
#include <Wire.h>
#endif

#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>
#endif

#if GUME_HAS_AUDIO_DAC
#include <driver/dac_common.h>
#include <driver/rtc_io.h>
#include <soc/rtc_io_reg.h>
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
 * So playing a sound only ARMS a script, and a dedicated task generates the
 * samples. A sound outliving the screen that started it is normal: a script is
 * device state, not screen state, and nothing in leaveActiveGame() needs to
 * know about it.
 *
 * WHY GENERATION IS A TASK AND NOT A TICK, WHICH IS THE SECOND VERSION.
 *
 * The first version generated from the loop, in `tickAudio()`, on the argument
 * that the DMA is far deeper than a frame: 1536 frames is 96ms on the codec at
 * 16kHz and 64ms on the built-in DAC at 24kHz, against a 20ms budget. That is
 * true of a typical frame and false of the frame that matters. A full-screen
 * repaint is ~150KB over SPI, and a launcher page turn is that plus every tile
 * and icon -- comfortably past 96ms on the 4-inch panel. So the sequence was:
 * arm the cue, fill the DMA, spend one enormous frame painting, and come back
 * to a DMA that ran dry somewhere in the middle. The gap is heard as the sound
 * being chopped off, and it was reported exactly that way -- twice -- about
 * the launcher's Previous and Next buttons, which are the two controls in the
 * firmware that make a noise and then immediately repaint the whole screen.
 *
 * Deepening the DMA would have moved the threshold without removing it: the
 * worst frame is not bounded by anything, so any buffer is a bet on how slow a
 * screen is allowed to get. A task is bounded by nothing. It runs above the
 * loop task, blocks inside i2s_write() until the DMA has room, and therefore
 * refills DURING the repaint that would otherwise have starved it. This is the
 * same move CLAUDE.md's responsiveness rule already prescribes for the battery
 * gauge and the watchdog: work whose deadline is not the frame's does not
 * belong on the frame.
 *
 * The synthesiser state is shared with the loop -- `playSound()` arms from
 * game code -- so `audioLock` covers arming and generation. It is NOT held
 * across the blocking write: the task generates into its own buffer under the
 * lock, releases it, and only then blocks. `playSound()` therefore waits for
 * at most one 128-sample block, never for the DMA.
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
namespace audio {
/* ---------------------------------------------------------------- engine */

Segment script[MAX_SEGMENTS];
uint8_t scriptLength = 0;
uint8_t scriptAt = 0;
int32_t segTotal = 0;        // samples in the segment being played
int32_t segLeft = 0;         // samples of it still to generate
bool playing = false;
/* The script is spent and the last segment is fading out. See nextSample(). */
bool releasing = false;

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
    while (segLeft <= 0 && !releasing) {
        if (scriptAt + 1 >= scriptLength) {
            releasing = true;
            gainTarget = 0.0f;
            break;
        }
        ++scriptAt;
        startSegment();
    }
    if (releasing && gain < RELEASE_FLOOR) {
        playing = false;
        releasing = false;
        gain = 0.0f;
        out = 0;
        return false;
    }

    const Segment& s = script[scriptAt];
    if (!releasing) --segLeft;   // a release holds the segment's last pitch

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

/* When generation finished, so the amplifier can outlive it by the depth of
 * the DMA. Zero means "still producing". */
uint32_t ampIdleSinceMs = 0;

/* Guards every variable the synthesiser owns -- the script, the oscillator
 * phase, the resonators, the gain and the pending tail. Two producers touch
 * them: the loop, via playSound()/setSoundEnabled(), and the generator task. */
SemaphoreHandle_t audioLock = nullptr;
TaskHandle_t audioTask = nullptr;

/* The DAC backend scales amplitude in software, so the generator needs the
 * volume without a Board& to read it from. Mirrored rather than read through
 * Preferences for the usual reason: this is on the path of every sample. */
/* [[maybe_unused]] rather than a tighter guard, deliberately. Its reads and
 * writes sit in a mix of GUME_HAS_AUDIO_CODEC and GUME_HAS_AUDIO_DAC blocks
 * while the definition covers either, so on the codec-only Freenove it came
 * out defined and unreferenced. Reshaping those guards to match is a change
 * to which board scales volume where, on evidence I cannot read confidently
 * from the preprocessor alone -- and this is one byte. The attribute says
 * exactly what is true: used in some configurations, not all. */
[[maybe_unused]] uint8_t outputVolume = Board::AUDIO_VOLUME_DEFAULT;

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

    if (audioLock != nullptr) xSemaphoreTake(audioLock, portMAX_DELAY);

    /* Is this the SAME script arriving again while it is still sounding?
     *
     * That is a held piano key, not a new cue. The synthesiser has no note-on
     * and no sustain, so PianoGame keeps a held note alive by asking for it
     * again just before the last one runs out -- and snapping `phase` back to
     * zero at that seam is a step discontinuity in the waveform, which is a
     * click. Every 280ms. Which is heard, correctly, as a tone that is not
     * continuous.
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

    for (uint8_t i = 0; i < count; ++i) script[i] = segments[i];
    scriptLength = count;
    scriptAt = 0;

    /* What must NOT be reset: `gain`, so a cue arriving on top of one still
     * sounding slews between the two instead of clicking. What must be: the
     * resonators, which otherwise ring the previous phoneme into the new
     * sound, and the pending tail, which belongs to a sound that has just
     * been superseded. */
    if (!continuation) {
        for (Resonator& r : formant) {
            r.y1 = 0.0f;
            r.y2 = 0.0f;
        }
        phase = 0.0f;
        pitchCounter = 0;
    }
    pendingLength = 0;
    pendingAt = 0;
    playing = true;
    releasing = false;
    startSegment();
    ampIdleSinceMs = 0;   // producing again: the tail timer restarts from here
    setAmp(true);

    if (audioLock != nullptr) xSemaphoreGive(audioLock);
    /* Wake the generator: it is asleep whenever nothing is playing, so without
     * this the sound would not start until its idle poll expired. */
    if (audioTask != nullptr) xTaskNotifyGive(audioTask);
}

/* Generate up to `frames` samples into `dst`, in whichever word format this
 * board's backend wants, and return the number of BYTES produced. Zero means
 * the script finished.
 *
 * One generator for both drains -- the task and tickAudio()'s fallback -- so
 * the two cannot drift on the thing that is easiest to get wrong here, which
 * is the DAC's offset-binary word format. The caller holds audioLock. */
size_t generateBlock(uint8_t* dst, int frames) {
    if (!playing) return 0;
#if GUME_HAS_AUDIO_CODEC
    int16_t* slots = reinterpret_cast<int16_t*>(dst);
    int n = 0;
    for (; n < frames; ++n) {
        int16_t sample = 0;
        if (!nextSample(sample)) break;
        slots[n * 2] = sample;
        slots[n * 2 + 1] = sample;   // one mono stream on both slots
    }
    return static_cast<size_t>(n) * 2 * sizeof(int16_t);
#else
    /* The DAC requires unsigned 16-bit words in offset-binary format, not
     * signed I2S, and volume is a linear amplitude multiplier applied here --
     * there is no register to write, unlike the ES8311. Do NOT copy the dB
     * conversion from applyCodecVolume(); that is correct for a logarithmic
     * register and wrong for this. Only the high 8 bits of each word reach the
     * DAC, so the +32768 offset lands in the high byte. */
    uint16_t* slots = reinterpret_cast<uint16_t*>(dst);
    const uint8_t vol = outputVolume;
    int n = 0;
    for (; n < frames; ++n) {
        int16_t sample = 0;
        if (!nextSample(sample)) break;
        const int32_t scaled = static_cast<int32_t>(sample) * vol / 100;
        const uint16_t dac = static_cast<uint16_t>(scaled + 32768);
        slots[n * 2] = dac;
        slots[n * 2 + 1] = dac;   // mono on both DAC channels
    }
    if (n > 0) dacIdleLeft = DMA_BUF_COUNT * DMA_BUF_LEN;   // re-park when done
    return static_cast<size_t>(n) * 2 * sizeof(uint16_t);
#endif
}

/* Hold the amplifier open for the DMA depth after generation stops.
 *
 * Generation runs AHEAD of playback, so `playing` goes false when the last
 * sample has been GENERATED, not when it has been HEARD -- there is up to
 * 96ms (codec) or 64ms (DAC) still queued at that moment. Dropping the
 * amplifier there cuts the tail off every cue, and off the short ones, which
 * is most of the vocabulary, it cuts off the whole thing. */

void audioTaskFn(void*) {
    /* Task-owned, so the blocking write below reads a buffer nothing else can
     * touch. That is what lets the lock be released before the write. */
    static uint8_t out[BLOCK_FRAMES * 2 * sizeof(int16_t)];
    for (;;) {
        size_t bytes = 0;
        xSemaphoreTake(audioLock, portMAX_DELAY);
        bytes = generateBlock(out, BLOCK_FRAMES);
        if (bytes == 0) tickAmpTail();
        xSemaphoreGive(audioLock);

#if GUME_HAS_AUDIO_DAC
        /* Once per sound, as it ends: park the DAC at mid-scale. */
        if (bytes == 0 && dacIdleLeft > 0) fillDacIdle(pdMS_TO_TICKS(500));
#endif

        if (bytes == 0) {
            /* Nothing to make. Sleep until something is armed -- with a short
             * timeout, because the amplifier tail above still has to expire. */
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25));
            continue;
        }

        /* Blocking, and outside the lock. This is where the task spends
         * essentially all of its time: asleep inside the driver until the DMA
         * has room, which is precisely when more samples are due. The timeout
         * is a liveness backstop, not a schedule. */
        size_t written = 0;
        i2s_write(I2S_NUM_0, out, bytes, &written, pdMS_TO_TICKS(500));
    }
}

/* Bring the generator up. Called once, at the end of beginAudio(), and only
 * when a backend actually installed.
 *
 * Priority 3 against the Arduino loop task's 1, on the same core: the whole
 * point is that it PREEMPTS a long repaint. Pinning matters -- core 0 carries
 * the BLE and Wi-Fi stacks, and audio has no business competing with a radio
 * for a core when the core it needs to interrupt is the other one. */
void startAudioTask() {
    audioLock = xSemaphoreCreateMutex();
    if (audioLock == nullptr) {
        Serial.println("[audio] no mutex; generation stays on the loop");
        return;
    }
    if (xTaskCreatePinnedToCore(audioTaskFn, "braino-audio", 4096, nullptr, 3,
                                &audioTask, ARDUINO_RUNNING_CORE) != pdPASS) {
        audioTask = nullptr;
        Serial.println("[audio] no task; generation stays on the loop");
    }
}

}  // namespace audio
#endif  /* GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC */

/* The Board:: members below are the public face of all this; they reach the
 * synthesiser by name, as they did when it was one file. */
using namespace audio;

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
     * is not what that press means. It fades over the ~25ms release rather
     * than stopping dead, which would be the very click the release exists to
     * remove; the amplifier drops AMP_TAIL_MS after that.
     *
     * Under the lock: the generator task may be part way through a block, and
     * clearing the script from under it is exactly the race the lock exists
     * for. */
    if (!on) {
        if (audioLock != nullptr) xSemaphoreTake(audioLock, portMAX_DELAY);
        if (playing) {
            releasing = true;
            gainTarget = 0.0f;
        }
        pendingLength = 0;
        pendingAt = 0;
        if (audioLock != nullptr) xSemaphoreGive(audioLock);
    }
#endif
}

