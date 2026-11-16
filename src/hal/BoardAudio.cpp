// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "BoardAudioInternal.h"

/* Nothing here talks to a codec register or a DAC pad -- that is all
 * BoardAudioBackend.cpp -- so nothing here includes Wire or the DAC and RTC
 * headers. host/sound/ compiles this file on its own, which keeps it honest. */

#if GUME_HAS_AUDIO
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>
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

#if GUME_HAS_AUDIO
namespace audio {
/* ---------------------------------------------------------------- engine */

/* The per-sample synthesiser -- the script, the oscillator, the resonators
 * and the gain -- is AudioSynth.cpp, which is pure so that a PC can render
 * it. What is here is everything around it: arming, the lock, the task. */

bool codecUp = false;
bool dacUp = false;
#if GUME_HAS_AUDIO_I2S
bool i2sUp = false;
#endif
bool ampOn = false;


/* When generation finished, so the amplifier can outlive it by the depth of
 * the DMA. Zero means "still producing". */
uint32_t ampIdleSinceMs = 0;

/* Guards every variable the synthesiser owns -- the script, the oscillator
 * phase, the resonators, the gain and the pending tail. Two producers touch
 * them: the loop, via playSound()/setSoundEnabled(), and the generator task. */
SemaphoreHandle_t audioLock = nullptr;
TaskHandle_t audioTask = nullptr;

/* The DAC and bare-I2S backends scale amplitude in software, so the
 * generator needs the volume without a Board& to read it from. Mirrored rather than read through
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

/* A held note is re-armed on the wall clock while the generator runs one DMA
 * ahead of the speaker; the re-arm has to land before that lead has eaten the
 * note, with two 40ms frames -- the firmware's own threshold for a bad one --
 * to spare. See SOUND_NOTE_REARM_MS. */
static_assert(SOUND_NOTE_REARM_MS
                  + (DMA_BUF_COUNT * DMA_BUF_LEN + BLOCK_FRAMES) * 1000 / AUDIO_RATE
                  + 2 * 40
                  <= SOUND_NOTE_MS,
              "a held note would fade out before PianoGame asks for it again");

/* ------------------------------------------------------------- authoring
 *
 * A cue is a const Segment array; `arm()` copies it in and starts it. Copying
 * rather than keeping a pointer is deliberate -- a script outlives the call
 * that started it by up to a second and a half, and a pointer into a caller's
 * frame would be a use-after-free waiting for someone to write a cue with a
 * local table. */
void arm(const Segment* segments, uint8_t count) {
    if ((!codecUp && !dacUp && !i2sUp) || count == 0) return;
    if (count > MAX_SEGMENTS) count = MAX_SEGMENTS;

    if (audioLock != nullptr) xSemaphoreTake(audioLock, portMAX_DELAY);

    /* The engine decides whether this is a fresh sound or a held note
     * asked for again; see synthLoad(). */
    synthSetRate(AUDIO_RATE);
#if GUME_HAS_AUDIO_DAC
    /* Just under one step of the DAC's eight bits at full volume: a note left
     * to fall further there is not quieter, only coarser -- a buzz of the last
     * step flicking at the note's pitch -- so it is ended where that begins. */
    synthSetNoteFloor(0.008f);
#endif
    synthLoad(segments, count);

    /* The pending tail belongs to a sound that has just been superseded. */
    pendingLength = 0;
    pendingAt = 0;
    ampIdleSinceMs = 0;   // producing again: the tail timer restarts from here
    setAmp(true);

    if (audioLock != nullptr) xSemaphoreGive(audioLock);
    /* Wake the generator: it is asleep whenever nothing is playing, so without
     * this the sound would not start until its idle poll expired. */
    if (audioTask != nullptr) xTaskNotifyGive(audioTask);
}

/* A piano key is still down: Sound::NoteHold. Keeps the note held under the
 * lock, as arming does; see synthHold(). */
void holdNote() {
    if (!codecUp && !dacUp && !i2sUp) return;
    if (audioLock != nullptr) xSemaphoreTake(audioLock, portMAX_DELAY);
    synthHold();
    ampIdleSinceMs = 0;
    if (audioLock != nullptr) xSemaphoreGive(audioLock);
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
#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_I2S
    /* Signed 16-bit, both slots. The codec applies volume in its own register;
     * a bare I2S amplifier has no register, so the level is a LINEAR
     * multiplier here, exactly as on the DAC -- not applyCodecVolume()'s dB
     * conversion, which is right only for a logarithmic register. */
#if GUME_HAS_AUDIO_I2S
    const int32_t vol = outputVolume;
#endif
    int16_t* slots = reinterpret_cast<int16_t*>(dst);
    int n = 0;
    for (; n < frames; ++n) {
        int16_t sample = 0;
        if (!nextSample(sample)) break;
#if GUME_HAS_AUDIO_I2S
        sample = static_cast<int16_t>(static_cast<int32_t>(sample) * vol / 100);
#endif
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
             * timeout while the amplifier tail above still has to expire, and
             * none at all once it has: arm() wakes the task, and there is
             * nothing else for it to do. Waking every 25ms regardless was forty
             * times a second for a silent console, the second-largest share of
             * this firmware's wakeups on the handheld. A board with no
             * amplifier switch never sees ampOn fall (setAmp() returns early),
             * so it keeps the old timeout and loses nothing. */
            ulTaskNotifyTake(pdTRUE, ampOn ? pdMS_TO_TICKS(25) : portMAX_DELAY);
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
#endif  /* GUME_HAS_AUDIO */

/* The Board:: members below are the public face of all this; they reach the
 * synthesiser by name, as they did when it was one file. Guarded like the
 * namespace itself: on a board with no audio path there is no `audio` to
 * name, and every member below compiles to its silent arm. */
#if GUME_HAS_AUDIO
using namespace audio;
#endif

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
#if GUME_HAS_AUDIO
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
        synthFadeOut();
        pendingLength = 0;
        pendingAt = 0;
        if (audioLock != nullptr) xSemaphoreGive(audioLock);
    }
#endif
}

