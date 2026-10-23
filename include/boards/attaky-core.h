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

    /* BOOT on IO38, a direct GPIO with a pull-up. It is the one key of the
     * Core's nine the firmware reads; the D-pad, Enter, L1, R1 and the power
     * key's short press are on the 0x59 expander and are not read. Touch stays
     * the input this console is built around. */
    ButtonProfile{
        /* bootPin   */ 38,
        /* activeLow */ true,
    },

    /* microSD on the Expand: Media I/O module, in SPI mode: CS (the card's
     * CD/DAT3) on IO8, MOSI (CMD) IO6, MISO (DAT0) IO7, SCLK IO5. Those lines
     * belong to whichever Expand module is fitted -- the LoRa/GPS module puts
     * its radio on the same four -- so this build is for a Core wearing Media
     * I/O, which the Cyber Deck Plus does. */
    SdProfile{
        /* cs    */ 8,
        /* mosi  */ 6,
        /* miso  */ 7,
        /* sclk  */ 5,
        /* spiHz */ 16000000,
    },

    /* The RGB LED is on the 0x59 expander (red P1_0, green P1_1, blue P1_2,
     * common anode), not on GPIOs, so RgbLedProfile cannot describe it. Left
     * dark: beepOk()/beepError() lose their pulse and nothing else changes.
     * Driving it through the expander is its own change. */
    RgbLedProfile{
        /* r           */ PIN_NONE,
        /* g           */ PIN_NONE,
        /* b           */ PIN_NONE,
        /* commonAnode */ true,
    },

    /* SILENT, for now. Media I/O has an NS4168 I2S class-D amplifier -- BCLK
     * IO17, word select IO21, data IO18 -- with no codec, no MCLK and no
     * enable line. Neither audio backend fits it: GUME_HAS_AUDIO_CODEC drives
     * an ES8311 over I2C, GUME_HAS_AUDIO_DAC the ESP32's built-in DAC, which
     * the S3 does not have. A plain-I2S backend is the codec path without its
     * register writes, and it is its own change; until then every
     * playSound() is a no-op and Settings says the board cannot make sound. */
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
        /* maxVolume           */ 0,
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
