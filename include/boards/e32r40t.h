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

    /* Not characterised on this board. See the header comment. */
    SdProfile{
        /* cs    */ PIN_NONE,
        /* mosi  */ PIN_NONE,
        /* miso  */ PIN_NONE,
        /* sclk  */ PIN_NONE,
        /* spiHz */ 0,
    },

    /* Inherited from the E32R28T-1, including its crossed red/green lines,
     * which were verified on that unit rather than assumed. Wrong here shows
     * up immediately and harmlessly: a beep lights the wrong colour, or an LED
     * that never goes out (common anode -- a channel lights when driven LOW).
     * The vendor manual for the 2.8-inch board names IO16/IO17/IO22 instead,
     * which the E32R28T-1 profile already contradicts on measured grounds; if
     * this board disagrees with both, measure before believing either. */
    RgbLedProfile{
        /* r           */ 16,
        /* g           */ 4,
        /* b           */ 17,
        /* commonAnode */ true,
    },

    /* Inherited from the E32R28T-1. Audio is stubbed there too -- beepOk()
     * and beepError() pulse the LED -- so this pin being wrong is quiet in
     * both senses. If a speaker on GPIO26 stays silent, that is the thing to
     * measure, not the codec fields below, which this board does not have. */
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
