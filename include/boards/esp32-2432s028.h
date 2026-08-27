#pragma once

#include "BoardProfile.h"

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
        /* mosi               */ 32,
        /* miso               */ 39,
        /* sclk               */ 25,
        /* cs                 */ 33,
        /* irq                */ 36,
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
        /* speakerPin */ PIN_NONE,
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
