#pragma once

#include "BoardProfile.h"

/* Resistive XPT2046 on its own bit-banged bus. See e32r28t1.h for why this is
 * a macro and not only a profile field. */
#define GUME_TOUCH_CAPACITIVE 0

#define GUME_HAS_AUDIO_CODEC 0
/* DAC audio on GPIO26, with the touch clock on the other DAC pad, GPIO25.
 * That is the combination 5.5.0 got wrong; beginAudio() now hands GPIO25 back
 * once the driver is up. Same arrangement as the E32R28T-1 -- see e32r28t1.h
 * for the full account. */
#define GUME_HAS_AUDIO_DAC   1

/* ESP32-2432S028, dual-USB, INVERTING PANEL. 2.8-inch, 240x320.
 *
 * A THIRD CYD VARIANT, and the reason this file exists rather than being folded
 * into one of the other two. The 2.8-inch "cheap yellow display" ships with at
 * least three different combinations of panel and backlight behind the same
 * silkscreen, and they are not distinguishable by looking at the board:
 *
 *   esp32-2432s028r.h        micro-USB, ILI9341, backlight IO21, no inversion
 *   esp32-2432s028.h         dual-USB, ST7789, backlight IO27  (UNVERIFIED)
 *   this file                dual-USB, ILI9341 sequence + runtime inversion,
 *                            backlight IO21                    (MEASURED)
 *
 * HOW THIS ONE WAS ESTABLISHED. Every combination was flashed onto one board
 * and the screen looked at:
 *
 *   ILI9341, no inversion          draws perfectly, every colour flipped --
 *                                  dark theme white, green purple, blue salmon
 *   ILI9341 + TFT_INVERT_COLORS    identical; the macro does nothing here
 *   ILI9341 + invertColours        CORRECT
 *   ST7789 + TFT_INVERT_COLORS     wrong colours
 *   ST7789 (its own profile, IO27) completely dark screen
 *
 * TWO TRAPS WORTH CARRYING AWAY. TFT_eSPI's `TFT_INVERT_COLORS` macro is read
 * ONLY by its ST7789 and ST7735 init sequences, so on an ILI9341 build it is
 * silently inert -- it was tried twice here and read as the flag being
 * ignored. `invertColours` below is a runtime invertDisplay() in
 * Board::begin() and works under any driver. And a wrong backlight pin gives a
 * dark panel while Wi-Fi, BLE, NVS, touch and the screen saver all run
 * perfectly, so the serial log looks healthy and it reads as a dead board.
 *
 * WHAT IS MEASURED AND WHAT IS NOT:
 *
 *   measured   panel, colour inversion, backlight IO21, touch, rotation,
 *              4MB flash and no PSRAM (from the chip, see the [boot] lines)
 *   inherited  SD, RGB LED and battery pins, taken from the E32R28T-1 which
 *              this board matches pin-for-pin everywhere that WAS checked
 *
 * The RGB order is the one to distrust: the E32R28T-1's own vendor table has
 * red and green crossed, and that was only found by driving each channel and
 * looking. A wrong-coloured status LED here is expected, not surprising.
 *
 * The display's own SPI pins are not here -- TFT_eSPI reads them from the `-D`
 * flags in platformio.ini, and BoardConfig.h cross-checks the two.
 */
inline constexpr BoardProfile BOARD = {
    "ESP32-2432S028-inv",
    "CYDINV",

    /* 3 is landscape with the USB edge at the bottom, as on the E32R28T-1.
     * Board::pollTouch() compensates for every rotation, so don't hand-correct
     * in game code. */
    PanelProfile{
        /* nativeWidth          */ 240,
        /* nativeHeight         */ 320,
        /* landscapeRotation    */ 3,
        /* portraitRotation     */ 0,
        /* backlightPin         */ 21,
        /* backlightActiveHigh  */ true,
        /* invertColours        */ true,
    },

    /* Measured working on this board. */
    TouchProfile{
        /* kind               */ (GUME_TOUCH_CAPACITIVE
                                     ? TouchKind::CapacitiveFt6336u
                                     : TouchKind::ResistiveXpt2046),
        /* mosi               */ 32,
        /* miso               */ 39,
        /* sclk               */ 25,
        /* cs                 */ 33,
        /* irq                */ 36,
        /* irqUsable          */ true,
        /* sda                */ PIN_NONE,
        /* scl                */ PIN_NONE,
        /* reset              */ PIN_NONE,
        /* i2cAddress         */ 0,
        /* i2cHz              */ 0,
        /* pressureThreshold  */ 350,
        /* hitSlop            */ 8,
    },

    ButtonProfile{
        /* bootPin   */ 0,
        /* activeLow */ true,
    },

    /* Inherited from the E32R28T-1; not exercised on this board. */
    SdProfile{
        /* cs    */ 5,
        /* mosi  */ 23,
        /* miso  */ 19,
        /* sclk  */ 18,
        /* spiHz */ 16000000,
    },

    /* Inherited, and see the note above about distrusting the order. */
    RgbLedProfile{
        /* r           */ 16,
        /* g           */ 4,
        /* b           */ 17,
        /* commonAnode */ true,
    },

    /* GPIO26 (DAC channel 2) feeds an onboard SC8002B amplifier and the JST
     * 1.25 speaker connector. The amplifier has no enable line to drive --
     * confirmed by the owner. maxVolume 75 is the CYD ceiling set by ear. */
    AudioProfile{
        /* speakerPin          */ 26,
        /* codecI2cAddress     */ 0,
        /* i2sMclk             */ PIN_NONE,
        /* i2sBclk             */ PIN_NONE,
        /* i2sWordSelect       */ PIN_NONE,
        /* i2sDataOut          */ PIN_NONE,
        /* i2sDataIn           */ PIN_NONE,
        /* ampEnablePin        */ PIN_NONE,
        /* ampEnableActiveLow  */ false,
        /* maxVolume           */ 75,
    },

    /* Inherited: IO34 through a 2:1 divider. sensorMaxVolts is an ADC fault
     * ceiling and NOT a pack-present test -- hal/BoardPower.cpp records why no
     * threshold can tell a missing pack from a present one on this family. */
    BatteryProfile{
        /* adcPin        */ 34,
        /* dividerRatio  */ 2.0f,
        /* sensorMaxVolts*/ 4.50f,
    },

    /* 4 MB part, huge_app.csv: 3 MB for the app. Read back from the chip at
     * boot -- see the [boot] mac=... flash= line. */
    MemoryProfile{
        /* flashBytes */ 4u * 1024u * 1024u,
    },
};
