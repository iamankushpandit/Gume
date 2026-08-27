#pragma once

#include "BoardProfile.h"

/* Makerfabs / Sunton ESP32-2432S028R -- the original/classic 2.4-inch CYD.
 * Micro-USB connector, ILI9341 240x320 TFT, XPT2046 resistive touch, RGB LED, LDR.
 *
 * This is the board that started the "cheap yellow display" wave. It is the most
 * widely documented and supported variant. If you don't know which CYD you have,
 * this is the most likely match.
 *
 * IMPORTANT: This firmware is ONLY for ILI9341-based boards. Newer ST7789 or
 * ILI9342 variants will not work correctly with this build. Verify your display
 * controller before flashing.
 *
 * Pins are from the Sunton community board definitions and vendor documentation.
 * GPIO34 is the light sensor (LDR), not battery sense (unlike E32R28T-1).
 *
 * The display's own SPI pins are NOT here: TFT_eSPI is configured entirely
 * through `-D` flags in platformio.ini and reads them from there. BoardConfig.h
 * cross-checks the two descriptions against each other so they cannot drift.
 */
inline constexpr BoardProfile BOARD = {
    "ESP32-2432S028R (ILI9341)",

    /* panel: 1 is landscape with the USB edge at the bottom.
     * 0 is the quarter-turn from it (portrait). Board::pollTouch()
     * compensates for every rotation, so don't hand-correct in game code. */
    PanelProfile{
        /* nativeWidth          */ 240,
        /* nativeHeight         */ 320,
        /* landscapeRotation    */ 1,
        /* portraitRotation     */ 0,
        /* backlightPin         */ 21,
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

    /* RGB LED, common anode (drive LOW to light). */
    RgbLedProfile{
        /* r           */ 4,
        /* g           */ 16,
        /* b           */ 17,
        /* commonAnode */ true,
    },

    /* Audio amplifier input on GPIO26. */
    AudioProfile{
        /* speakerPin */ 26,
    },

    /* Battery sense on IO35 (ADC1_CH7, input-only). Divider ratio is 2:1. */
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
