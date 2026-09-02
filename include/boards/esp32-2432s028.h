#pragma once

#include "BoardProfile.h"

/* Which touch controller this board wires, as a macro so the preprocessor can
 * act on it. It has to be a macro and not just the profile field: `if
 * constexpr` does not discard in a non-template function, so a capacitive
 * branch mentioning Wire links the whole I2C library into a resistive board --
 * measured at +4,476 bytes of flash for code that can never run, on a budget
 * already at 75%. The TouchKind below is derived from this macro rather than
 * stated alongside it, so there is still exactly one statement of the fact. */
#define GUME_TOUCH_CAPACITIVE 0

/* No audio codec. Same reasoning as GUME_TOUCH_CAPACITIVE above: the
 * codec path pulls in Wire and driver/i2s.h, and `if constexpr` would
 * link both into a board that has neither. */
#define GUME_HAS_AUDIO_CODEC 0

/* Makerfabs / Sunton ESP32-2432S028Rv3 -- a 2.4-inch CYD variant (Rv3/dual-USB).
 * ST7789 240x320 TFT, XPT2046 resistive touch, battery sense divider, RGB LED, LDR.
 *
 * IMPORTANT: This is the ST7789 variant (Rv3). The original classic CYD uses ILI9341.
 * Do NOT use this firmware on ILI9341-based boards; colors will be inverted and touch
 * calibration will not work. The marketplace sells multiple variants under the same
 * "ESP32-2432S028" name. Verify your display controller before flashing.
 *
 * Pins are from the vendor's schematics and Sunton community board definitions.
 * GPIO34 is the light sensor (LDR), not battery sense (unlike E32R28T-1).
 *
 * The display's own SPI pins are NOT here: TFT_eSPI is configured entirely
 * through `-D` flags in platformio.ini and reads them from there. BoardConfig.h
 * cross-checks the two descriptions against each other so they cannot drift.
 */
inline constexpr BoardProfile BOARD = {
    "ESP32-2432S028Rv3 (ST7789)",

    /* panel: 1 is landscape with the USB edge at the bottom (rotation 1).
     * 2 is the quarter-turn from it (portrait). Board::pollTouch()
     * compensates for every rotation, so don't hand-correct in game code. */
    PanelProfile{
        /* nativeWidth          */ 240,
        /* nativeHeight         */ 320,
        /* landscapeRotation    */ 1,
        /* portraitRotation     */ 2,
        /* backlightPin         */ 27,
        /* backlightActiveHigh  */ true,
    },

    /* touch: a bus of its own, bit-banged rather than using a second
     * hardware peripheral since the TFT owns HSPI. */
    TouchProfile{
        /* kind               */ (GUME_TOUCH_CAPACITIVE
                                     ? TouchKind::CapacitiveFt6336u
                                     : TouchKind::ResistiveXpt2046),
        /* mosi               */ 32,
        /* miso               */ 39,
        /* sclk               */ 25,
        /* cs                 */ 33,
        /* irq                */ 36,
        /* sda                */ PIN_NONE,
        /* scl                */ PIN_NONE,
        /* reset              */ PIN_NONE,
        /* i2cAddress         */ 0,
        /* i2cHz              */ 0,
        /* pressureThreshold  */ 350,
        /* hitSlop            */ 8,
    },

    SdProfile{
        /* cs    */ 5,
        /* mosi  */ 23,
        /* miso  */ 19,
        /* sclk  */ 18,
        /* spiHz */ 16000000,
    },

    /* No RGB LED on this board variant. */
    RgbLedProfile{
        /* r           */ PIN_NONE,
        /* g           */ PIN_NONE,
        /* b           */ PIN_NONE,
        /* commonAnode */ false,
    },

    /* Audio is stubbed. beepOk()/beepError() are no-ops on this variant. */
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

    /* Battery sense on IO35 (ADC1_CH7, input-only). The divider ratio is
     * typically 2:1 (voltage at pin is half the cell voltage). */
    BatteryProfile{
        /* adcPin        */ 35,
        /* dividerRatio  */ 2.0f,
        /* sensorMaxVolts*/ 4.50f,
    },

    /* 4 MB part, partitioned huge_app.csv: 3 MB for the app. */
    MemoryProfile{
        /* flashBytes */ 4u * 1024u * 1024u,
    },
};
