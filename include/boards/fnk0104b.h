#pragma once

#include "BoardProfile.h"

/* Which touch controller this board wires, as a macro so the preprocessor can
 * act on it. It has to be a macro and not just the profile field: `if
 * constexpr` does not discard in a non-template function, so a capacitive
 * branch mentioning Wire links the whole I2C library into a resistive board.
 * The TouchKind below is derived from this macro rather than stated alongside
 * it, so there is still exactly one statement of the fact. */
#define GUME_TOUCH_CAPACITIVE 1

/* Freenove FNK0104B -- 2.8-inch ESP32-S3 display board. ILI9341 240x320 IPS
 * panel, FT6336U CAPACITIVE touch over I2C, ES8311 audio codec with a speaker
 * connector, one WS2812 pixel, SDMMC card slot, and an MX1.25 battery header.
 *
 * The A variant is the same board without the touch controller populated. It
 * cannot run Braino -- touch is the only input this firmware has -- and the
 * static_asserts in BoardConfig.h are what say so.
 *
 * Pins are Freenove's own, cross-checked against the hardware with env:s3diag
 * rather than trusted: the display setup from
 * Libraries/FNK0104AB/TFT_eSPI_Setups_v1.3.zip, the touch and audio pins from
 * Tutorial_With_Touch/Sketches. Chip identity was read off the part itself --
 * ESP32-S3 rev v0.2, 16 MB quad flash, 8 MB embedded octal PSRAM (N16R8).
 *
 * The display's own SPI pins are NOT here: TFT_eSPI is configured entirely
 * through `-D` flags in platformio.ini and reads them from there. BoardConfig.h
 * cross-checks the two descriptions so they cannot drift.
 */
inline constexpr BoardProfile BOARD = {
    "Freenove-FNK0104B",

    /* panel.
     *
     * This board breaks an assumption the other three share: its USB-C sits on
     * a SHORT edge, so rotation 0 -- portrait -- is the orientation with the
     * socket at the bottom, and NO landscape rotation puts it there. Measured
     * on the device: rot 0 is USB-bottom, rot 1 is USB-right, so rot 3 is
     * USB-left. 3 is chosen so the cable exits on the left, away from the hand
     * doing most of the work for a right-handed player. It is an ergonomic
     * choice between two equally correct values, not a derivation. */
    PanelProfile{
        /* nativeWidth          */ 240,
        /* nativeHeight         */ 320,
        /* landscapeRotation    */ 3,
        /* portraitRotation     */ 0,
        /* backlightPin         */ 45,
        /* backlightActiveHigh  */ true,
    },

    /* touch: FT6336U on I2C, SHARED with the ES8311 audio codec at 0x18. The
     * bus is not private to touch, which matters if anything here ever grows a
     * long transaction.
     *
     * The controller reports the panel's native portrait frame and knows
     * nothing about setRotation(); BoardTouch.cpp owns the rotation into
     * screen space. `reset` must be released before the first transaction or a
     * chip that is present reports as absent. */
    TouchProfile{
        /* kind               */ (GUME_TOUCH_CAPACITIVE
                                     ? TouchKind::CapacitiveFt6336u
                                     : TouchKind::ResistiveXpt2046),
        /* mosi               */ PIN_NONE,
        /* miso               */ PIN_NONE,
        /* sclk               */ PIN_NONE,
        /* cs                 */ PIN_NONE,
        /* irq                */ 17,
        /* sda                */ 16,
        /* scl                */ 15,
        /* reset              */ 18,
        /* i2cAddress         */ 0x38,
        /* i2cHz              */ 400000,
        /* pressureThreshold  */ 0,
        /* hitSlop            */ 8,
    },

    /* No SD, deliberately, for now.
     *
     * The slot exists, but it is SDMMC 4-bit (CLK 38, CMD 40, D0 39, D1 41,
     * D2 48, D3 47) and SdProfile describes an SPI card: cs/mosi/miso/sclk.
     * Half-wiring it would be a lie in the profile. PIN_NONE is a supported
     * configuration -- sdReady() is false and optional SD content simply is
     * not loaded, which everything already has defaults for. An SDMMC backend
     * is its own change. */
    SdProfile{
        /* cs    */ PIN_NONE,
        /* mosi  */ PIN_NONE,
        /* miso  */ PIN_NONE,
        /* sclk  */ PIN_NONE,
        /* spiHz */ 0,
    },

    /* No RGB LED that this firmware can drive, for now.
     *
     * The board has ONE WS2812 addressable pixel on GPIO42 -- a timed
     * bitstream, not three PWM channels, so RgbLedProfile cannot express it.
     * PIN_NONE means beepOk()/beepError() become no-ops and the screen saver
     * loses its rally colour; nothing else changes. A WS2812 driver behind the
     * same feedback API is its own change. */
    RgbLedProfile{
        /* r           */ PIN_NONE,
        /* g           */ PIN_NONE,
        /* b           */ PIN_NONE,
        /* commonAnode */ false,
    },

    /* There IS real audio here -- an ES8311 codec at I2C 0x18 with I2S on
     * MCLK 4 / BCLK 5 / DIN 6 / WS 7 / DOUT 8 and an active-LOW amplifier
     * enable on GPIO1 -- and env:s3diag plays through it. But AudioProfile
     * describes a single speaker pin, which a codec is not, so this stays
     * PIN_NONE until the profile can describe one. Claiming a speaker pin the
     * firmware would then toggle uselessly is worse than admitting none. */
    AudioProfile{
        /* speakerPin */ PIN_NONE,
    },

    /* Battery sense on GPIO9 = ADC1_CH8 on the S3, which matters: ADC2 is
     * unusable while Wi-Fi is associated and this radio comes up for NTP.
     * Freenove's own sketch states the divider as x2.0.
     *
     * Measured here: 3.93 V with a pack fitted on USB, against 4.09-4.16 V on
     * USB with NO pack. The no-pack case reads HIGHER, exactly as on the
     * E32R28T-1 -- so no threshold separates them and this board must not grow
     * an isBatteryPresent() either. The charge-inference constants in
     * BoardPower.cpp were tuned against a different charger and have NOT been
     * validated here. */
    BatteryProfile{
        /* adcPin        */ 9,
        /* dividerRatio  */ 2.0f,
        /* sensorMaxVolts*/ 4.50f,
    },

    /* 16 MB part, read off the chip. Partitioned huge_app.csv for now, which
     * uses 3 MB of it -- consistent with the other boards, and one fewer
     * variable during bring-up. */
    MemoryProfile{
        /* flashBytes */ 16u * 1024u * 1024u,
    },
};
