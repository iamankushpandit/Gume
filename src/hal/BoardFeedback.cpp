#include "Board.h"

#if GUME_HAS_AUDIO_CODEC
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>
#endif

#include "BleBeacon.h"

namespace {
constexpr uint8_t RGB_CH_R = 5;
constexpr uint8_t RGB_CH_G = 6;
constexpr uint8_t RGB_CH_B = 7;
constexpr uint32_t RGB_PWM_HZ = 5000;
constexpr uint8_t RGB_PWM_BITS = 8;

void attachRgbChannel(int8_t pin, uint8_t channel) {
    if (pin == PIN_NONE) return;
    ledcSetup(channel, RGB_PWM_HZ, RGB_PWM_BITS);
    ledcAttachPin(pin, channel);
}

/* Duty is inverted on a common-anode LED: full brightness is a line held low. */
void writeRgbChannel(int8_t pin, uint8_t channel, uint8_t level) {
    if (pin == PIN_NONE) return;
    ledcWrite(channel, BOARD.rgb.commonAnode ? 255 - level : level);
}
}

/* ------------------------------------------------------------------ audio
 *
 * On a board with an audio codec, beep() is real. On a board without one it
 * stays the no-op it always was and the LED pulse is the whole of the
 * feedback -- `beepOk()` has never promised a sound.
 *
 * WHY THIS IS NOT SIMPLY "PLAY A TONE".
 *
 * `beepOk()` is called from game code, in the middle of a frame, and the loop
 * has a 20ms budget. The bring-up probe in src/s3_diag.cpp plays its tones by
 * blocking on i2s_write until the whole note has been handed over -- 90 to
 * 300ms -- which is correct there, because a probe has no frame budget and
 * nothing else to do. Doing that here would blow the budget by an order of
 * magnitude on every correct answer in every game, and `Watchdog` would log
 * the stall.
 *
 * So a beep is RENDERED into a static buffer when it is asked for, and FED to
 * the I2S DMA a slice at a time by tickAudio(), which the runtime calls once
 * per frame beside tickRgb(). Every i2s_write here is non-blocking: if the
 * DMA is full the remainder waits for the next frame. A beep therefore costs
 * a few hundred microseconds in the frame that starts it and almost nothing
 * after.
 *
 * The buffer is static and mono. `CLAUDE.md`'s memory rule is that a fixed
 * allocation which is occasionally half empty beats anything that grows, and
 * this device has no compacting heap. Mono halves it; the codec is fed the
 * same sample on both slots.
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
#if GUME_HAS_AUDIO_CODEC
namespace {
constexpr int AUDIO_RATE = 16000;

/* The longest beep the firmware can ask for. beepError() is the long one at
 * roughly 300ms; the buffer is sized for that plus headroom. 5120 samples of
 * 16-bit mono is 10KB of static RAM, which on this device is a good trade for
 * zero heap traffic. */
constexpr int TONE_MAX_SAMPLES = AUDIO_RATE * 320 / 1000;

int16_t tonePcm[TONE_MAX_SAMPLES];
int  toneLength = 0;      // samples rendered
int  toneAt = 0;          // samples already handed to the DMA
bool codecUp = false;
bool ampOn = false;

void esWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(static_cast<uint8_t>(BOARD.audio.codecI2cAddress));
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

void setAmp(bool on) {
    if (BOARD.audio.ampEnablePin == PIN_NONE || ampOn == on) return;
    ampOn = on;
    const bool low = BOARD.audio.ampEnableActiveLow ? on : !on;
    digitalWrite(BOARD.audio.ampEnablePin, low ? LOW : HIGH);
}

/* Append one tone to the render buffer. Amplitude is ramped over 4ms at each
 * end: on a small driver the click of an abrupt start is louder than the note.
 * Silently truncates rather than overrunning -- a shortened beep is a far
 * better failure than a smashed stack. */
void renderTone(float hz, int ms, float amplitude) {
    const int want = AUDIO_RATE * ms / 1000;
    const int room = TONE_MAX_SAMPLES - toneLength;
    const int n = want < room ? want : room;
    if (n <= 0) return;

    const int ramp = AUDIO_RATE * 4 / 1000;
    const float step = 2.0f * PI * hz / AUDIO_RATE;
    float phase = 0.0f;
    for (int i = 0; i < n; ++i) {
        float env = amplitude;
        if (i < ramp) env *= static_cast<float>(i) / ramp;
        else if (i > n - ramp) env *= static_cast<float>(n - i) / ramp;
        tonePcm[toneLength + i] = static_cast<int16_t>(sinf(phase) * 30000.0f * env);
        phase += step;
        if (phase > 2.0f * PI) phase -= 2.0f * PI;
    }
    toneLength += n;
}

void renderSilence(int ms) {
    const int want = AUDIO_RATE * ms / 1000;
    const int room = TONE_MAX_SAMPLES - toneLength;
    const int n = want < room ? want : room;
    for (int i = 0; i < n; ++i) tonePcm[toneLength + i] = 0;
    if (n > 0) toneLength += n;
}

void startRender() { toneLength = 0; toneAt = 0; }

void commitRender() {
    if (toneLength > 0) setAmp(true);
}
}  // namespace
#endif  /* GUME_HAS_AUDIO_CODEC */

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

    const uint8_t volume = AUDIO_VOLUME_DEFAULT > AUDIO_VOLUME_MAX
                               ? AUDIO_VOLUME_MAX : AUDIO_VOLUME_DEFAULT;
    esWrite(0x32, static_cast<uint8_t>((volume * 256 / 100) - 1));

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
    Serial.printf("[audio] codec 0x%02X up at %d Hz, volume %u%%\n",
                  BOARD.audio.codecI2cAddress, AUDIO_RATE, volume);
#endif
}

void Board::tickAudio() {
#if GUME_HAS_AUDIO_CODEC
    if (!codecUp) return;

    if (toneAt >= toneLength) {
        /* Drained. Drop the amplifier so an idle console is not powering it. */
        if (toneLength > 0) {
            toneLength = 0;
            toneAt = 0;
            setAmp(false);
        }
        return;
    }

    /* Non-blocking on purpose: whatever the DMA will take this frame, it
     * takes; the rest waits. A frame must never be spent waiting on audio. */
    int16_t frame[128 * 2];
    while (toneAt < toneLength) {
        const int remaining = toneLength - toneAt;
        const int n = remaining < 128 ? remaining : 128;
        for (int i = 0; i < n; ++i) {
            frame[i * 2] = tonePcm[toneAt + i];
            frame[i * 2 + 1] = tonePcm[toneAt + i];
        }
        size_t written = 0;
        if (i2s_write(I2S_NUM_0, frame, n * 2 * sizeof(int16_t), &written, 0) != ESP_OK
                || written == 0) {
            break;
        }
        toneAt += static_cast<int>(written / (2 * sizeof(int16_t)));
        if (written < n * 2 * sizeof(int16_t)) break;   /* DMA full */
    }
#endif
}

void Board::beep(uint16_t frequency, uint16_t ms) {
#if GUME_HAS_AUDIO_CODEC
    if (!codecUp) return;
    startRender();
    renderTone(static_cast<float>(frequency), ms, 0.6f);
    commitRender();
#else
    (void)frequency;
    (void)ms;
#endif
}

/* The two beeps are the ones bring-up settled on in src/s3_diag.cpp: a rising
 * two-tone for yes, a low double buzz for no. They differ in pitch, length AND
 * shape, so they stay distinguishable to a player who is not listening
 * carefully -- which is every player. */
void Board::beepOk() {
    pulseRgb(0, 255, 40, 450);
#if GUME_HAS_AUDIO_CODEC
    if (codecUp) {
        startRender();
        renderTone(880.0f, 90, 0.55f);
        renderTone(1320.0f, 110, 0.55f);
        commitRender();
        return;
    }
#endif
    beep(1175, 55);
}

void Board::beepError() {
    pulseRgb(255, 0, 0, 450);
#if GUME_HAS_AUDIO_CODEC
    if (codecUp) {
        startRender();
        renderTone(160.0f, 120, 0.7f);
        renderSilence(40);
        renderTone(160.0f, 140, 0.7f);
        commitRender();
        return;
    }
#endif
    beep(220, 120);
}

/* Common-anode boards sink current, so a channel lights when its line is
 * driven LOW. A board that wires the LED the other way says so in its profile
 * rather than needing this inverted here. */
void Board::setRgb(bool red, bool green, bool blue) {
    const uint8_t on = BOARD.rgb.commonAnode ? LOW : HIGH;
    const uint8_t off = BOARD.rgb.commonAnode ? HIGH : LOW;
    if (BOARD.rgb.r != PIN_NONE) digitalWrite(BOARD.rgb.r, red ? on : off);
    if (BOARD.rgb.g != PIN_NONE) digitalWrite(BOARD.rgb.g, green ? on : off);
    if (BOARD.rgb.b != PIN_NONE) digitalWrite(BOARD.rgb.b, blue ? on : off);
}

void Board::setRgbEnabled(bool on) {
    prefs_.putBool("rgbOn", on);
    if (!on) setRgbColor(0, 0, 0);
}

bool Board::rgbEnabled() {
    return prefs_.getBool("rgbOn", true);
}

bool Board::bleBeaconEnabled() {
    return prefs_.getBool("bleOn", false);
}

void Board::setBleBeaconEnabled(bool on) {
    prefs_.putBool("bleOn", on);
    BleBeacon::setEnabled(on);
    /* Nearby play rides on this radio, so turning the beacon off must take it
     * with it. engine/NearbyPlay watches BleBeacon::enabled() every frame and
     * stands itself down; the stored preference is left alone so that turning
     * the beacon back on restores whatever the owner had chosen. */
}

/* Mirrored in RAM: NearbyPlay::tick() consults this once per frame to decide
 * whether the radio should be listening, and Preferences is flash-backed. */
bool Board::nearbyEnabled() {
    if (!nearbyCached_) {
        cachedNearby_ = prefs_.getBool("nearbyOn", false);
        nearbyCached_ = true;
    }
    return cachedNearby_;
}

void Board::setNearbyEnabled(bool on) {
    cachedNearby_ = on;
    nearbyCached_ = true;
    prefs_.putBool("nearbyOn", on);
}

void Board::setRgbColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!BOARD.hasRgbLed()) return;
    if (!rgbReady_) {
        attachRgbChannel(BOARD.rgb.r, RGB_CH_R);
        attachRgbChannel(BOARD.rgb.g, RGB_CH_G);
        attachRgbChannel(BOARD.rgb.b, RGB_CH_B);
        rgbReady_ = true;
    }
    rgbR_ = r;
    rgbG_ = g;
    rgbB_ = b;
    writeRgbChannel(BOARD.rgb.r, RGB_CH_R, r);
    writeRgbChannel(BOARD.rgb.g, RGB_CH_G, g);
    writeRgbChannel(BOARD.rgb.b, RGB_CH_B, b);
}

void Board::pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
    if (!rgbEnabled()) return;
    setRgbColor(r, g, b);
    rgbHoldUntilMs_ = millis() + ms;
}

void Board::tickRgb() {
    if (rgbHoldUntilMs_ == 0) return;
    if (millis() >= rgbHoldUntilMs_) {
        rgbHoldUntilMs_ = 0;
        setRgbColor(0, 0, 0);
    }
}
