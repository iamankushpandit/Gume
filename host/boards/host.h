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

/* THE HOST BUILD'S BOARD: a desktop or a Linux handheld pretending to be one
 * of ours, so the firmware under src/ compiles and runs unchanged.
 *
 * This is not a board in the sense of include/boards/ and is deliberately not
 * kept there. A header in that directory is a claim of support -- a
 * [board_*] section, an installer entry, a CI build -- and none of that means
 * anything for a program run on a PC. tools/check_boards.py never sees this
 * file, which is the point.
 *
 * Every "pin" below is a VIRTUAL line. The host platform (host/src/) answers
 * for each one: the backlight is a brightness multiplier on the window, the
 * touch controller is an FT6336U emulated over the Wire shim and fed by the
 * mouse or the gamepad pointer, and the BOOT key is the Home button. Choosing
 * the capacitive controller is what makes the emulation honest: it reports
 * pixels and has nothing to calibrate, so BoardTouch.cpp runs exactly the path
 * it runs on the FNK0104B and the calibration wizard is never offered.
 *
 * The panel is the canvas size exactly, 240x320 native and landscape at
 * rotation 1, so ScaledRenderer stays a passthrough and the window scales the
 * finished picture instead -- 2x on a 640x480 handheld, which is an exact
 * fit. */

namespace HostPin {
constexpr int8_t BACKLIGHT = 1;
constexpr int8_t TOUCH_IRQ = 2;
constexpr int8_t TOUCH_SDA = 3;
constexpr int8_t TOUCH_SCL = 4;
constexpr int8_t HOME      = 5;
constexpr int8_t I2S_BCLK  = 6;
constexpr int8_t I2S_WS    = 7;
constexpr int8_t I2S_DOUT  = 8;
constexpr int8_t LED_R     = 9;
constexpr int8_t LED_G     = 10;
constexpr int8_t LED_B     = 11;
constexpr int8_t BATTERY   = 12;
}   // namespace HostPin

/* A handheld build reads its battery (host/src/HostBattery.h); a desktop
 * build does not, because a sense line that always reads nothing would show
 * a fault badge where a board without battery sense shows none at all.
 * CMake sets this for cross-compiled builds. */
#ifndef GUME_HOST_BATTERY
#define GUME_HOST_BATTERY 0
#endif

#define GUME_TOUCH_CAPACITIVE 1
/* The synthesiser's codec path is the one that emits signed 16-bit stereo at
 * 16 kHz, which is what an SDL audio device wants. host/src/HostI2s.cpp plays
 * the role of the ES8311 and its volume register. */
#define GUME_HAS_AUDIO_CODEC 1
#define GUME_HAS_AUDIO_DAC   0

inline constexpr BoardProfile BOARD = {
    "Host-SDL",
    "HOST",
    PanelProfile{
        240,                      // nativeWidth
        320,                      // nativeHeight
        1,                        // landscapeRotation
        0,                        // portraitRotation
        HostPin::BACKLIGHT,       // backlightPin
        true,                     // backlightActiveHigh
        false,                    // invertColours
    },
    TouchProfile{
        TouchKind::CapacitiveFt6336u,
        PIN_NONE,                 // mosi
        PIN_NONE,                 // miso
        PIN_NONE,                 // sclk
        PIN_NONE,                 // cs
        HostPin::TOUCH_IRQ,       // irq
        true,                     // irqUsable
        HostPin::TOUCH_SDA,       // sda
        HostPin::TOUCH_SCL,       // scl
        PIN_NONE,                 // reset
        0x38,                     // i2cAddress -- the FT6336U's own
        400000,                   // i2cHz
        0,                        // pressureThreshold (capacitive)
        8,                        // hitSlop
    },
    ButtonProfile{
        HostPin::HOME,            // bootPin
        true,                     // activeLow
    },
    SdProfile{
        PIN_NONE, PIN_NONE, PIN_NONE, PIN_NONE, 0,
    },
    /* The handheld's light, as the firmware's status LED: see
     * host/src/HostLed.h. A device without one simply shows nothing. */
    RgbLedProfile{
        HostPin::LED_R, HostPin::LED_G, HostPin::LED_B, false,
    },
    AudioProfile{
        PIN_NONE,                 // speakerPin
        0x18,                     // codecI2cAddress -- nominal; nothing is on the bus
        PIN_NONE,                 // i2sMclk
        HostPin::I2S_BCLK,        // i2sBclk
        HostPin::I2S_WS,          // i2sWordSelect
        HostPin::I2S_DOUT,        // i2sDataOut
        PIN_NONE,                 // i2sDataIn
        PIN_NONE,                 // ampEnablePin
        false,                    // ampEnableActiveLow
        85,                       // maxVolume -- the codec board's ceiling
    },
#if GUME_HOST_BATTERY
    /* The divider is nominal: analogRead() answers with the cell voltage over
     * it, and esp_adc_cal_raw_to_voltage() is exact on the host, so the
     * firmware multiplies back to the voltage Linux reported. */
    BatteryProfile{
        HostPin::BATTERY, 2.0f, 4.50f,
    },
#else
    BatteryProfile{
        PIN_NONE, 0.0f, 0.0f,
    },
#endif
    MemoryProfile{
        16u * 1024u * 1024u,      // nominal; nothing is flashed
    },
};
