// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "BoardProfile.h"

/* Capacitive touch, as a macro the preprocessor can act on -- see
 * fnk0104b.h, which explains why this cannot be the profile field alone. */
#define GUME_TOUCH_CAPACITIVE 1

/* A bare I2S amplifier (the Media I/O module's NS4168), so neither the codec
 * nor the built-in DAC backend -- see the audio block below. */
#define GUME_HAS_AUDIO_I2S 1

/* Attaky Core 1.0 -- the ESP32-S3 module at the heart of every Attaky modular
 * handheld, here as the Cyber Deck Plus: Core + Expand: Media I/O + Control:
 * Keyboard (Bottom) + Control: Gamepad (Bottom) + Power: Standard Cell.
 *
 * NOT YET RUN ON THE HARDWARE. Every value below is read off Attaky's own
 * documents and checked against two firmwares Attaky ships for this board,
 * not measured here:
 *
 *   - the host pin map in Attaky-Firmware-Builder's
 *     shared/hardware-catalog.md (github.com/Attaky-Module), which is the one
 *     source that states the display's SPI pins;
 *   - docs.attaky.com's Core, Media I/O, Gamepad and Standard Cell
 *     datasheets and its I2C address map;
 *   - Attaky's MeshCore fork (variants/attaky_core) and Retro-Go overlay
 *     (targets/ATK-RetroSeries-V1/config.h), which agree with the catalog on
 *     every pin they both name.
 *
 * The module is an ESP32-S3-WROOM-1U-N16R8, the same part as the FNK0104B,
 * which is why [esp32s3_common] serves both.
 *
 * Three things are this board's own and are easy to get wrong:
 *
 *   1. The panel is mounted half a turn from the controller's default, so
 *      under TFT_eSPI landscape is setRotation(3) and portrait is (2) --
 *      Attaky's numbers, and they warn that other driver libraries need
 *      different ones. To be confirmed in all four positions on the bench,
 *      touch included.
 *   2. The touch controller's RESET and INT are not GPIOs. Both are ports of
 *      AW9523 I2C expanders on the same bus as the touch controller -- see
 *      the touch block below.
 *   3. USB is a CH340X bridge into UART0, not the S3's native USB, so the
 *      environment builds with ARDUINO_USB_CDC_ON_BOOT=0 and uploads at
 *      460800; Attaky report 921600 is unreliable through that bridge.
 *
 * The name is used to say which hardware this firmware is for. Attaky is
 * Attaky's trademark; see NOTICE.md on keeping the two apart.
 */
inline constexpr BoardProfile BOARD = {
    "Attaky-Core",
    "ATK",

    /* panel: ST7789, 2.0-inch, 240x320 native, on its own SPI bus (CS 45,
     * DC 12, RST 10, SCLK 11, MOSI 13, no MISO -- in platformio.ini, where
     * TFT_eSPI reads them). Backlight on IO14, high = on, and on a plain GPIO,
     * so the brightness setting and panel sleep both work.
     *
     * Attaky's Retro-Go target sends INVON in its init, so the panel wants
     * the inversion that TFT_INVERSION_ON gives the ST7789 sequence; that is
     * how platformio.ini states it, and invertColours stays false because the
     * driver is already the panel's own. If the first picture is a photo
     * negative, this is the line to change. */
    PanelProfile{
        /* nativeWidth          */ 240,
        /* nativeHeight         */ 320,
        /* landscapeRotation    */ 3,
        /* portraitRotation     */ 2,
        /* backlightPin         */ 14,
        /* backlightActiveHigh  */ true,
        /* invertColours        */ false,
    },

    /* touch: FocalTech FT6636 at 0x38, the FT6x36 family whose report
     * registers the FT6336U path already reads. Shared I2C bus on SDA IO2,
     * SCL IO1, 400 kHz, with two AW9523 expanders (0x58, 0x59), a QMI8658A IMU
     * (0x6B), an STK3311 light sensor (0x48) and, on the Power module, a
     * MAX17048 fuel gauge (0x36). Only the touch controller and the 0x59
     * expander are spoken to.
     *
     * RESET is port P1_3 of the AW9523 at 0x59 -- port 11 in the 0..15
     * numbering -- active low. Board::begin() drives it through the expander,
     * held through the panel's init and released after, exactly as a GPIO
     * reset is.
     *
     * INT is port P0_4 of the AW9523 at 0x58, whose own interrupt output
     * reaches the ESP32 on IO4. The capacitive path never reads an interrupt
     * -- it polls the controller once a frame -- so `irq` names IO4 to say
     * where the line arrives, and `irqUsable` is false because the level on
     * IO4 means "one of sixteen expander ports changed", not "a finger is
     * down". */
    TouchProfile{
        /* kind                 */ (GUME_TOUCH_CAPACITIVE
                                       ? TouchKind::CapacitiveFt6336u
                                       : TouchKind::ResistiveXpt2046),
        /* mosi                 */ PIN_NONE,
        /* miso                 */ PIN_NONE,
        /* sclk                 */ PIN_NONE,
        /* cs                   */ PIN_NONE,
        /* irq                  */ 4,
        /* irqUsable            */ false,
        /* sda                  */ 2,
        /* scl                  */ 1,
        /* reset                */ PIN_NONE,
        /* resetExpanderAddress */ 0x59,
        /* resetExpanderPort    */ 11,
        /* i2cAddress           */ 0x38,
        /* i2cHz                */ 400000,
        /* pressureThreshold    */ 0,
        /* hitSlop              */ 8,
    },

    /* BOOT on IO38, a direct GPIO with a pull-up -- the one key of the Core's
     * nine that is not behind the 0x59 expander. The D-pad, Enter and L1 are
     * read through it as navigation keys, below. Touch stays the input this
     * console is built around. */
    ButtonProfile{
        /* bootPin   */ 38,
        /* activeLow */ true,
    },

    /* keys: the Core's D-pad and Enter, on the same AW9523 at 0x59 as the
     * touch reset, port bank P0, active low with external pull-ups -- Attaky's
     * catalog and the Core datasheet: DOWN P0_0, LEFT P0_1, ENTER P0_2,
     * RIGHT P0_3, UP P0_4. Back is the top-left shoulder key, L1, on P0_5.
     * Board::begin() already leaves every port but the touch reset an input.
     * The power key's SHORT press is P0_7 and locks the console, as the Lock
     * button does; its long press switches the power in hardware and never
     * reaches the firmware. R1 (P0_6) is not read. Which key is which has not
     * been seen on a device; the directions are stated as the catalog states
     * them. */
    KeysProfile{
        /* keyExpander   */ 0x59,
        /* keyUp         */ 4,
        /* keyDown       */ 0,
        /* keyLeft       */ 1,
        /* keyRight      */ 3,
        /* keyEnter      */ 2,
        /* keyBack       */ 5,
        /* keyPower      */ 7,
        /* keysActiveLow */ true,
    },

    /* keyboard: Attaky's Control: Keyboard (Bottom) 1.0, when it is clipped
     * on -- which it is on the Cyber Deck Plus, and which Board::begin()
     * checks rather than assumes. Left half at 0x5A, right at 0x5B, on the
     * shared I2C bus through the module's FPC. Layout from Attaky's hardware
     * catalog. Left: 1-5, Q-T, A-G, Shift Z-V, then LAG TAB ALT (not used)
     * , and Space. Right: 6-0, Y-P, H-L and Delete, B N M, an empty position
     * and Enter, then an empty position, the full stop, MIC (not used), # and
     * Fn (not used). Not yet seen on a device. */
    KeyboardProfile{
        /* keyboardLeft  */ 0x5A,
        /* keyboardRight */ 0x5B,
        /* leftLayout    */ "12345" "QWERT" "ASDFG" "^ZXCV" "~~~, ",
        /* rightLayout   */ "67890" "YUIOP" "HJKL<" "BNM~>" "~.~#~",
    },

    /* microSD on the Expand: Media I/O module, in SPI mode: CS (the card's
     * CD/DAT3) on IO8, MOSI (CMD) IO6, MISO (DAT0) IO7, SCLK IO5. Those lines
     * belong to whichever Expand module is fitted -- the LoRa/GPS module puts
     * its radio on the same four -- so this build is for a Core wearing Media
     * I/O, which the Cyber Deck Plus does -- and Board::begin() looks for the
     * other two before bringing the slot up; see expandProbe below. */
    SdProfile{
        /* cs    */ 8,
        /* mosi  */ 6,
        /* miso  */ 7,
        /* sclk  */ 5,
        /* spiHz */ 16000000,
    },

    /* Two Expand modules other than Media I/O reuse the SD lines (IO5-8): the
     * LoRa/GPS module puts its radio's SPI there and the FM/AM module a GPIO,
     * so a Core wearing either would have them driven as an SD card.
     * Board::begin() looks for both before mountSd(), passive-first, as
     * Attaky's shared/module-detection-and-safe-boot.md lays out: the FM/AM
     * module's TEF6687 answers at 0x64 on that module's own I2C bus (SDA
     * IO42, SCL IO41), and the LoRa/GPS module's AT6558 streams NMEA at 9600
     * baud into IO17 unasked, which is listened to with the pin an input.
     * Media I/O has no identity to find; finding neither is what lets the SD
     * slot come up. */
    ExpandProbeProfile{
        /* fmRadioAddress */ 0x64,
        /* fmSda          */ 42,
        /* fmScl          */ 41,
        /* gpsRx          */ 17,
        /* gpsBaud        */ 9600,
    },

    /* The RGB LED is on the 0x59 expander, not on GPIOs: red P1_0, green
     * P1_1, blue P1_2 -- ports 8, 9 and 10 -- common anode, so a channel
     * lights when its port is driven LOW. Driven in the expander's GPIO mode,
     * so each channel is on or off: seven colours, no dimming. P1_3 beside
     * them is the touch reset, and the LED driver never writes it. */
    RgbLedProfile{
        /* r           */ 8,
        /* g           */ 9,
        /* b           */ 10,
        /* commonAnode */ true,
        /* expander    */ 0x59,
    },

    /* audio: Media I/O's NS4168 I2S class-D amplifier -- BCLK IO17, word
     * select IO21, data IO18, from Attaky's hardware catalog, and the same
     * three Attaky's Retro-Go target drives. No codec, no MCLK and no enable
     * line, so GUME_HAS_AUDIO_I2S: standard I2S at 16kHz with volume applied
     * in software. See BoardAudioBackend.cpp.
     *
     * maxVolume is 50, and that is caution rather than a measurement: nobody
     * has heard this amplifier or its speaker yet. The ceilings that exist
     * were set by ear -- 85 on the FNK0104B's codec, 75 on the CYD DAC boards,
     * whose speaker distorts above it -- and a class-D part fed full-scale
     * samples has nothing behind it to soften a wrong guess. Raise it on the
     * device, listening to the boot phrase and the piano at the top of the
     * slider, and record what was heard here. */
    AudioProfile{
        /* speakerPin          */ PIN_NONE,
        /* codecI2cAddress     */ 0,
        /* i2sMclk             */ PIN_NONE,
        /* i2sBclk             */ 17,
        /* i2sWordSelect       */ 21,
        /* i2sDataOut          */ 18,
        /* i2sDataIn           */ PIN_NONE,
        /* ampEnablePin        */ PIN_NONE,
        /* ampEnableActiveLow  */ false,
        /* maxVolume           */ 50,
    },

    /* No ADC battery sense. The Standard Cell module reports the cell through
     * a MAX17048 fuel gauge at 0x36 on the shared bus and wires no divider to
     * any GPIO, so the gauge blanks its digits. Reading the fuel gauge is its
     * own change, and a better answer than a divider: it reports charge, not
     * a voltage to be guessed from. */
    BatteryProfile{
        /* adcPin        */ PIN_NONE,
        /* dividerRatio  */ 0.0f,
        /* sensorMaxVolts*/ 0.0f,
    },

    /* 16 MB QuadSPI flash on the N16R8; partitioned huge_app.csv like every
     * other board, which uses 3 MB of it. */
    MemoryProfile{
        /* flashBytes */ 16u * 1024u * 1024u,
    },
};
