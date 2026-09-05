#pragma once

#include "BoardProfile.h"

/* Which touch controller this board wires, as a macro so the preprocessor can
 * act on it -- see e32r28t1.h for why this cannot just be the profile field. */
#define GUME_TOUCH_CAPACITIVE 0

/* No codec; DAC output on GPIO26 (ESP32 DAC2), same as the 2.8-inch board. */
#define GUME_HAS_AUDIO_CODEC 0
#define GUME_HAS_AUDIO_DAC   1

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
 * INHERITED from the E32R28T-1 rather than measured here: the RGB LED, the
 * speaker and battery sense. These began as PIN_NONE, which was right while
 * nothing about the board was known -- but the display bus then turned out to
 * be identical to the 2.8-inch board's, pin for pin, which makes "same
 * reference PCB with a bigger panel" evidence rather than hope. Each is
 * flagged below with what would prove it wrong, because they fail differently:
 * a wrong LED or speaker pin drives a line something else may own, while a
 * wrong battery pin reports a percentage that is pure fiction and looks
 * entirely plausible.
 *
 * Still PIN_NONE: the SD slot, which nothing has needed yet and which shares a
 * bus with the peripheral header.
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

    /* BOOT (IO0), the key the ROM uses to enter serial download mode; read at
     * runtime as an ordinary input with an internal pull-up. This is the one
     * fact here that is not a guess about the board's layout: IO0 is the ESP32
     * strapping pin, so the ROM's own boot behaviour already proves the button
     * is on it and pulls the line low. */
    ButtonProfile{
        /* bootPin   */ 0,
        /* activeLow */ true,
    },

    /* Not characterised on this board. See the header comment. */
    SdProfile{
        /* cs    */ PIN_NONE,
        /* mosi  */ PIN_NONE,
        /* miso  */ PIN_NONE,
        /* sclk  */ PIN_NONE,
        /* spiHz */ 0,
    },

    /* Red and blue are inherited from the E32R28T-1 and are still unverified
     * on this board. Green is NOT: this board's green channel is PIN_NONE
     * because IO4 is not an LED here at all.
     *
     * The inherited block claimed IO4 as green, and the previous version of
     * this comment said "if this board disagrees with both, measure before
     * believing either". It does disagree, and the LCDWIKI schematic and pin
     * table for the E32R40T/E32N40T settle it: IO4 is `AUDIO_EN`, the
     * active-low shutdown input of the onboard 8002-series power amplifier.
     *
     * That inheritance was not a cosmetic error. This LED is common anode, so
     * "off" drives the pin HIGH -- and HIGH on an active-low shutdown pin
     * holds the amplifier in shutdown. Board::begin() calls
     * setRgb(false,false,false) on every boot, so the firmware was switching
     * the speaker off as a side effect of turning off an LED that is not
     * there. The DAC was driving GPIO26 correctly the whole time and nothing
     * came out.
     *
     * Red and blue keep their inherited values; a wrong colour is harmless
     * and self-evident, which is what the original comment was right about. */
    RgbLedProfile{
        /* r           */ 16,
        /* g           */ PIN_NONE,
        /* b           */ 17,
        /* commonAnode */ true,
    },

    /* Vendor-documented, not inferred. The LCDWIKI schematic for this board
     * runs GPIO26 (`AUDIO_IN`) through an RC filter into U5, an 8002-series
     * mono BTL power amplifier -- SC8002B on the schematic, FM8002E in the
     * user manual, revision-dependent and identical in topology -- whose
     * output is SP+/SP- on JP1. GPIO26 is ESP32 DAC channel 2, so
     * I2S_DAC_BUILT_IN drives the amplifier's analog input directly. This is
     * an analog power amp on the board, NOT an external I2S DAC: there is no
     * BCLK/LRCLK/DOUT path and adding one would be describing hardware that
     * is not here.
     *
     * `AUDIO_EN` on IO4 is the amplifier's shutdown input and is ACTIVE LOW
     * (low = enabled). See the RGB block above for why that pin is not the
     * green LED, and what it cost to believe that it was.
     *
     * maxVolume 75 is still inherited from the 2.8-inch board and is NOT yet
     * set by listening on this one -- the vendor rates the amp at 1.5W into
     * 8 ohms from 5V, so the ceiling here is about what the speaker and the
     * case can take, and it should be confirmed by ear. */
    AudioProfile{
        /* speakerPin          */ 26,
        /* codecI2cAddress     */ 0,
        /* i2sMclk             */ PIN_NONE,
        /* i2sBclk             */ PIN_NONE,
        /* i2sWordSelect       */ PIN_NONE,
        /* i2sDataOut          */ PIN_NONE,
        /* i2sDataIn           */ PIN_NONE,
        /* ampEnablePin        */ 4,
        /* ampEnableActiveLow  */ true,
        /* maxVolume           */ 75,
    },

    /* Inherited from the E32R28T-1: IO34 (ADC1_CH6, input-only) behind the 2:1
     * divider the vendor manual states plainly -- "the obtained voltage
     * multiplied by 2 is the actual battery voltage". sensorMaxVolts is a
     * fault ceiling on the ADC, NOT a pack-present test; like the 2.8-inch
     * board this one cannot tell a missing pack from a present one.
     *
     * This is the one to distrust. A wrong adcPin does not fail loudly -- it
     * reports a plausible-looking percentage that is fiction. Check it against
     * a meter before believing the badge, and note GPIO34 is the light sensor
     * rather than a battery sense on the ESP32-2432S028R, so this family
     * resemblance does not extend to every CYD. */
    BatteryProfile{
        /* adcPin        */ 34,
        /* dividerRatio  */ 2.0f,
        /* sensorMaxVolts*/ 4.50f,
    },

    /* 4 MB part, partitioned huge_app.csv: 3 MB for the app. */
    MemoryProfile{
        /* flashBytes */ 4u * 1024u * 1024u,
    },
};
