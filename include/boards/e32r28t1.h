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
/* DAC AUDIO, ON A BOARD WHOSE TOUCH CLOCK IS THE OTHER DAC PAD.
 *
 * The XPT2046's bit-banged SPI clock is GPIO25, and GPIO25 is ESP32 DAC
 * channel 1; the speaker is on GPIO26, channel 2. Installing I2S in
 * I2S_MODE_DAC_BUILT_IN claims channel 1's pad as well, and narrowing to
 * channel 2 afterwards does not give it back -- so 5.5.0 shipped a console
 * that drew perfectly and could not be touched, and 5.5.1 switched audio off.
 *
 * beginAudio() now hands the unused pad back explicitly once the driver is up,
 * and Board::begin() re-applies the touch pin setup after it; the boot log
 * prints the pad's state before and after so the release is visible. See
 * BoardConfig.h, which allows a touch pin on the non-speaker pad for exactly
 * this reason and still refuses everything else there.
 *
 * The speaker was silent for a second reason too: IO4 was declared as the RGB
 * green channel, but it is the FM8002E amplifier's enable, active low, so
 * turning the "LED" off every boot held the amplifier in shutdown. */
#define GUME_HAS_AUDIO_DAC   1

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
    "R28T",

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
        /* invertColours        */ false,
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

    /* RGB LED, common anode (drive LOW to light a channel). From the vendor's
     * table: red IO22, green IO16, blue IO17.
     *
     * This used to read r=16, g=4, on the strength of "an orange mix came out
     * green and purple came out cyan". Those two observations are exactly what
     * IO16 being GREEN produces -- they were evidence for the vendor's table,
     * read as evidence of crossed wires. And IO4 is not an LED at all: it is
     * the amplifier enable, below. Red on IO22 was never driven before. */
    RgbLedProfile{
        /* r           */ 22,
        /* g           */ 16,
        /* b           */ 17,
        /* commonAnode */ true,
    },

    /* GPIO26 is ESP32 DAC channel 2, driven through I2S_DAC_BUILT_IN into an
     * onboard FM8002E amplifier whose enable is IO4, active low -- confirmed by
     * the owner and the vendor's pin table. Volume is pure software gain.
     *
     * maxVolume 75 is the ceiling the 2.8-inch CYD boards were given by ear;
     * re-check it on this board now that the amplifier is actually on. */
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
