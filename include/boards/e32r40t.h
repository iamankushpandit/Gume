#pragma once

#include "BoardProfile.h"

/* Which touch controller this board wires, as a macro so the preprocessor can
 * act on it -- see e32r28t1.h for why this cannot just be the profile field. */
#define GUME_TOUCH_CAPACITIVE 0

/* No audio codec. */
#define GUME_HAS_AUDIO_CODEC 0

/* LCDWIKI E32R40T (ESP32-32E) -- the 4-inch board. ST7796 320x480 panel,
 * XPT2046 resistive touch sharing the display's SPI bus.
 *
 * This board exists in Braino for a specific audience: a 4-inch screen serves
 * players who need a physically bigger, plainer display than the 2.8-inch
 * board offers. That is also why the runtime draws playable games through
 * Ui::ScaledRenderer at textScale 2 rather than merely stretching the layout
 * -- see AppRuntime.h.
 *
 * WHAT IS MEASURED AND WHAT IS NOT
 *
 * Measured on hardware with env:diag4, 2026-09-03:
 *   - ST7796 at 320x480 native, rendering correctly at rotation 3 (480x320
 *     landscape) with correct colour order: all four corner labels land in
 *     their corners and the border reaches all four physical edges.
 *   - The display SPI bus is IDENTICAL to the 2.8-inch board's: MISO 12,
 *     MOSI 13, SCLK 14, CS 15, DC 2, no reset line, HSPI, 40MHz.
 *   - The backlight is GPIO27, active high. GPIO21 -- the 2.8-inch board's
 *     backlight, and the obvious assumption -- was measured DARK twice,
 *     including from a clean reset with nothing else driven.
 *
 * Derived from the earlier feat/st7796-4inch-board port, which ran this panel
 * and its touch working:
 *   - Touch is an XPT2046 sharing the display's bus with its own CS on 33.
 *   - Its IRQ on 36 cannot be used; see irqUsable below.
 *
 * NOT characterised, and therefore declared absent rather than guessed:
 * the SD slot, the RGB LED, the speaker and battery sense. The pins are
 * plausibly the same as the 2.8-inch board's, and "plausibly" is exactly the
 * reasoning that produced a backlight pin nobody had checked. PIN_NONE costs
 * a feature the firmware already knows how to do without; a wrong pin costs a
 * fictional battery percentage or a fight over a line something else owns.
 * Fill these in when someone measures them.
 *
 * The display's own SPI pins are NOT here: TFT_eSPI is configured through -D
 * flags in platformio.ini, and BoardConfig.h cross-checks the two descriptions
 * so they cannot drift.
 */
inline constexpr BoardProfile BOARD = {
    "E32R40T",

    /* panel: 3 is landscape, confirmed by drawing into it. */
    PanelProfile{
        /* nativeWidth          */ 320,
        /* nativeHeight         */ 480,
        /* landscapeRotation    */ 3,
        /* portraitRotation     */ 0,
        /* backlightPin         */ 27,
        /* backlightActiveHigh  */ true,
    },

    /* touch: the XPT2046 rides the display's own hardware SPI bus, arbitrated
     * by its own CS, rather than the separate bit-banged bus the 2.8-inch
     * board uses. The mosi/miso/sclk below therefore repeat the display's
     * pins, which TouchProfile explicitly allows.
     *
     * irqUsable is false, and it is the field that matters here. GPIO36 is
     * input-only, has no internal pull, and this board fits no external
     * pull-up -- so the line floats LOW and reports a permanent false
     * pen-down. Gating on pressure alone is clean on this panel: idle noise
     * reads 10-20 and a real press 2000+, so the 350 threshold below sits an
     * order of magnitude clear of the noise on both sides. */
    TouchProfile{
        /* kind               */ (GUME_TOUCH_CAPACITIVE
                                     ? TouchKind::CapacitiveFt6336u
                                     : TouchKind::ResistiveXpt2046),
        /* mosi               */ 13,
        /* miso               */ 12,
        /* sclk               */ 14,
        /* cs                 */ 33,
        /* irq                */ 36,
        /* irqUsable          */ false,
        /* sda                */ PIN_NONE,
        /* scl                */ PIN_NONE,
        /* reset              */ PIN_NONE,
        /* i2cAddress         */ 0,
        /* i2cHz              */ 0,
        /* pressureThreshold  */ 350,
        /* hitSlop            */ 8,
    },

    /* Not characterised on this board. See the header comment. */
    SdProfile{
        /* cs    */ PIN_NONE,
        /* mosi  */ PIN_NONE,
        /* miso  */ PIN_NONE,
        /* sclk  */ PIN_NONE,
        /* spiHz */ 0,
    },

    /* Not characterised on this board. beepOk()/beepError() fall back to
     * doing nothing visible rather than driving lines we have not confirmed. */
    RgbLedProfile{
        /* r           */ PIN_NONE,
        /* g           */ PIN_NONE,
        /* b           */ PIN_NONE,
        /* commonAnode */ true,
    },

    /* Not characterised on this board. */
    AudioProfile{
        /* speakerPin          */ PIN_NONE,
        /* codecI2cAddress     */ 0,
        /* i2sMclk             */ PIN_NONE,
        /* i2sBclk             */ PIN_NONE,
        /* i2sWordSelect       */ PIN_NONE,
        /* i2sDataOut          */ PIN_NONE,
        /* i2sDataIn           */ PIN_NONE,
        /* ampEnablePin        */ PIN_NONE,
        /* ampEnableActiveLow  */ false,
    },

    /* Not characterised on this board. A wrong adcPin here would not fail
     * loudly -- it would report a battery percentage that is pure fiction,
     * which is worse than showing no badge at all. */
    BatteryProfile{
        /* adcPin        */ PIN_NONE,
        /* dividerRatio  */ 0.0f,
        /* sensorMaxVolts*/ 0.0f,
    },

    /* 4 MB part, partitioned huge_app.csv: 3 MB for the app. */
    MemoryProfile{
        /* flashBytes */ 4u * 1024u * 1024u,
    },
};
