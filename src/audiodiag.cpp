/* Standalone DAC audio bring-up probe for CYD-family boards.
 *
 * ------------------------------------------------------------------------
 * Why this is a separate firmware
 * ------------------------------------------------------------------------
 * The main app's frame budget (20ms), watchdog (12s) and non-blocking DMA
 * design are all correct for a product; they are exactly what prevents a
 * long blocking tone from working. This builds ALONE
 * (`build_src_filter = +<audiodiag.cpp>`, env:audiodiag) so there is
 * nothing to fight -- it can block, loop and generate audio as long as
 * it needs to.
 *
 * No Board, no Ui, no Watchdog, no game code. The only things pulled in
 * are TFT_eSPI (for the on-screen readout) and driver/i2s.h.
 *
 * ------------------------------------------------------------------------
 * What this validates
 * ------------------------------------------------------------------------
 * The three hardware facts that cannot be inferred from vendor schematics
 * alone:
 *   1. Does GPIO26 actually reach the JST speaker connector?
 *   2. Is the output level audible from an unamplifed 1-inch driver?
 *   3. Where does audible distortion begin (the threshold that sets
 *      maxVolume = 75 in the board profile)?
 *
 * Answers come from ears, not from a serial log. The serial log confirms
 * the I2S and DAC paths initialised; the speaker either makes sound or it
 * does not, and the distortion threshold is heard, not computed.
 *
 * ------------------------------------------------------------------------
 * IDF version note
 * ------------------------------------------------------------------------
 * I2S_DAC_BUILT_IN and i2s_set_dac_mode() are IDF 4.4 (Arduino core
 * 2.0.17) APIs. They were removed in IDF 5.x. If the platform is ever
 * bumped, this file and BoardAudio.cpp's DAC backend both need rewriting
 * with the new driver API. See BoardAudio.cpp's beginAudio() DAC branch.
 *
 * The same constant (GUME_HAS_AUDIO_DAC) in the same board header controls
 * both files; any change to one is a change to both.
 *
 * ------------------------------------------------------------------------
 * Using it
 * ------------------------------------------------------------------------
 *   pio run -e audiodiag -t upload && pio device monitor
 *
 * BOOT (IO0) steps to the next page. There is no back.
 *
 * Pages:
 *   SINE   -- 440 Hz, 60% volume. Change SINE_HZ in the source and
 *             reflash to find the distortion threshold for maxVolume.
 *   SWEEP  -- 200->4000 Hz linear sweep, 4 s loop. Confirms no dead
 *             spots in the speaker's range.
 *   STEPS  -- Volume 10/20/30...100% at 440 Hz, 500 ms per step.
 *             Confirms the curve is audibly monotonic.
 *   TONES  -- Short tone bursts approximating each Sound:: cue: tap,
 *             select, correct, wrong, coin, level-up, victory, game-over.
 *             Confirms the DAC output matches what the app will play.
 *
 * The synthesiser's full cue tables and the phoneme table live in
 * BoardAudio.cpp and are not duplicated here: this probe validates the
 * output path, not the cue content. Flash env:app once the path works and
 * the spoken phrase and every cue play naturally.
 */

#ifdef CYD_AUDIO_DIAG

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>
#include <math.h>
#include "BoardConfig.h"

/* ---- constants -------------------------------------------------------- */
static constexpr uint32_t SAMPLE_RATE = 16000;
static constexpr i2s_port_t DAC_I2S_PORT = I2S_NUM_0;
static constexpr int BOOT_PIN = 0;

/* From the LCDWIKI pin table for this board family: IO26 is the DAC audio
 * output, IO4 is the amplifier enable and is ACTIVE LOW. A probe states its
 * own pins rather than reading a board profile -- it exists to check whether
 * the profile is right, so taking them from it would prove nothing. */
static constexpr int SPEAKER_PIN = 26;
static constexpr int AMP_EN_PIN  = 4;
static constexpr uint8_t PWM_TEST_CH = 7;

/* The DAC channel that reaches GPIO26 on the classic ESP32.
 *
 * THE ENUM NAMES ARE INVERTED RELATIVE TO THE CHANNEL NUMBERS. Straight from
 * the IDF's hal/i2s_types.h, which is the only authority worth quoting here:
 *
 *   I2S_DAC_CHANNEL_RIGHT_EN = 1   maps to DAC channel 1 on GPIO25
 *   I2S_DAC_CHANNEL_LEFT_EN  = 2   maps to DAC channel 2 on GPIO26
 *
 * So DAC channel 2 -- the one wired to the speaker on these boards -- is the
 * *LEFT* enum. This probe exists to answer "does GPIO26 reach the speaker?",
 * and picking the wrong constant here makes it answer "no" for a board that is
 * wired perfectly well: the instrument would confirm a fault it caused itself.
 * Keep this in step with Board::beginAudio() -- change one, change both. */
static constexpr i2s_dac_mode_t DAC_CHANNEL = I2S_DAC_CHANNEL_LEFT_EN;

/* Same value that will go into boardProfile.audio.maxVolume for the CYD.
 * This is 75 by default; raise it to find the distortion threshold. If
 * a given board clips below 75, lower maxVolume in its board header. */
static constexpr uint8_t VOLUME_MAX = 75;

/* SINE page: frequency in Hz. Change and reflash to probe other frequencies. */
static constexpr float SINE_HZ = 440.0f;

/* ---- TFT --------------------------------------------------------------- */
TFT_eSPI tft;

static void tftBanner(const char* page, const char* line1 = nullptr,
                      const char* line2 = nullptr, const char* line3 = nullptr) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString("audiodiag", 4, 4);
    tft.setTextFont(4);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(page, 4, 30);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    if (line1) tft.drawString(line1, 4, 70);
    if (line2) tft.drawString(line2, 4, 92);
    if (line3) tft.drawString(line3, 4, 114);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("BOOT -> next page", 4, 220);
}

/* ---- I2S / DAC --------------------------------------------------------- */
static bool dacReady = false;

static bool dacBegin() {
    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(
        I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    /* MSB, not PCM_SHORT. In I2S_MODE_DAC_BUILT_IN the samples are latched
     * into the DAC by the I2S framer, and PCM short-sync framing does not
     * present them the way the DAC expects -- the peripheral runs, the writes
     * succeed, and nothing audible comes out. */
    cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.tx_desc_auto_clear = true;
    cfg.use_apll = false;

    if (i2s_driver_install(DAC_I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
        Serial.println("[audiodiag] I2S driver install FAILED");
        return false;
    }
    if (i2s_set_dac_mode(DAC_CHANNEL) != ESP_OK) {
        Serial.println("[audiodiag] i2s_set_dac_mode FAILED");
        i2s_driver_uninstall(DAC_I2S_PORT);
        return false;
    }
    /* Silence the DAC at mid-scale to avoid a DC pop on startup. */
    {
        const uint16_t silence = 0x8000;
        static uint16_t buf[128];
        for (auto& v : buf) v = silence;
        size_t written = 0;
        i2s_write(DAC_I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
    }
    Serial.printf("[audiodiag] DAC up on GPIO%d at %lu Hz\n",
                  26, (unsigned long)SAMPLE_RATE);
    return true;
}

/* Write a block of signed int16 samples. Applies linear volume scaling and
 * the signed->unsigned offset-binary conversion the DAC requires.
 *
 * Volume 0-100 maps linearly to amplitude 0-100% (NOT dB). The DAC register
 * is linear, so this is correct. Do NOT copy the dB conversion from
 * BoardAudio.cpp's applyCodecVolume() -- that is for the ES8311 logarithmic
 * register and would give the wrong curve here.
 *
 * I2S in RIGHT_LEFT stereo format expects TWO 16-bit words per audio frame.
 * Both words are set to the same value, so it does not matter which slot the
 * enabled DAC channel takes its sample from and the unused one is never left
 * undefined. That is deliberate: it makes the output correct whichever of the
 * two DAC pins a board turns out to use. */
static void dacWrite(const int16_t* samples, size_t count, uint8_t volumePct) {
    static uint16_t buf[128 * 2];   /* 128 stereo frames = 512 bytes */
    while (count > 0) {
        size_t chunk = count < 128 ? count : 128;
        for (size_t i = 0; i < chunk; ++i) {
            const int32_t scaled = static_cast<int32_t>(samples[i])
                                   * volumePct / 100;
            /* Offset-binary: mid-scale (0x8000) = silence for the DAC.
             * Only the high 8 bits reach the DAC in I2S_DAC_BUILT_IN mode,
             * so the offset must go in the high byte. */
            const uint16_t val = static_cast<uint16_t>(scaled + 32768);
            buf[i * 2]     = val;   /* left  slot -> DAC2 / GPIO26       */
            buf[i * 2 + 1] = val;   /* right slot -> DAC1 / GPIO25       */
        }
        size_t written = 0;
        i2s_write(DAC_I2S_PORT, buf, chunk * 2 * sizeof(uint16_t),
                  &written, portMAX_DELAY);
        count -= chunk;
    }
}

/* ---- waveform generators --------------------------------------------- */

/* Generate `durationMs` ms of a sine wave at `freqHz`, at given volume. */
static void playSine(float freqHz, uint32_t durationMs, uint8_t volumePct) {
    const uint32_t totalSamples = SAMPLE_RATE * durationMs / 1000;
    static int16_t samples[256];
    const float step = 2.0f * M_PI * freqHz / SAMPLE_RATE;
    float phase = 0.0f;
    uint32_t done = 0;
    while (done < totalSamples) {
        uint32_t chunk = totalSamples - done;
        if (chunk > 256) chunk = 256;
        for (uint32_t i = 0; i < chunk; ++i) {
            samples[i] = static_cast<int16_t>(sinf(phase) * 30000.0f);
            phase += step;
            if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
        }
        dacWrite(samples, chunk, volumePct);
        done += chunk;
    }
}

/* Generate `durationMs` ms of silence (mid-scale). */
static void playSilence(uint32_t durationMs) {
    const uint32_t totalSamples = SAMPLE_RATE * durationMs / 1000;
    static int16_t samples[256];
    memset(samples, 0, sizeof(samples));
    uint32_t done = 0;
    while (done < totalSamples) {
        uint32_t chunk = totalSamples - done;
        if (chunk > 256) chunk = 256;
        dacWrite(samples, chunk, 100);
        done += chunk;
    }
}

/* ---- BOOT button ------------------------------------------------------ */
static bool bootPressed() {
    return digitalRead(BOOT_PIN) == LOW;
}

static void waitBootRelease() {
    while (bootPressed()) delay(10);
}

/* Returns true when BOOT is pressed (with debounce). Blocks up to timeoutMs
 * and returns false if it expires without a press. Used in looping pages so
 * the BOOT key advances rather than having to wait for the loop. */
static bool waitBootOrTimeout(uint32_t timeoutMs) {
    const uint32_t end = millis() + timeoutMs;
    while (millis() < end) {
        if (bootPressed()) {
            waitBootRelease();
            return true;
        }
        delay(5);
    }
    return false;
}

/* ---- pages ------------------------------------------------------------- */

/* SINE: continuous tone at SINE_HZ. BOOT advances. */
static void pageSine() {
    char buf[48];
    snprintf(buf, sizeof(buf), "%.0f Hz, vol %u%%", SINE_HZ, VOLUME_MAX);
    tftBanner("SINE", buf,
              "Continuous tone at SINE_HZ.",
              "Listen for distortion above vol max.");
    Serial.printf("[audiodiag] SINE: %.0f Hz, volume %u%%\n",
                  SINE_HZ, VOLUME_MAX);
    /* Loop until BOOT is pressed. */
    while (true) {
        playSine(SINE_HZ, 500, VOLUME_MAX);
        if (bootPressed()) { waitBootRelease(); return; }
    }
}

/* SWEEP: 200->4000 Hz over 4 seconds, looped. */
static void pageSweep() {
    tftBanner("SWEEP", "200->4000 Hz, 4 s loop.", "Confirms no dead spots.",
              "Listen for gaps in the sweep.");
    Serial.println("[audiodiag] SWEEP: 200-4000 Hz");
    static int16_t samples[64];
    float freq = 200.0f;
    const float freqTop = 4000.0f;
    const uint32_t sweepSamples = SAMPLE_RATE * 4;   /* 4 seconds */
    float phase = 0.0f;
    uint32_t done = 0;
    while (true) {
        const float step = 2.0f * M_PI * freq / SAMPLE_RATE;
        for (int i = 0; i < 64; ++i) {
            samples[i] = static_cast<int16_t>(sinf(phase) * 30000.0f);
            phase += step;
            if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
        }
        dacWrite(samples, 64, VOLUME_MAX);
        done += 64;
        freq = 200.0f + (freqTop - 200.0f) * (float)done / sweepSamples;
        if (done >= sweepSamples) {
            done = 0;
            freq = 200.0f;
            /* Check BOOT between loops. */
        }
        if (bootPressed()) { waitBootRelease(); return; }
    }
}

/* STEPS: 10%->100% in 10% steps, 500 ms each, at 440 Hz. */
static void pageVolume() {
    tftBanner("VOLUME", "10%..100% in 500ms steps.",
              "Confirm audibly monotonic.", "Loops until BOOT.");
    Serial.println("[audiodiag] VOLUME steps");
    while (true) {
        for (uint8_t pct = 10; pct <= 100; pct += 10) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Volume: %u%%", pct);
            tft.fillRect(4, 145, 310, 20, TFT_BLACK);
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setTextFont(2);
            tft.drawString(buf, 4, 145);
            Serial.printf("[audiodiag] vol %u%%\n", pct);
            playSine(440.0f, 400, pct);
            playSilence(100);
            if (bootPressed()) { waitBootRelease(); return; }
        }
        playSilence(500);
        if (bootPressed()) { waitBootRelease(); return; }
    }
}

/* TONES: brief tone bursts that approximate the feel of each Sound:: cue.
 *
 * These are NOT the synthesiser's scripts -- the full cue scripts live in
 * BoardAudio.cpp and cannot be compiled here without pulling in all of Board.
 * The purpose is to confirm the DAC path is live, not to verify cue content;
 * flash env:app once the path works and the real cues will play. */
static void pageTones() {
    const struct { const char* name; float freqHz; uint32_t ms; } tones[] = {
        { "Tap",       800,  60 },
        { "Select",   1200,  80 },
        { "Correct",  1000, 150 },
        { "Wrong",     220, 200 },
        { "Coin",     1600,  80 },
        { "LevelUp",  1800, 200 },
        { "Victory",  2000, 500 },
        { "GameOver",  160, 600 },
    };
    tftBanner("TONES", "Tone bursts for each cue.", "Validates DAC output path.",
              "Press BOOT to advance.");
    Serial.println("[audiodiag] TONES");
    size_t idx = 0;
    while (true) {
        const auto& t = tones[idx];
        char buf[48];
        snprintf(buf, sizeof(buf), "%-10s  %.0f Hz  %lu ms",
                 t.name, t.freqHz, (unsigned long)t.ms);
        tft.fillRect(4, 145, 310, 20, TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextFont(2);
        tft.drawString(buf, 4, 145);
        Serial.printf("[audiodiag] %s\n", t.name);
        playSine(t.freqHz, t.ms, VOLUME_MAX);
        playSilence(200);
        idx = (idx + 1) % (sizeof(tones) / sizeof(tones[0]));
        if (bootPressed()) { waitBootRelease(); return; }
    }
}

/* ---- entrypoints ------------------------------------------------------- */
void setup() {
    Serial.begin(115200);
    Serial.println("[audiodiag] starting");

    /* BOOT button. */
    pinMode(BOOT_PIN, INPUT_PULLUP);

    /* AMPLIFIER ENABLE, AND THEN A TEST THAT DOES NOT USE THE DAC AT ALL.
     *
     * The LCDWIKI schematic and pin table for this board both say IO4 is
     * `AUDIO_EN`, the active-low shutdown of the onboard 8002-series amp, and
     * IO26 is the DAC output feeding it. MarioCruz/ESP32-EnvMonitor-v2 runs
     * the same board and does the same thing, with one detail neither document
     * states: it waits 20ms after enabling before making a sound. That settle
     * is copied here rather than guessed at.
     *
     * The PWM burst below is the whole point of this block. It drives IO26
     * with LEDC -- no I2S, no DAC, no sample pipeline -- which is what the
     * vendor's own 17_Buzzer demo does. So it separates the two questions that
     * silence cannot distinguish on its own:
     *
     *   heard  -> the speaker, the amp and the enable line are all fine, and
     *             the fault is in how we drive the DAC.
     *   silent -> the fault is before the DAC: no speaker plugged into JP1,
     *             the amp not enabled, or the wrong pins entirely.
     *
     * It runs before the I2S driver is installed, because LEDC and
     * I2S_DAC_BUILT_IN both want the same pad and the last one to claim it
     * wins. */
    pinMode(AMP_EN_PIN, OUTPUT);
    digitalWrite(AMP_EN_PIN, HIGH);          /* active low: start disabled */
    Serial.print("[audiodiag] amp enable (active low) on GPIO");
    Serial.println(AMP_EN_PIN);

    digitalWrite(AMP_EN_PIN, LOW);           /* enable */
    delay(20);                               /* settle -- from the reference */
    Serial.println("[audiodiag] PWM burst on the speaker pin: 440/880/1320 Hz");
    ledcSetup(PWM_TEST_CH, 440, 10);
    ledcAttachPin(SPEAKER_PIN, PWM_TEST_CH);
    const uint16_t tones[3] = {440, 880, 1320};
    for (uint8_t i = 0; i < 3; ++i) {
        ledcWriteTone(PWM_TEST_CH, tones[i]);
        ledcWrite(PWM_TEST_CH, 512);         /* 50% of 10-bit */
        delay(400);
    }
    ledcWrite(PWM_TEST_CH, 0);
    ledcDetachPin(SPEAKER_PIN);
    /* The amplifier STAYS ENABLED from here on. Every page below drives the
     * DAC, and a probe that silently switched the amp off between the PWM
     * burst and the DAC tests would make a working DAC path look broken --
     * which is the exact confusion this firmware exists to remove. */
    digitalWrite(AMP_EN_PIN, LOW);
    delay(20);
    Serial.println("[audiodiag] PWM burst done -- heard it or not, say which");

    /* TFT. */
    tft.init();
    tft.setRotation(1);   /* landscape: USB edge at bottom on CYD 2.8" */
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString("audiodiag init...", 4, 4);
    Serial.println("[audiodiag] TFT ok");

    /* DAC / I2S. */
    if (!dacBegin()) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("DAC FAILED -- see serial", 4, 40);
        for (;;) delay(1000);
    }

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("DAC ok -- press BOOT to start", 4, 40);
    Serial.printf("[audiodiag] board: %s\n", BOARD_NAME);
    Serial.printf("[audiodiag] GUME_HAS_AUDIO_DAC = %d\n", GUME_HAS_AUDIO_DAC);
    /* A DAC tone, immediately, with no button and no screen.
     *
     * The panel is wrong on some boards this probe gets flashed to -- it is a
     * boardless env, so its TFT config is one board's and the hardware may be
     * another's -- and a blank screen must not stop the one measurement that
     * matters. Reset therefore gives an unambiguous A/B by ear:
     *
     *   three short PWM tones  -- speaker, amp and enable line are good
     *   one long steady tone   -- the I2S -> built-in DAC path is good too
     *
     * Hearing the first and not the second isolates the fault to how the DAC
     * is driven, which is the only question left once the PWM burst sounds. */
    Serial.println("[audiodiag] DAC tone: 1 kHz for 2 s -- long steady tone");
    {
        static int16_t tone[512];
        for (int i = 0; i < 512; ++i) {
            tone[i] = static_cast<int16_t>(
                24000.0f * sinf(2.0f * PI * 1000.0f * i / SAMPLE_RATE));
        }
        const int blocks = (SAMPLE_RATE * 2) / 512;
        for (int b = 0; b < blocks; ++b) dacWrite(tone, 512, 80);
    }
    Serial.println("[audiodiag] DAC tone done -- did you hear a LONG tone?");

    /* Third test: the DAC pad driven DIRECTLY, with no I2S at all.
     *
     * PWM already proved the pad, the amp and the speaker. This proves the
     * DAC peripheral itself. Software-timed, which is far too jittery for a
     * product and perfectly adequate to answer one question:
     *
     *   heard, but the I2S tone was not -> the fault is the I2S framing
     *   neither heard                    -> the DAC peripheral or its routing
     *
     * ::dacWrite is Arduino's 8-bit DAC write. It needs the global scope
     * qualifier because this file already has a dacWrite of its own. */
    Serial.println("[audiodiag] DIRECT DAC tone: 1 kHz for 2 s, no I2S");
    {
        i2s_driver_uninstall(DAC_I2S_PORT);   /* release the pad first */
        static uint8_t wave[16];
        for (int i = 0; i < 16; ++i) {
            wave[i] = static_cast<uint8_t>(128 + 100 * sinf(2.0f * PI * i / 16));
        }
        const uint32_t until = millis() + 2000;
        while (millis() < until) {
            for (int i = 0; i < 16; ++i) {
                ::dacWrite(SPEAKER_PIN, wave[i]);
                delayMicroseconds(62);
            }
        }
        ::dacWrite(SPEAKER_PIN, 128);   /* park at mid-scale */
    }
    Serial.println("[audiodiag] DIRECT DAC done -- third long tone?");

    Serial.println("[audiodiag] press BOOT to start");
    waitBootOrTimeout(60000);
    waitBootRelease();
    dacReady = true;
}

void loop() {
    if (!dacReady) { delay(100); return; }
    pageSine();
    pageSweep();
    pageVolume();
    pageTones();
}

#endif /* CYD_AUDIO_DIAG */
