// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "BoardAudioInternal.h"

#if GUME_HAS_AUDIO_CODEC
#include <Wire.h>
#endif

#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
#include <driver/i2s.h>
#include <math.h>
#include <string.h>
#endif

#if GUME_HAS_AUDIO_DAC
#include <driver/dac_common.h>
#include <driver/rtc_io.h>
#include <soc/rtc_io_reg.h>
#endif

/* The hardware under the synthesiser.
 *
 * The ES8311's registers, the I2S peripheral, the amplifier enable line and
 * the built-in DAC's idle level -- everything whose correctness is a fact
 * about a board rather than about a sound. The synthesiser next door does not
 * know which of the two backends it is feeding, and nothing here knows what a
 * cue is.
 *
 * Two things in here are the scars of real bench failures and are commented
 * where they sit: the sample rate the built-in DAC actually produces (see
 * BoardAudio.cpp), and GPIO25 -- DAC channel 1 -- being the touch clock on two
 * of the CYD boards, which beginAudio() powers down and hands back to the GPIO
 * matrix before Board::begin() re-applies the touch pins.
 */
#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
namespace audio {

/* WHY THE TWO BACKENDS GENERATE AT DIFFERENT SAMPLE RATES.
 *
 * AUDIO_RATE is declared in BoardAudioInternal.h because all three files
 * need it; the measurement that chose its value is a fact about this
 * peripheral, so it is recorded here, beside the I2S setup it constrains.
 */
/* The sample rate the synthesiser generates at AND the rate the I2S peripheral
 * is configured with. One constant, deliberately: they cannot be allowed to
 * disagree, because nothing sounds wrong in a way that names the cause -- a
 * mismatch just makes every cue the wrong length and the wrong pitch.
 *
 * The two backends do not get the same number, and this is measured rather
 * than chosen. On the ESP32's built-in DAC (I2S_MODE_DAC_BUILT_IN) the clock
 * divider does not reach low sample rates: everything below 22050 Hz comes out
 * at some faster rate entirely, and the driver reports success either way.
 * Measured on an E32R32P with env:audiodiag_e32r32p, by timing how long a
 * blocking i2s_write() of a known number of frames takes to drain:
 *
 *     configured   actual     ratio
 *          8000     44260     5.53x
 *         11025     25316     2.30x
 *         12000     31128     2.59x
 *         16000     88642     5.54x   <- what this firmware used to ask for
 *         22050     22053     1.00x
 *         24000     24006     1.00x
 *         32000     32000     1.00x
 *         44100     44077     1.00x
 *         48000     48048     1.00x
 *
 * At or above 22050 the rate is exact; below it the error is large and not
 * even monotonic, which is the signature of a divider wrapping rather than
 * saturating. So the console was generating cues for 16000 samples a second
 * and the hardware was consuming them at nearly 89000: every sound played in
 * about a fifth of its intended length, an octave and a half sharp. That is
 * not heard as "too fast", it is heard as a click, or as a speaker that is
 * cutting out -- which is exactly how it was reported on the 3.2-inch and
 * 4-inch boards.
 *
 * i2s_get_clk() is no help here and is worth knowing about: it returned the
 * requested value in all nine cases above, including the wrong ones. It
 * reports what the driver was asked for, not what the peripheral is doing.
 *
 * 24000 is the choice rather than 22050 or 32000 because it is exact, it keeps
 * the DMA holding 1536 frames = 64ms (still three frame budgets of slack), and
 * it costs 1.5x the generation work of 16000 rather than 2x.
 *
 * The codec board is untouched at 16000. Its rate is not derived from this
 * divider -- the ES8311 is clocked from MCLK at 384x off the APLL, which is
 * exact at 16000 and known good on hardware. There is no reason to disturb the
 * one audio path that was never broken, and a shared constant here would have
 * done exactly that.
 *
 * If the platform is ever bumped to IDF 5.x the built-in DAC driver is
 * rewritten wholesale and this measurement must be repeated, not assumed. */

/* Generation runs AHEAD of playback, and that is the whole point of the DMA:
 * tickAudio() fills it as fast as it will take samples, so `playing` goes
 * false when the last sample has been GENERATED, not when it has been HEARD.
 * At 6 x 256 frames there is up to 96ms (codec) or 64ms (DAC) still queued at
 * that moment.
 *
 * Dropping the amplifier there cuts the tail off every cue, and on the short
 * ones -- which is most of the vocabulary -- it cuts off the whole thing: the
 * sound is sitting in the DMA when the amp stops being able to reproduce it.
 * So the amp is held for the DMA depth plus margin after generation ends. */
/* And held for a good while longer than that. The amplifier on the CYD boards
 * is an 8002 behind an enable line, and switching it is itself a pop -- which
 * at 150ms happened twice for every piano note, because a young player plays notes
 * further apart than that. Two seconds keeps it on through anything that is
 * being played and still lets an idle console drop it. */
constexpr uint32_t AMP_TAIL_MS = 2000;

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

#if GUME_HAS_AUDIO_DAC
/* Frames of mid-scale still to be written before an idle DAC is parked; zero
 * once it is. Set by generateBlock() whenever it produces anything, drained
 * by fillDacIdle(). See there. */
int dacIdleLeft = 0;
#endif

void tickAmpTail() {
    if (ampIdleSinceMs == 0) {
        ampIdleSinceMs = millis();
    } else if (millis() - ampIdleSinceMs >= AMP_TAIL_MS) {
        setAmp(false);
    }
}

#if GUME_HAS_AUDIO_DAC
/* SILENCE ON THE BUILT-IN DAC IS MID-SCALE, AND THE DRIVER DID NOT KNOW THAT.
 *
 * The DMA was set up with tx_desc_auto_clear, which fills a buffer with ZERO
 * words whenever there is nothing new to send. For a codec that is silence.
 * For this DAC a zero word is 0 V, while every sound is centred on mid-scale:
 * so the speaker line fell from mid-rail to ground at the end of every sound
 * and jumped back up at the start of the next. That is a click on both edges
 * of every cue on every DAC board -- the "kit ... kit" recorded around each
 * piano note, and the beeps that clicked all over the device.
 *
 * So on this backend auto-clear is off, which makes an idle DMA replay what is
 * already in its buffers, and this fills every one of them with mid-scale once
 * a sound has finished. What an idle DMA then replays is silence at the level
 * the sound ended on.
 *
 * With a wait it blocks until every buffer is parked; with a zero wait it
 * writes what fits and is called again next frame -- that is the loop-driven
 * fallback, which must never block. Both drains call it, because a DMA that
 * is never parked replays the tail of the last sound over and over. The task
 * calls it outside audioLock, like its own blocking write; the fallback calls
 * it under the lock with a zero wait, which cannot block. */
void fillDacIdle(TickType_t wait) {
    static uint16_t mid[BLOCK_FRAMES * 2];
    if (mid[0] != 0x8000) {
        for (uint16_t& w : mid) w = 0x8000;
    }
    while (dacIdleLeft > 0) {
        size_t written = 0;
        i2s_write(I2S_NUM_0, mid, sizeof(mid), &written, wait);
        dacIdleLeft -= static_cast<int>(written / (2 * sizeof(uint16_t)));
        if (written < sizeof(mid)) return;   // DMA full: the next call finishes
    }
}
#endif

}  // namespace audio
#endif  /* GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC */

/* The Board:: members below are the public face of all this; they reach the
 * synthesiser by name, as they did when it was one file. */
using namespace audio;

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
    cfg.dma_buf_count = DMA_BUF_COUNT;
    cfg.dma_buf_len = DMA_BUF_LEN;
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
    /* The amplifier first, and the same way the codec board does it: brought
     * up OFF, switched on only while something is playing. On this board that
     * is not a power optimisation so much as the difference between working
     * and silent -- see e32r40t.h, where IO4 was inherited as an LED channel
     * and was therefore being driven to its shutdown level on every boot. */
    if (BOARD.audio.ampEnablePin != PIN_NONE) {
        pinMode(BOARD.audio.ampEnablePin, OUTPUT);
        ampOn = true;
        setAmp(false);
    }

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
    /* MSB, not PCM_SHORT -- and this was the last thing standing between a
     * correctly configured DAC and complete silence.
     *
     * In I2S_MODE_DAC_BUILT_IN the I2S framer is what latches each sample into
     * the DAC. PCM short-frame sync does not present the word the way the DAC
     * expects, so the driver installs, i2s_write() reports every byte
     * accepted, the logs look perfect and nothing is audible. Confirmed on
     * hardware with env:audiodiag: the same tone was silent under PCM_SHORT
     * and audible under MSB, with nothing else changed.
     *
     * The codec path above uses STAND_I2S, which is right for the ES8311 and
     * is a different question -- do not unify them. */
    cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = DMA_BUF_COUNT;
    cfg.dma_buf_len = DMA_BUF_LEN;
    /* OFF, unlike the codec: a cleared buffer is 0 V here, not silence. See
     * fillDacIdle(), which is what an idle DMA replays instead. */
    cfg.tx_desc_auto_clear = false;
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

    /* HAND THE OTHER DAC PAD BACK.
     *
     * Installing I2S in I2S_MODE_DAC_BUILT_IN claims BOTH DAC pads, and the
     * i2s_set_dac_mode() call above only chooses which one carries samples --
     * it does not release the other. 5.5.0 is the proof: the two 2.8-inch
     * boards, whose resistive touch clock is GPIO25, drew perfectly and could
     * not be touched, and 5.5.1 answered by switching their audio off.
     *
     * So the unused channel is powered down and its pad returned to the GPIO
     * matrix here, explicitly, and Board::begin() re-applies the touch pin
     * setup after this function returns. The pad's state is logged either
     * side, because this is the line to read when a board comes up with sound
     * and no touch: "rtc_mux 1->0" means the pad was claimed and is now back.
     * The MUX_SEL and XPD_DAC bits sit at the same positions in both pad
     * registers, which the static_assert keeps honest. */
    {
        static_assert(RTC_IO_PDAC1_MUX_SEL == RTC_IO_PDAC2_MUX_SEL &&
                      RTC_IO_PDAC1_XPD_DAC == RTC_IO_PDAC2_XPD_DAC,
                      "DAC pad register layouts differ; read each by name");
        const bool speakerOnDac2 = BOARD.audio.speakerPin == DAC2_GPIO;
        const int8_t otherPad = speakerOnDac2 ? DAC1_GPIO : DAC2_GPIO;
        const uint32_t padReg = speakerOnDac2 ? RTC_IO_PAD_DAC1_REG : RTC_IO_PAD_DAC2_REG;
        const bool rtcBefore = GET_PERI_REG_MASK(padReg, RTC_IO_PDAC1_MUX_SEL) != 0;
        const bool outBefore = GET_PERI_REG_MASK(padReg, RTC_IO_PDAC1_XPD_DAC) != 0;
        dac_output_disable(speakerOnDac2 ? DAC_CHANNEL_1 : DAC_CHANNEL_2);
        rtc_gpio_deinit(static_cast<gpio_num_t>(otherPad));
        Serial.printf("[audio] GPIO%d released: rtc_mux %d->%d, dac_out %d->%d\n",
                      static_cast<int>(otherPad), rtcBefore ? 1 : 0,
                      GET_PERI_REG_MASK(padReg, RTC_IO_PDAC1_MUX_SEL) != 0 ? 1 : 0,
                      outBefore ? 1 : 0,
                      GET_PERI_REG_MASK(padReg, RTC_IO_PDAC1_XPD_DAC) != 0 ? 1 : 0);
    }

    /* Park the output at mid-scale (0x8000) -- the natural "nothing playing"
     * level for an unsigned 8-bit DAC driven by offset-binary words -- in
     * EVERY DMA buffer, not just the first few frames. With auto-clear off the
     * DMA replays its buffers until the first sound, and any left holding the
     * driver's zeros would put 0 V on the speaker line in between. */
    dacIdleLeft = DMA_BUF_COUNT * DMA_BUF_LEN;
    fillDacIdle(pdMS_TO_TICKS(200));

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

#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
    /* Last, and only if a backend actually installed: a generator with nothing
     * to drive would sit awake polling a driver that is not there. */
    if (codecUp || dacUp) startAudioTask();
#endif
}

void Board::tickAudio() {
#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
    if (!codecUp && !dacUp) return;

    /* The generator task does this work now, and does it during a long frame
     * as well as a short one -- which is the whole reason it exists. See the
     * note at the top of this file.
     *
     * This path remains as the fallback for a device where the task could not
     * be created at all (no mutex, or no memory for a 4KB stack). A console
     * that boots into a low-memory corner should still make its noises, just
     * with the old dependence on frames arriving promptly. It shares
     * generateBlock() with the task, so the one genuinely error-prone part --
     * the DAC's offset-binary word format -- has a single definition. */
    if (audioTask != nullptr) return;

    /* Zero wait, never blocking: this is the loop, and the frame budget is
     * 20ms. If the lock is busy the samples can wait for the next frame. */
    if (audioLock != nullptr && xSemaphoreTake(audioLock, 0) != pdTRUE) return;

    for (;;) {
        if (pendingAt >= pendingLength) {
            /* i2s_write() with a zero timeout reports how much it accepted
             * only after the fact, so a full DMA leaves the tail of a
             * generated block in hand. Throwing it away is audible as a
             * stutter on anything long enough to fill the DMA -- precisely the
             * spoken phrase -- and the synthesiser cannot be run backwards to
             * regenerate it, so it is held and written first next time. */
            const size_t bytes = generateBlock(pending, BLOCK_FRAMES);
            if (bytes == 0) break;
            pendingLength = bytes;
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

    if (!playing && pendingAt >= pendingLength) {
        tickAmpTail();
#if GUME_HAS_AUDIO_DAC
        if (dacIdleLeft > 0) fillDacIdle(0);
#endif
    }
    if (audioLock != nullptr) xSemaphoreGive(audioLock);
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
#if GUME_HAS_AUDIO_DAC
        outputVolume = stored;
#endif
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
#elif GUME_HAS_AUDIO_DAC
    /* DAC backend: volume is a linear multiplier applied while generating, so
     * there is no register to write -- only this mirror, which is what the
     * generator reads. It is a plain byte written by the loop and read by the
     * audio task; a torn read is not possible and the worst a race can do is
     * leave one block of samples at the previous level. */
    outputVolume = percent;
#endif
}
