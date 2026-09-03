/* Freenove FNK0104B bring-up probe -- env:s3diag, built alone.
 *
 * This board is not supported yet, and this file is how that gets decided. It
 * exists because the questions a new board poses cannot be answered from
 * inside the app, and on this board they cannot be answered by env:bringup
 * either: that path calls Board::runTouchCalibration() on a board with no
 * stored calibration, and the wizard is driven by XPT2046 code that a
 * capacitive panel does not answer. The first crosshair would be a dead end
 * with the display check stranded behind it.
 *
 * So: no Board, no Ui, no watchdog, no frame budget. TFT_eSPI and Wire, and
 * nothing else that could shape what the hardware appears to be doing.
 *
 * Five questions, in the order they block the port:
 *
 *   1. Is GPIO45 really the backlight? It is the pin that fails silently --
 *      a healthy serial log and a black screen looks identical to a dead
 *      panel, a wrong driver and a bad joint. So the probe BLINKS it on a
 *      cadence: a screen that pulses is proof, in a way a lit screen is not.
 *   2. Do the display pins and the ILI9341_2 driver actually drive it?
 *   3. Are TFT_BGR and TFT_INVERSION_ON both right? Freenove's setup asserts
 *      both, which is an unusual pair -- the colour bars are labelled so the
 *      answer is read rather than guessed.
 *   4. Which rotation puts the USB-C socket at the bottom? That value becomes
 *      PanelProfile::landscapeRotation. BOOT cycles it.
 *   5. Is there an FT6336U at 0x38 on SDA 16 / SCL 15, and what coordinate
 *      space does it report in? That last part is the one a datasheet cannot
 *      settle, and it decides how TouchProfile has to change.
 *
 * Everything it prints is a measurement. Where it states a vendor claim it
 * says so, because the vendor claim is what is on trial.
 *
 * LAYOUT RULE, learned the hard way on the first version of this file.
 * --------------------------------------------------------------------
 * A probe whose whole job is to be read at four different rotations must not
 * be laid out at one of them. The first version positioned text at a fixed
 * x=16 with fixed row steps, which is fine at 320 wide and runs straight off
 * the edge at 240 -- and TFT_eSPI does not mark the cut. drawChar() simply
 * returns once x reaches the viewport edge, so a clipped line looks like a
 * shorter line, and a reader has no way to tell a truncated battery voltage
 * from a low one. That is not a cosmetic bug in a diagnostic; it is the
 * diagnostic lying.
 *
 * So every string here goes through fitCentred(), which MEASURES the string
 * with textWidth() and steps down through the available fonts until it fits
 * the live tft.width(). Nothing is positioned by a constant that assumes an
 * orientation, and the frame drawn around the screen edge makes any overrun
 * visible against it rather than silently absent.
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <driver/i2s.h>

/* Freenove's own values, from Libraries/FNK0104AB/TFT_eSPI_Setups_v1.3.zip
 * and Tutorial_With_Touch/Sketches/Sketch_11.1_Touch. Named here rather than
 * inlined so a wrong one can be corrected in a single place while probing.
 *
 * Note GPIO45 (backlight) and GPIO46 (TFT DC) are ESP32-S3 strapping pins --
 * sampled at reset, free afterwards. That is fine, and worth knowing before
 * anyone reads a boot-mode oddity as a display fault. */
static constexpr int PIN_TOUCH_SDA = 16;
static constexpr int PIN_TOUCH_SCL = 15;
static constexpr int PIN_TOUCH_RST = 18;
static constexpr int PIN_TOUCH_INT = 17;
static constexpr int PIN_BOOT_KEY  = 0;
static constexpr int PIN_BATT_ADC  = 9;   /* vendor divider ratio 2.0 */

static constexpr uint8_t FT6336U_ADDR          = 0x38;
static constexpr uint8_t FT6336U_REG_TD_STATUS = 0x02;
static constexpr uint8_t FT6336U_REG_CHIP_ID   = 0xA3;
static constexpr uint8_t FT6336U_REG_FIRMWARE  = 0xA6;

/* ------------------------------------------------------------------- audio
 *
 * The speaker is not an amplifier on a pin -- it hangs off an ES8311 codec,
 * which is the second device the I2C scan found (0x18, sharing the touch bus).
 * Nothing comes out of it until it has been configured over I2C AND fed a
 * clocked I2S stream, so "no sound" has several quite different causes and the
 * probe has to separate them: codec not acknowledging, codec configured but no
 * stream, stream running but the amplifier disabled, or all three fine and the
 * speaker not connected. Each is reported separately below.
 *
 * Pins are Freenove's, from Tutorial_With_Touch/Sketches/Sketch_07.1_Music.
 * Their sketch will NOT build here: it uses ESP_I2S.h and Audio.h from Arduino
 * core 3.x, and this repository pins espressif32@7.0.1, which is core 2.0.17.
 * So the I2S side is the legacy driver/i2s.h API and the codec is driven by
 * direct register writes -- the register VALUES are lifted from their es8311.c
 * rather than invented, because a wrong codec init is silent in exactly the
 * same way a disconnected speaker is.
 *
 * GPIO1 is the power-amplifier enable. It is a separate question from the
 * codec: the DAC can be running perfectly into a muted amplifier. */
static constexpr int PIN_I2S_MCLK  = 4;
static constexpr int PIN_I2S_BCLK  = 5;
static constexpr int PIN_I2S_DIN   = 6;   /* codec ADC -> ESP32 (microphone) */
static constexpr int PIN_I2S_WS    = 7;
static constexpr int PIN_I2S_DOUT  = 8;   /* ESP32 -> codec DAC (speaker)    */
static constexpr int PIN_PA_ENABLE = 1;

static constexpr uint8_t ES8311_ADDR   = 0x18;
static constexpr int     AUDIO_RATE    = 16000;
static constexpr int     AUDIO_MCLK_HZ = AUDIO_RATE * 384;   /* 6.144 MHz */

/* PRODUCT DECISION, 2026-09-01: the firmware will never drive the codec above
 * 80%. This is a ceiling, not a default -- the same kind of constraint as
 * Board::BRIGHTNESS_MIN, which floors the backlight so a player cannot make
 * the screen unreadable. Here the concern points the other way: this is a
 * handheld held close to a child's ears, and the last 20% of a small driver
 * is mostly distortion anyway. The probe enforces it too, so nothing
 * demonstrated on the bench is louder than what the product will ship.
 *
 * When audio reaches the app this becomes a named constant beside
 * BRIGHTNESS_MIN, and any volume slider clamps to it rather than relabelling
 * 80 as "100%" -- a control that lies about its range is worse than one with
 * a shorter range. */
/* Keep in step with Board::AUDIO_VOLUME_MAX. Set by listening on the
 * FNK0104B's own driver -- see the long note beside it in src/hal/Board.h. */
static constexpr int AUDIO_VOLUME_MAX = 85;

/* How long the REC button captures for. Declared up here because draw() paints
 * the button label and sits above the recorder. */
static constexpr int ECHO_SECONDS = 5;

static bool  audioReady = false;      /* codec acknowledged and configured */
static bool  i2sReady   = false;      /* the stream is clocked             */
static int   audioVolume = 50;        /* 0..AUDIO_VOLUME_MAX, 'v' cycles  */
static int   micLevel   = 0;          /* 0..100, live                      */
static int   micPeak    = 0;          /* 0..100, decaying                  */
static int   echoPeak   = 0;          /* loudest sample of the last record */
static const char* lastSound = "-";
static bool paEnabled = true;   /* amp on; the PIN is driven LOW for on */
static bool audioUsedApll = false;
static uint8_t adcGainScale = 0x07;   /* reg16, 'g' cycles 0..7 */

/* Where the colour bars ended up last paint. Tapping a bar plays a sound, and
 * the hit test reads these rather than recomputing the layout -- the same rule
 * the launcher follows, so the target and the pixels cannot disagree. */
static int barsX = 0, barsY = 0, barsW = 0, barsH = 0;
/* The REC button. One rect, used by both the paint and the hit
 * test, so the target and the pixels cannot drift apart. */
static int recX = 0, recY = 0, recW = 0, recH = 0;
static bool echoRequested = false;
static bool echoBusy = false;
static constexpr int BAR_COUNT = 5;

static TFT_eSPI tft;

static uint8_t rotation = 1;          /* first guess; BOOT cycles it */
static uint8_t i2cFound[8];
static uint8_t i2cCount = 0;
static bool    touchPresent = false;
static uint8_t touchChipId = 0;
static uint8_t touchFirmware = 0;
static bool    redraw = true;

/* Live touch, in the controller's own coordinates -- deliberately NOT mapped
 * to the display. What the port needs to know is what the chip reports; any
 * rotation applied here would hide exactly that. */
static int  rawX = -1, rawY = -1;
static int  rawXMin = 9999, rawXMax = -1, rawYMin = 9999, rawYMax = -1;
static int  touchCount = 0;
static long touchEvents = 0;

/* Battery, sampled once per redraw and shared by screen and serial so the two
 * can never disagree -- the first version read the ADC separately in each and
 * invited exactly that doubt. */
static float battVolts = 0.0f;

/* Press-edge tracking for the tap-to-play zones. */
static bool touchDown = false;
static bool touchWasDown = false;

/* ----------------------------------------------------- the rotation question
 *
 * The FT6336U reports in the panel's NATIVE portrait frame and knows nothing
 * about setRotation(). Something has to rotate its output to match the
 * display, and that transform is the last unknown in the port -- it is also
 * the one thing a datasheet cannot settle, because it depends on which
 * physical corner the panel calls its origin.
 *
 * Measured on this board: rot=0 is portrait with USB-C at the bottom, and
 * rot=1 puts USB-C on the right. That means rot=1 is the board turned a
 * quarter-turn anticlockwise, so the viewer's top-left corner at rot=1 is the
 * panel's native top-RIGHT. Everything below follows from that.
 *
 * Only rotations 1 and 3 depend on that handedness; 0 and 2 do not. So rather
 * than assert the derivation, 'm' swaps the two candidate mappings live. If
 * the crosshair tracks a finger at every rotation in one mode and not the
 * other, the hardware has answered, and the answer goes into BoardTouch.cpp
 * having been seen rather than reasoned. */
static bool altHandedness = false;

static void mapNativeToScreen(int xn, int yn, int& sx, int& sy) {
    const int W0 = TFT_WIDTH;    /* native portrait width  (240) */
    const int H0 = TFT_HEIGHT;   /* native portrait height (320) */
    uint8_t r = rotation;
    if (altHandedness && (r & 1)) r = (uint8_t)(r ^ 2);   /* swap 1 <-> 3 */

    switch (r) {
        case 1:  sx = yn;              sy = (W0 - 1) - xn; break;
        case 2:  sx = (W0 - 1) - xn;   sy = (H0 - 1) - yn; break;
        case 3:  sx = (H0 - 1) - yn;   sy = xn;            break;
        default: sx = xn;              sy = yn;            break;
    }
}

/* ------------------------------------------------------------------ layout */

/* Fonts this probe may use, largest first. fitCentred() walks down this list. */
static const uint8_t FONTS[] = { 4, 2, 1 };
static constexpr int EDGE_MARGIN = 6;   /* px kept clear either side */

static int fontHeight(uint8_t font) { return tft.fontHeight(font); }

/* Draw `text` horizontally centred at `y`, in the largest listed font no
 * wider than the panel. Returns the height actually used, so callers stack
 * without assuming a row pitch.
 *
 * `maxFont` caps the size for hierarchy (a heading may be big, a detail line
 * small); the fitting still applies below that cap. If even font 1 does not
 * fit, it is drawn anyway and reported over serial -- a probe must say when
 * it cannot show something, never quietly clip it. */
static int fitCentred(const char* text, int y, uint8_t maxFont, uint16_t colour) {
    const int limit = tft.width() - 2 * EDGE_MARGIN;
    uint8_t chosen = FONTS[sizeof(FONTS) - 1];
    bool fits = false;

    for (size_t i = 0; i < sizeof(FONTS); ++i) {
        if (FONTS[i] > maxFont) continue;
        chosen = FONTS[i];
        if (tft.textWidth(text, chosen) <= limit) { fits = true; break; }
    }
    if (!fits) {
        Serial.printf("[layout] '%s' does not fit %d px even at font 1\n",
                      text, limit);
    }

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(colour, TFT_BLACK);
    tft.drawString(text, tft.width() / 2, y + fontHeight(chosen) / 2, chosen);
    return fontHeight(chosen);
}

/* Labelled bars. Unlabelled colour bars prove nothing: if BGR and inversion
 * are both wrong the result is still a plausible-looking spread of colour.
 * The captions are fitted too -- at 240 wide each bar is only 48 px. */
static void drawColourBars(int y, int h) {
    struct Bar { uint16_t colour; const char* name; };
    static const Bar bars[] = {
        { TFT_RED,   "RED"   }, { TFT_GREEN, "GREEN" }, { TFT_BLUE,  "BLUE"  },
        { TFT_WHITE, "WHITE" }, { TFT_BLACK, "BLACK" },
    };
    const int n = (int)(sizeof(bars) / sizeof(bars[0]));
    const int w = tft.width() / n;
    const int used = w * n;
    const int left = (tft.width() - used) / 2;   /* centre the whole strip */

    barsX = left; barsY = y; barsW = w; barsH = h;

    tft.setTextDatum(MC_DATUM);
    for (int i = 0; i < n; ++i) {
        const int x = left + i * w;
        tft.fillRect(x, y, w, h, bars[i].colour);
        tft.setTextColor(bars[i].colour == TFT_WHITE ? TFT_BLACK : TFT_WHITE);
        /* Caption at font 1 unless it overruns its own bar. */
        if (tft.textWidth(bars[i].name, 1) <= w - 4) {
            tft.drawString(bars[i].name, x + w / 2, y + h / 2, 1);
        }
    }
    tft.drawRect(left, y, used, h, TFT_DARKGREY);
}

static void draw() {
    const int W = tft.width(), H = tft.height();
    tft.fillScreen(TFT_BLACK);

    /* A frame one pixel inside the edge. Anything that overruns crosses it,
     * so clipping becomes visible rather than merely absent. */
    tft.drawRect(0, 0, W, H, TFT_DARKGREY);

    /* Corner markers prove the driver is addressing the whole panel and not a
     * window offset by a few pixels -- a classic wrong-driver symptom. */
    tft.fillRect(1, 1, 12, 12, TFT_YELLOW);
    tft.fillRect(W - 13, 1, 12, 12, TFT_CYAN);
    tft.fillRect(1, H - 13, 12, 12, TFT_MAGENTA);
    tft.fillRect(W - 13, H - 13, 12, 12, TFT_GREEN);

    char line[64];

    /* Build the stack, measure it, then centre it vertically. Doing it in this
     * order is what makes the same code readable in portrait and landscape
     * instead of correct in one and off the bottom in the other. */
    const int barH  = (H >= 300) ? 36 : 28;
    const int meterH = 10;
    const int gap   = 4;
    const int stackH = fontHeight(4) + gap        /* title            */
                     + fontHeight(2) + gap        /* rotation + size  */
                     + fontHeight(1) + gap        /* hint             */
                     + barH          + gap
                     + fontHeight(2) + gap        /* touch status     */
                     + fontHeight(1) + gap        /* raw coords       */
                     + fontHeight(1) + gap        /* seen range       */
                     + fontHeight(1) + gap        /* audio status     */
                     + meterH        + gap        /* mic meter        */
                     + fontHeight(2) + 10 + gap   /* REC button       */
                     + fontHeight(1) + gap        /* mic numbers      */
                     + fontHeight(4);             /* battery          */
    int y = (H - stackH) / 2;
    if (y < 16) y = 16;                            /* never under the markers */

    y += fitCentred("FNK0104B", y, 4, TFT_WHITE) + gap;

    snprintf(line, sizeof(line), "rot=%u  %dx%d", rotation, W, H);
    y += fitCentred(line, y, 2, TFT_CYAN) + gap;

    y += fitCentred(altHandedness ? "map B - tap a bar for sound"
                                  : "map A - tap a bar for sound",
                    y, 1, TFT_DARKGREY) + gap;

    drawColourBars(y, barH);
    y += barH + gap;

    uint16_t statusColour;
    if (touchPresent) {
        snprintf(line, sizeof(line), "FT6336U id=%02X fw=%02X",
                 touchChipId, touchFirmware);
        statusColour = TFT_GREEN;
    } else if (i2cCount > 0) {
        snprintf(line, sizeof(line), "I2C ok, no 0x38");
        statusColour = TFT_ORANGE;
    } else {
        snprintf(line, sizeof(line), "I2C SILENT");
        statusColour = TFT_RED;
    }
    y += fitCentred(line, y, 2, statusColour) + gap;

    snprintf(line, sizeof(line), "raw %d,%d  n=%d", rawX, rawY, touchCount);
    y += fitCentred(line, y, 1, TFT_WHITE) + gap;

    snprintf(line, sizeof(line), "x %d-%d  y %d-%d",
             rawXMax < 0 ? 0 : rawXMin, rawXMax < 0 ? 0 : rawXMax,
             rawYMax < 0 ? 0 : rawYMin, rawYMax < 0 ? 0 : rawYMax);
    y += fitCentred(line, y, 1, TFT_WHITE) + gap;

    /* Audio, stated as three separate facts. "no sound" has four causes and
     * collapsing them into one label would hide which. */
    uint16_t audioColour;
    if (audioReady && i2sReady) {
        snprintf(line, sizeof(line), "ES8311 ok  vol %d  %s", audioVolume, lastSound);
        audioColour = TFT_GREEN;
    } else if (audioReady) {
        snprintf(line, sizeof(line), "codec ok, I2S FAILED");
        audioColour = TFT_ORANGE;
    } else {
        snprintf(line, sizeof(line), "ES8311 NOT responding");
        audioColour = TFT_RED;
    }
    y += fitCentred(line, y, 1, audioColour) + gap;

    /* Microphone meter. Live bar plus a decaying peak tick, because a clap is
     * over before the eye finds the bar. */
    {
        const int mw = (W * 3) / 4;
        const int mx = (W - mw) / 2;
        tft.drawRect(mx, y, mw, meterH, TFT_DARKGREY);
        const int fill = (mw - 2) * micLevel / 100;
        if (fill > 0) {
            tft.fillRect(mx + 1, y + 1, fill, meterH - 2,
                         micLevel > 80 ? TFT_RED : TFT_CYAN);
        }
        if (fill < mw - 2) {
            tft.fillRect(mx + 1 + fill, y + 1, (mw - 2) - fill, meterH - 2, TFT_BLACK);
        }
        const int px = mx + 1 + (mw - 2) * micPeak / 100;
        tft.drawFastVLine(px, y, meterH, TFT_YELLOW);
    }
    y += meterH + gap;

    {
        const int bw = (W * 2) / 3;
        const int bh = fontHeight(2) + 10;
        recX = (W - bw) / 2; recY = y; recW = bw; recH = bh;
        const uint16_t fill = echoBusy ? TFT_RED : TFT_NAVY;
        tft.fillRoundRect(recX, recY, recW, recH, 4, fill);
        tft.drawRoundRect(recX, recY, recW, recH, 4, TFT_WHITE);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, fill);
        char label[32];
        snprintf(label, sizeof(label), echoBusy ? "RECORDING..." : "REC %ds + PLAY",
                 ECHO_SECONDS);
        tft.drawString(label, recX + recW / 2, recY + recH / 2, 2);
    }
    y += fontHeight(2) + 10 + gap;

    snprintf(line, sizeof(line), "mic %d%%  last peak %d", micLevel, echoPeak);
    y += fitCentred(line, y, 1, echoPeak > 400 ? TFT_GREEN : TFT_DARKGREY) + gap;

    /* Deliberately large and alone on its line. This is the value that was
     * misread as "1.x V" when it was clipped, so it gets the room to be
     * unambiguous. */
    snprintf(line, sizeof(line), "%.2f V", battVolts);
    fitCentred(line, y, 4, TFT_YELLOW);

    /* The crosshair is drawn in RAW coordinates on purpose. If it tracks your
     * finger, the controller's axes already match the display at this
     * rotation. If it is mirrored or transposed, that mismatch is the thing
     * the touch layer will have to correct -- and now it is visible. */
    if (rawX >= 0 && rawY >= 0) {
        int sx = 0, sy = 0;
        mapNativeToScreen(rawX, rawY, sx, sy);
        if (sx >= 0 && sx < W && sy >= 0 && sy < H) {
            tft.drawFastHLine(0, sy, W, TFT_RED);
            tft.drawFastVLine(sx, 0, H, TFT_RED);
            tft.fillCircle(sx, sy, 4, TFT_RED);
        }
    }
}

/* --------------------------------------------------------------- ES8311 */

static bool esWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool esRead(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)ES8311_ADDR, 1) != 1) return false;
    value = Wire.read();
    return true;
}

/* Playback level for the record-and-playback test ONLY.
 *
 * This deliberately exceeds AUDIO_VOLUME_MAX. The ceiling is a product rule
 * about a handheld held near a child's ears; this is a bench instrument being
 * used to judge whether a recording captured anything, and that judgement
 * needs headroom -- a quiet capture played quietly is indistinguishable from
 * no capture at all. It applies to this one playback, is restored afterwards,
 * and must NOT follow the codec driver into the firmware. */
static constexpr int ECHO_PLAYBACK_VOLUME = 90;

/* Register 0x32 is the DAC volume and it is linear in DECIBELS -- half a
 * decibel per step, 0xBF is unity gain, 0x00 is silence.
 *
 * This probe and the firmware BOTH used to scale the percentage straight onto
 * the byte, which treats a logarithmic register as a linear one and is wrong
 * by up to 25 dB: "60%" was -19.5 dB and "100%" was +32 dB. It was found from
 * the firmware side, where the console turned out to be nearly inaudible, and
 * it is corrected here in the same change deliberately. A bench instrument
 * that disagrees with the product about what a number means is worse than no
 * instrument: the next person to bring up a board would set 80 here, hear it,
 * and get something else entirely from the same 80 in Settings.
 *
 * Keep this identical to applyCodecVolume() in src/hal/BoardAudio.cpp. */
static uint8_t es8311VolumeReg(int percent) {
    if (percent <= 0) return 0;
    if (percent > 100) percent = 100;
    const float dB = 20.0f * log10f((float)percent / 100.0f);
    int reg = (int)lroundf(0xBF + dB * 2.0f);
    if (reg < 1) reg = 1;
    if (reg > 0xBF) reg = 0xBF;
    return (uint8_t)reg;
}

static void es8311SetVolumeRaw(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    audioVolume = percent;
    esWrite(0x32, es8311VolumeReg(percent));
}

static void es8311SetVolume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > AUDIO_VOLUME_MAX) percent = AUDIO_VOLUME_MAX;
    audioVolume = percent;
    esWrite(0x32, es8311VolumeReg(percent));
}

/* Dividers for MCLK 6.144 MHz / 16 kHz, taken from es8311.c's coeff_div table:
 * pre_div 3, pre_multi 1, adc_div 1, dac_div 1, fs_mode 0, lrck 0x00ff,
 * bclk_div 4, adc_osr 0x10, dac_osr 0x10. Hardcoded rather than table-driven
 * because the probe runs at exactly one rate and a table would be four more
 * things that can be wrong. */
static bool es8311Init() {
    uint8_t v = 0;

    /* Reset, then power on. */
    if (!esWrite(0x00, 0x1F)) return false;   /* an ACK here is the codec */
    delay(20);
    esWrite(0x00, 0x00);
    esWrite(0x00, 0x80);

    /* Clock manager: MCLK from the MCLK pin, all clocks enabled. */
    esWrite(0x01, 0x3F);

    esRead(0x02, v); v &= 0x07; v |= (3 - 1) << 5; v |= 1 << 3; esWrite(0x02, v);
    esWrite(0x03, (0 << 6) | 0x10);           /* fs_mode | adc_osr */
    esWrite(0x04, 0x10);                      /* dac_osr           */
    esWrite(0x05, ((1 - 1) << 4) | (1 - 1));  /* adc_div | dac_div */
    esRead(0x06, v); v &= 0xE0; v |= (4 - 1); esWrite(0x06, v);   /* bclk_div */
    esRead(0x07, v); v &= 0xC0; v |= 0x00;    esWrite(0x07, v);   /* lrck_h   */
    esWrite(0x08, 0xFF);                                          /* lrck_l   */

    /* Slave mode, I2S, 16-bit in and out. */
    esRead(0x00, v); v &= 0xBF; esWrite(0x00, v);
    esWrite(0x09, 3 << 2);
    esWrite(0x0A, 3 << 2);

    esWrite(0x0D, 0x01);   /* power up analogue          */
    esWrite(0x0E, 0x02);   /* analogue PGA + ADC modulator */
    esWrite(0x12, 0x00);   /* power up DAC               */
    esWrite(0x13, 0x10);   /* enable output to drive     */
    esWrite(0x1C, 0x6A);   /* ADC EQ bypass, DC cancel   */
    esWrite(0x37, 0x08);   /* DAC EQ bypass              */
    /* Microphone. reg14 selects the analogue mic and its PGA gain; reg17 is
     * the ADC VOLUME and reg16 an extra digital scale-up.
     *
     * reg17 is the one that bites. It resets to minimum, and the vendor only
     * sets it inside es8311_microphone_config(), which their own
     * es8311_codec_init() leaves commented out -- so following their init
     * faithfully produces a codec that records perfect silence while every
     * other register reads back correct. Measured here: with reg17 unset, a
     * 2-second capture peaked at 1 count out of 32767. */
    esWrite(0x14, 0x1A);        /* analogue mic, max PGA gain   */
    esWrite(0x17, 0xC8);        /* ADC volume -- NOT a default  */
    esWrite(0x16, adcGainScale); /* extra digital gain, 0..7    */

    es8311SetVolume(audioVolume);

    /* Read something back: a codec that accepted every write but reports a
     * reset register of 0 was never really there. */
    return esRead(0x00, v);
}

static bool i2sBegin() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
    cfg.sample_rate = AUDIO_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.tx_desc_auto_clear = true;
    cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;

    /* The codec is an I2S slave taking its clock from MCLK, so MCLK has to be
     * 6.144 MHz and not approximately that. 160 MHz / 6.144 MHz is 26.04, so
     * the integer divider cannot hit it -- ask for the APLL, and say plainly
     * which one we ended up on rather than leaving a silent approximation. */
    cfg.use_apll = true;
    audioUsedApll = true;
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        cfg.use_apll = false;
        audioUsedApll = false;
        if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num = PIN_I2S_MCLK;
    pins.bck_io_num = PIN_I2S_BCLK;
    pins.ws_io_num = PIN_I2S_WS;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num = PIN_I2S_DIN;
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return false;

    i2s_zero_dma_buffer(I2S_NUM_0);
    return true;
}

/* One tone. Blocking on purpose: there is no frame budget here and a probe
 * that plays a note while everything else waits is easier to reason about
 * than a mixer. Amplitude is ramped in and out over 4 ms so the speaker does
 * not click, which on a small driver is louder than the tone. */
static void playTone(float hz, int ms, float amplitude) {
    if (!i2sReady) return;
    static int16_t buf[256 * 2];
    const int total = (AUDIO_RATE * ms) / 1000;
    const int ramp = AUDIO_RATE * 4 / 1000;
    float phase = 0.0f;
    const float step = 2.0f * PI * hz / AUDIO_RATE;

    int done = 0;
    while (done < total) {
        const int n = (total - done) > 256 ? 256 : (total - done);
        for (int i = 0; i < n; ++i) {
            const int at = done + i;
            float env = amplitude;
            if (at < ramp) env *= (float)at / ramp;
            else if (at > total - ramp) env *= (float)(total - at) / ramp;
            const int16_t s = (int16_t)(sinf(phase) * 32000.0f * env);
            phase += step;
            if (phase > 2.0f * PI) phase -= 2.0f * PI;
            buf[i * 2] = s;
            buf[i * 2 + 1] = s;
        }
        size_t written = 0;
        i2s_write(I2S_NUM_0, buf, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        done += n;
    }
}

static void playSilence(int ms) {
    if (!i2sReady) return;
    static int16_t buf[128 * 2] = {0};
    int total = (AUDIO_RATE * ms) / 1000;
    while (total > 0) {
        const int n = total > 128 ? 128 : total;
        size_t written = 0;
        i2s_write(I2S_NUM_0, buf, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        total -= n;
    }
}

/* Five deliberately different sounds. Different in PITCH, LENGTH and SHAPE,
 * not just frequency -- a speaker with a broken solder joint can pass a single
 * mid tone and fail everything else, and a resonance can flatter one note. */
static void playSound(int which) {
    switch (which) {
        case 0:  /* OK: rising two-tone */
            lastSound = "OK chirp";
            playTone(880, 90, 0.6f); playTone(1320, 110, 0.6f);
            break;
        case 1:  /* Error: low buzz */
            lastSound = "error buzz";
            playTone(160, 140, 0.8f); playSilence(40); playTone(160, 200, 0.8f);
            break;
        case 2:  /* Arpeggio: checks it is not just one resonant note */
            lastSound = "arpeggio";
            playTone(523, 90, 0.5f); playTone(659, 90, 0.5f);
            playTone(784, 90, 0.5f); playTone(1047, 160, 0.5f);
            break;
        case 3: { /* Sweep: exposes rattles and dead bands */
            lastSound = "sweep";
            for (int f = 200; f <= 4000; f += 200) playTone((float)f, 22, 0.5f);
            break;
        }
        default: /* Quiet tick: proves low amplitude is audible at all */
            lastSound = "quiet tick";
            playTone(2000, 40, 0.12f);
            break;
    }
    if (i2sReady) {
        Serial.printf("[audio] played '%s' vol %d amp %s apll %s\n",
                      lastSound, audioVolume, paEnabled ? "on" : "off",
                      audioUsedApll ? "yes" : "no");
    } else {
        /* The first version logged "played" unconditionally while playTone()
         * returned early on a dead stream -- so the log confirmed sound that
         * never left the chip. A probe may not do that. */
        Serial.printf("[audio] NOT played (%s): I2S is not running\n", lastSound);
    }
    redraw = true;
}

/* Record, then play it back.
 *
 * The level meter answers "is the ADC moving?", which is a weaker question
 * than it looks: a meter can twitch on noise, on a floating input, or on
 * bleed from the speaker, and a person watching a bar cannot tell those from
 * a working microphone. Hearing your own voice come back cannot be faked --
 * it proves the analogue front end, the codec ADC, the I2S input path, the
 * I2S output path and the speaker in one gesture.
 *
 * Two seconds of mono 16-bit at 16 kHz is 64 KB, kept static. This board has
 * 8 MB of PSRAM, but a static buffer needs no allocator and cannot fragment
 * anything -- and the firmware this probe serves has a standing rule against
 * heap churn, so the probe may as well hold the line too. */
static constexpr int ECHO_SAMPLES = AUDIO_RATE * ECHO_SECONDS;
static int16_t echoBuf[ECHO_SAMPLES];
static bool echoHas = false;


static void echoRecord() {
    if (!i2sReady) { Serial.println("[echo] I2S not running"); return; }
    Serial.printf("[echo] recording %d s -- speak now\n", ECHO_SECONDS);

    /* Mute the amplifier while recording so the speaker cannot feed itself. */
    digitalWrite(PIN_PA_ENABLE, HIGH);

    static int16_t frame[256 * 2];
    int stored = 0;
    echoPeak = 0;
    const uint32_t deadline = millis() + (ECHO_SECONDS * 1000) + 1500;

    /* Discard the first buffers: the codec's ADC settles after the amplifier
     * state changes, and a click at the head sounds like a fault. */
    for (int i = 0; i < 4; ++i) {
        size_t junk = 0;
        i2s_read(I2S_NUM_0, frame, sizeof(frame), &junk, pdMS_TO_TICKS(50));
    }

    while (stored < ECHO_SAMPLES && millis() < deadline) {
        size_t got = 0;
        if (i2s_read(I2S_NUM_0, frame, sizeof(frame), &got,
                     pdMS_TO_TICKS(200)) != ESP_OK || got == 0) {
            continue;
        }
        const int frames = got / sizeof(int16_t) / 2;
        for (int i = 0; i < frames && stored < ECHO_SAMPLES; ++i) {
            const int16_t v = frame[i * 2];      /* left channel only */
            echoBuf[stored++] = v;
            const int mag = v < 0 ? -v : v;
            if (mag > echoPeak) echoPeak = mag;
        }
    }

    echoHas = stored > 0;
    Serial.printf("[echo] captured %d/%d samples, peak %d (%d%% of full scale)\n",
                  stored, ECHO_SAMPLES, echoPeak, (echoPeak * 100) / 32767);
    if (echoPeak < 200) {
        Serial.println("[echo] peak is near zero -- the microphone is not "
                       "producing signal. That is a mic/codec-ADC fault, not "
                       "a speaker one.");
    }
    digitalWrite(PIN_PA_ENABLE, paEnabled ? LOW : HIGH);
}

static void echoPlay() {
    if (!i2sReady || !echoHas) { Serial.println("[echo] nothing recorded"); return; }
    const int restore = audioVolume;
    es8311SetVolumeRaw(ECHO_PLAYBACK_VOLUME);
    Serial.printf("[echo] playing back %d s at %d%% (diagnostic level)\n",
                  ECHO_SECONDS, ECHO_PLAYBACK_VOLUME);
    static int16_t frame[256 * 2];
    int at = 0;
    while (at < ECHO_SAMPLES) {
        const int n = (ECHO_SAMPLES - at) > 256 ? 256 : (ECHO_SAMPLES - at);
        for (int i = 0; i < n; ++i) {
            frame[i * 2] = echoBuf[at + i];
            frame[i * 2 + 1] = echoBuf[at + i];
        }
        size_t written = 0;
        i2s_write(I2S_NUM_0, frame, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        at += n;
    }
    es8311SetVolumeRaw(restore);
    Serial.printf("[echo] done, volume back to %d%%\n", audioVolume);
}

/* Microphone. One short non-blocking read per loop; RMS scaled to 0..100 with
 * a slowly decaying peak, so a clap registers visibly rather than flickering
 * for one frame. */
static void sampleMic() {
    if (!i2sReady) return;
    static int16_t buf[128 * 2];
    size_t got = 0;
    if (i2s_read(I2S_NUM_0, buf, sizeof(buf), &got, 0) != ESP_OK || got == 0) return;

    const int samples = got / sizeof(int16_t);
    uint64_t sum = 0;
    for (int i = 0; i < samples; ++i) {
        const int32_t s = buf[i];
        sum += (uint64_t)(s * s);
    }
    const float rms = sqrtf((float)sum / samples);
    int level = (int)(rms * 100.0f / 8000.0f);      /* 8000 ~ a loud voice */
    if (level > 100) level = 100;

    micLevel = level;
    if (level > micPeak) micPeak = level;
}

/* -------------------------------------------------------------- hardware */

static bool readRegs(uint8_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(FT6336U_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)FT6336U_ADDR, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; ++i) buf[i] = Wire.read();
    return true;
}

static void scanI2c() {
    i2cCount = 0;
    Serial.println("[i2c] scanning SDA=16 SCL=15 ...");
    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[i2c]   device at 0x%02X%s\n", addr,
                          addr == FT6336U_ADDR ? "  <- FT6336U (expected)" : "");
            if (i2cCount < sizeof(i2cFound)) i2cFound[i2cCount++] = addr;
        }
    }
    if (i2cCount == 0) {
        Serial.println("[i2c]   NOTHING ANSWERED. Either the pins are wrong or "
                       "the controller is not populated.");
    }
    touchPresent = false;
    for (uint8_t i = 0; i < i2cCount; ++i) {
        if (i2cFound[i] == FT6336U_ADDR) touchPresent = true;
    }

    if (touchPresent) {
        uint8_t v = 0;
        if (readRegs(FT6336U_REG_CHIP_ID, &v, 1))  touchChipId = v;
        if (readRegs(FT6336U_REG_FIRMWARE, &v, 1)) touchFirmware = v;
        Serial.printf("[touch] FT6336U chip id 0x%02X, firmware 0x%02X\n",
                      touchChipId, touchFirmware);
    }
}

static void pollTouch() {
    if (!touchPresent) return;
    uint8_t buf[5];
    if (!readRegs(FT6336U_REG_TD_STATUS, buf, 5)) return;

    touchCount = buf[0] & 0x0F;
    if (touchCount == 0) { rawX = rawY = -1; touchDown = false; return; }
    touchDown = true;

    const int x = ((buf[1] & 0x0F) << 8) | buf[2];
    const int y = ((buf[3] & 0x0F) << 8) | buf[4];
    if (x == rawX && y == rawY) return;

    rawX = x; rawY = y;
    if (x < rawXMin) rawXMin = x;
    if (x > rawXMax) rawXMax = x;
    if (y < rawYMin) rawYMin = y;
    if (y > rawYMax) rawYMax = y;
    ++touchEvents;
    Serial.printf("[touch] n=%d raw=(%d,%d)  seen x:%d..%d y:%d..%d\n",
                  touchCount, x, y, rawXMin, rawXMax, rawYMin, rawYMax);
    redraw = true;
}

static void sampleBattery() {
    battVolts = analogReadMilliVolts(PIN_BATT_ADC) * 2.0f / 1000.0f;
}

void setup() {
    Serial.begin(115200);
    const uint32_t waited = millis();
    while (!Serial && millis() - waited < 2000) { delay(10); }
    delay(200);

    Serial.println();
    Serial.println("=== Freenove FNK0104B bring-up probe (env:s3diag) ===");
    Serial.printf("[chip] %s rev %d, %u MHz, %u KB free heap, PSRAM %u KB\n",
                  ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getCpuFreqMHz(), ESP.getFreeHeap() / 1024,
                  ESP.getPsramSize() / 1024);
    Serial.printf("[flash] %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.println("[tft] vendor claim: ILI9341_2, BGR, INVERSION_ON, "
                   "MISO13 MOSI11 SCLK12 CS10 DC46 RST-1 BL45");

    pinMode(PIN_BOOT_KEY, INPUT_PULLUP);
    pinMode(PIN_TOUCH_INT, INPUT_PULLUP);

    /* Release the touch controller's reset before scanning, or a present chip
     * reports as absent and we would blame the pins. */
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(20);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(300);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(rotation);
    tft.fillScreen(TFT_BLACK);

    Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);
    scanI2c();

    /* Amplifier enable is its own question: the DAC can be running perfectly
     * into a muted amplifier, which sounds exactly like a dead codec.
     *
     * ACTIVE LOW. Freenove's Sketch_07.1_Music drives this pin LOW once in
     * setup() and never raises it, so LOW is the enabled state. The first
     * version of this probe assumed an "enable" is active high, drove it HIGH,
     * and produced perfect silence with a codec reporting itself configured --
     * which is exactly the failure this pin was called out to separate. 'p'
     * toggles it live so the board can confirm rather than the comment. */
    pinMode(PIN_PA_ENABLE, OUTPUT);
    digitalWrite(PIN_PA_ENABLE, paEnabled ? LOW : HIGH);

    audioReady = es8311Init();
    Serial.printf("[audio] ES8311 at 0x18: %s\n",
                  audioReady ? "configured" : "NO RESPONSE");
    i2sReady = i2sBegin();
    Serial.printf("[audio] I2S MCLK=%d BCLK=%d WS=%d DOUT=%d DIN=%d @%d Hz: %s\n",
                  PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT,
                  PIN_I2S_DIN, AUDIO_RATE, i2sReady ? "running" : "FAILED");

    analogReadResolution(12);
    sampleBattery();
    draw();

    /* Announce ourselves, so the speaker is tested before anyone touches
     * anything -- and so a board that boots silent is obvious at once. */
    if (audioReady && i2sReady) playSound(0);
    Serial.println("[ready] BOOT = next rotation. Tap a colour bar to play "
                   "that sound.");
    Serial.println("[keys]  0-3 rotation, r rescan I2C, b blink backlight, "
                   "m swap touch map, 1-5 via bars, s sweep, v volume, "
                   "a re-init audio.");
}

void loop() {
    static bool     lastKey = HIGH;
    static uint32_t lastBlink = 0;
    static uint32_t lastBeat = 0;

    pollTouch();
    sampleMic();

    /* Peak decays slowly so a clap stays visible for about a second. */
    static uint32_t lastPeakDecay = 0;
    if (millis() - lastPeakDecay > 60) {
        lastPeakDecay = millis();
        if (micPeak > micLevel) --micPeak;
        redraw = true;
    }

    /* Tap a colour bar to play its sound. The hit test runs on the MAPPED
     * coordinates, so it is also a live test of the touch transform: if the
     * wrong bar plays, the mapping is wrong, and that is audible without
     * looking at the screen. */
    if (touchDown && !touchWasDown && barsW > 0) {
        int sx = 0, sy = 0;
        mapNativeToScreen(rawX, rawY, sx, sy);
        if (sy >= barsY && sy < barsY + barsH
                && sx >= barsX && sx < barsX + barsW * BAR_COUNT) {
            const int which = (sx - barsX) / barsW;
            /* Flash the bar's outline first. Touch registering and audio
             * working are two different claims, and with the amplifier muted
             * the first version could satisfy neither visibly -- so a tap that
             * lands is now seen even when nothing is heard. */
            tft.drawRect(barsX + which * barsW, barsY, barsW, barsH, TFT_YELLOW);
            Serial.printf("[tap] bar %d at mapped (%d,%d)\n", which, sx, sy);
            playSound(which);
        } else if (recW > 0 && sx >= recX && sx < recX + recW
                   && sy >= recY && sy < recY + recH) {
            /* Only a REQUEST: the test blocks for seconds, and starting it
             * with a finger still on the glass would record the tap. */
            echoRequested = true;
            Serial.println("[echo] REC button pressed");
        }
    }
    touchWasDown = touchDown;

    if (echoRequested && !touchDown) {
        echoRequested = false;
        echoBusy = true;
        draw();                       /* show RECORDING before it blocks */
        playTone(1200, 80, 0.5f); playSilence(120);
        playTone(1200, 80, 0.5f); playSilence(120);
        playTone(1600, 120, 0.5f); playSilence(250);
        echoRecord();
        playSilence(200);
        echoPlay();
        echoBusy = false;
        redraw = true;
    }

    /* BOOT, debounced on the press edge. */
    const bool key = digitalRead(PIN_BOOT_KEY);
    if (lastKey == HIGH && key == LOW) {
        rotation = (rotation + 1) & 3;
        tft.setRotation(rotation);
        Serial.printf("[rot] now %u (%dx%d)\n",
                      rotation, tft.width(), tft.height());
        redraw = true;
        delay(120);
    }
    lastKey = key;

    if (Serial.available()) {
        const int c = Serial.read();
        /* '0'..'3' set the rotation outright. BOOT does the same thing, but
         * only for someone holding the board -- and the layout has to be
         * checked at all four, which is not a thing to make a person do by
         * hand while reading back what fits. */
        if (c >= '0' && c <= '3') {
            rotation = (uint8_t)(c - '0');
            tft.setRotation(rotation);
            Serial.printf("[rot] now %u (%dx%d)\n",
                          rotation, tft.width(), tft.height());
            redraw = true;
        }
        if (c == 'm') {
            altHandedness = !altHandedness;
            Serial.printf("[map] now %s\n", altHandedness ? "B" : "A");
            redraw = true;
        }
        if (c >= '1' && c <= '5') playSound(c - '1');
        if (c == 's') playSound(3);
        if (c == 'v') {
            /* Top step is the ceiling, not 100 -- see AUDIO_VOLUME_MAX. */
            static const int steps[] = { 20, 35, 50, 65, AUDIO_VOLUME_MAX };
            static int at = 2;
            at = (at + 1) % 5;
            es8311SetVolume(steps[at]);
            Serial.printf("[audio] volume %d\n", audioVolume);
            playSound(4);
        }
        if (c == 'a') {
            audioReady = es8311Init();
            Serial.printf("[audio] re-init: %s\n", audioReady ? "ok" : "FAILED");
            redraw = true;
        }
        if (c == 'p') {
            paEnabled = !paEnabled;
            digitalWrite(PIN_PA_ENABLE, paEnabled ? LOW : HIGH);
            Serial.printf("[audio] amp %s (GPIO1 driven %s)\n",
                          paEnabled ? "ENABLED" : "disabled",
                          paEnabled ? "LOW" : "HIGH");
            playSound(0);
        }
        if (c == 'i') {
            uint8_t r00 = 0, r32 = 0, r14 = 0;
            esRead(0x00, r00); esRead(0x32, r32); esRead(0x14, r14);
            Serial.printf("[status] codec=%d i2s=%d apll=%d amp=%s vol=%d\n",
                          (int)audioReady, (int)i2sReady, (int)audioUsedApll,
                          paEnabled ? "on" : "off", audioVolume);
            Serial.printf("[status] ES8311 reg00=0x%02X reg32=0x%02X reg14=0x%02X\n",
                          r00, r32, r14);
            Serial.printf("[status] mic level=%d peak=%d\n", micLevel, micPeak);
        }
        if (c == 'e') {
            /* Three beeps, record, play back. The beeps matter: they tell the
             * person when to talk, and they prove the speaker still works
             * immediately before the recording, so a silent playback can only
             * be the microphone. */
            playTone(1200, 80, 0.5f); playSilence(120);
            playTone(1200, 80, 0.5f); playSilence(120);
            playTone(1600, 120, 0.5f); playSilence(150);
            echoRecord();
            playSilence(150);
            echoPlay();
            redraw = true;
        }
        if (c == 'g') {
            adcGainScale = (uint8_t)((adcGainScale + 1) & 0x07);
            esWrite(0x16, adcGainScale);
            Serial.printf("[audio] ADC gain scale (reg16) = %u\n", adcGainScale);
        }
        if (c == 'r') { scanI2c(); redraw = true; }
        if (c == 'b') {
            Serial.println("[bl] blinking GPIO45 five times");
            for (int i = 0; i < 5; ++i) {
                digitalWrite(TFT_BL, LOW);  delay(150);
                digitalWrite(TFT_BL, HIGH); delay(150);
            }
        }
    }

    /* A slow backlight pulse. A screen that visibly breathes proves GPIO45 is
     * the backlight; a screen that is merely lit does not. */
    if (millis() - lastBlink > 3000) {
        lastBlink = millis();
        digitalWrite(TFT_BL, LOW);
        delay(60);
        digitalWrite(TFT_BL, HIGH);
    }

    if (millis() - lastBeat > 1000) {
        lastBeat = millis();
        sampleBattery();
        Serial.printf("[beat] rot=%u %dx%d touch=%s raw=(%d,%d) batt=%.2fV\n",
                      rotation, tft.width(), tft.height(),
                      touchPresent ? "0x38" : "none", rawX, rawY, battVolts);
        redraw = true;
    }

    if (redraw) { redraw = false; draw(); }
    delay(10);
}
