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
static constexpr uint32_t DAC_I2S_PORT = I2S_NUM_0;
static constexpr int BOOT_PIN = 0;

/* The DAC channel that reaches GPIO26 on the classic ESP32.
 * GPIO25 = DAC1 = I2S_DAC_CHANNEL_LEFT_EN.
 * GPIO26 = DAC2 = I2S_DAC_CHANNEL_RIGHT_EN.
 * These are the only two pins the built-in DAC can use. */
static constexpr i2s_dac_mode_t DAC_CHANNEL = I2S_DAC_CHANNEL_RIGHT_EN;

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
    cfg.communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT;
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
 * With I2S_DAC_CHANNEL_RIGHT_EN, DAC2 (GPIO26) gets the right channel word.
 * Both words are set to the same value so the left-channel slot (unused) is
 * also mid-scale rather than leaving it undefined. */
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
            buf[i * 2]     = val;   /* left  channel (unused on CYD)     */
            buf[i * 2 + 1] = val;   /* right channel = DAC2 = GPIO26     */
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
