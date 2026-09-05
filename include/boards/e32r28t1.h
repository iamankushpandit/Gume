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

/* No codec; DAC output on GPIO26 (ESP32 DAC2). The I2S peripheral drives it
 * directly (I2S_DAC_BUILT_IN), which is why `driver/i2s.h` is still needed --
 * same reasoning as the codec macro above: the DAC path would link the driver
 * into every board if it were not guarded. GUME_HAS_AUDIO_CODEC stays 0. */
#define GUME_HAS_AUDIO_CODEC 0
/* NO DAC AUDIO ON THIS BOARD: ITS TOUCH CLOCK IS A DAC PAD.
 *
 * The XPT2046's bit-banged SPI clock is GPIO25, and GPIO25 is ESP32 DAC
 * channel 1. Bringing up I2S in I2S_MODE_DAC_BUILT_IN takes the DAC pads over
 * from the GPIO matrix, so Board::begin() -- which configures touch first and
 * calls beginAudio() forty lines later -- ended up handing the touch clock to
 * the audio peripheral. The panel drew perfectly and nothing responded to a
 * finger.
 *
 * That shipped in 5.5.0 and is why 5.5.1 exists. Do not re-enable this without
 * moving the touch clock, which is a hardware fact and cannot be moved.
 *
 * Nothing is lost by it: the speaker on this board never worked anyway, because
 * IO4 is declared here as the RGB green channel while the vendor pin table
 * calls it the amplifier enable -- so the amp sat in shutdown. Sound on this
 * board needs that resolved AND a touch clock that is not a DAC pad.
 *
 * BoardConfig.h now refuses this combination at compile time rather than
 * leaving it to whoever flashes it. */
#define GUME_HAS_AUDIO_DAC   0

/* HOSYOND / LCDWIKI E32R28T-1 (ESP32-32E) -- the 2.8-inch board Braino! ships
 * on. ILI9341 320x240 TFT, XPT2046 resistive touch on its own bit-banged bus,
 * micro-SD on VSPI, RGB status LED, and a TP4054 single-cell charger.
 *
 * Pins are from the vendor's own table, not a generic ESP32 pinout:
 * https://www.lcdwiki.com/res/E32R28T-1/2.8inch_E32R28T-1_E32N28T-1_Arduino_Demo_Instructions.pdf
 *
 * The display's own SPI pins are NOT here: TFT_eSPI is configured entirely
 * through `-D` flags in platformio.ini and reads them from there. BoardConfig.h
 * cross-checks the two descriptions against each other so they cannot drift.
 */
inline constexpr BoardProfile BOARD = {
    "E32R28T-1",

    /* panel: 3 is landscape with the USB edge at the bottom (1 is the same
     * view rotated 180). 0 is the quarter-turn from it. Board::pollTouch()
     * compensates for every rotation, so don't hand-correct in game code. */
    PanelProfile{
        /* nativeWidth          */ 240,
        /* nativeHeight         */ 320,
        /* landscapeRotation    */ 3,
        /* portraitRotation     */ 0,
        /* backlightPin         */ 21,
        /* backlightActiveHigh  */ true,
    },

    /* touch: a bus of its own, deliberately -- the TFT owns HSPI. See
     * src/hal/CLAUDE.md for why this is bit-banged rather than a second
     * hardware peripheral. */
    TouchProfile{
        /* kind               */ (GUME_TOUCH_CAPACITIVE
                                     ? TouchKind::CapacitiveFt6336u
                                     : TouchKind::ResistiveXpt2046),
        /* mosi               */ 32,
        /* miso               */ 39,
        /* sclk               */ 25,
        /* cs                 */ 33,
        /* irq                */ 36,
/* irqUsable         */ true,
        /* sda                */ PIN_NONE,
        /* scl                */ PIN_NONE,
        /* reset              */ PIN_NONE,
        /* i2cAddress         */ 0,
        /* i2cHz              */ 0,
        /* pressureThreshold  */ 350,
        /* hitSlop            */ 8,
    },

    /* BOOT (IO0), the key the ROM uses to enter serial download mode. Read
     * only at runtime, where it is an ordinary input with an internal
     * pull-up. */
    ButtonProfile{
        /* bootPin   */ 0,
        /* activeLow */ true,
    },

    SdProfile{
        /* cs    */ 5,
        /* mosi  */ 23,
        /* miso  */ 19,
        /* sclk  */ 18,
        /* spiHz */ 16000000,
    },

    /* RGB LED, common anode (drive LOW to light a channel).
     *
     * The red and green lines are crossed relative to the usual standard pinout on this
     * unit (E32R28T-1): driving GPIO4 lit GREEN, not red. Verified on hardware -- an orange
     * (R255 G110) mix came out green, and purple (R200 B255) came out cyan/blue,
     * which is exactly what swapping R and G produces. */
    RgbLedProfile{
        /* r           */ 16,
        /* g           */ 4,
        /* b           */ 17,
        /* commonAnode */ true,
    },

    /* GPIO26 is ESP32 DAC channel 2. The I2S peripheral drives it directly
     * (I2S_DAC_BUILT_IN) at 16 kHz; no codec or amplifier enable is wired.
     * Volume is pure software gain -- there is no register.
     *
     * maxVolume is 75 rather than 100. The DAC feeds a 1-inch driver with no
     * amplifier behind it, and above roughly 75% the output distorts audibly
     * without getting meaningfully louder -- the headroom buys nothing. This
     * was established by ear on a 2.8-inch CYD board; if a louder amplifier
     * is ever wired behind GPIO26 on a future variant, raise the ceiling in
     * that board's profile. */
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

    /* Battery sense on IO34 (ADC1_CH6, input-only). The vendor manual states
     * the divider plainly: "the obtained voltage multiplied by 2 is the actual
     * battery voltage". sensorMaxVolts is a fault ceiling on the ADC, NOT a
     * pack-present test -- this board cannot tell a missing pack from a
     * present one, and hal/BoardPower.cpp records the measurements proving it.
     *
     * GPIO34 on the classic CYD (ESP32-2432S028R) is the LDR, not a battery
     * sense. Do not copy pin maps between CYD variants. */
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
