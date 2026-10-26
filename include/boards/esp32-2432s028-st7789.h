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

/* Resistive XPT2046 on its own bit-banged bus. See e32r28t1.h for why this is
 * a macro and not only a profile field. */
#define GUME_TOUCH_CAPACITIVE 0

#define GUME_HAS_AUDIO_CODEC 0
/* DAC audio on GPIO26, with the touch clock on the other DAC pad, GPIO25.
 * That is the combination 5.5.0 got wrong; beginAudio() now hands GPIO25 back
 * once the driver is up. Same arrangement as the E32R28T-1 -- see e32r28t1.h
 * for the full account. */
#define GUME_HAS_AUDIO_DAC   1

/* ESP32-2432S028, dual-USB, ST7789 PANEL. 2.8-inch, 240x320. The board the
 * community calls the CYD2USB.
 *
 * The same dual-USB board is sold with two different display controllers,
 * sometimes under the same listing, and nothing on the silkscreen says which
 * is inside. esp32-2432s028-inv.h is the one with ILI9341 glass, which wants
 * the ILI9341 sequence plus a runtime inversion; this file is the one with
 * ST7789 glass. The two are the same PCB -- every pin below is identical to
 * the inverted profile's -- and differ only in what the panel is told at
 * init, which is why there is no second measurement here of anything but
 * the display.
 *
 * HOW THIS ONE WAS ESTABLISHED. Reported in issue 200 by an owner of the
 * board: the inverted image draws it correctly but washed out. Three builds
 * went to the reporter, and the answer came from their photographs, not
 * from the words:
 *
 *   ST7789, BGR, no inversion     every colour inverted (described as
 *                                 "vivid but red is blue")
 *   ST7789, RGB, no inversion     inverted AND red/blue exchanged: the
 *                                 launcher's blue, teal and coral tiles
 *                                 drew blue, blue and yellow, and the
 *                                 Dark theme drew white where black is
 *   ST7789, BGR + invertColours   this file
 *
 * The lesson is the inverted sibling's again: a report that the colours
 * are "swapped" cannot tell inversion from colour order, because inverted
 * red IS cyan. Ask for a photograph of the launcher, whose three tile
 * fills and two themes are known values, and read the mapping off it.
 * The maintainer does not own this glass; every display fact here is the
 * reporter's measurement, and the docs page says what was confirmed when.
 *
 * NOT TO BE CONFUSED WITH the Rv3 profile that existed until 5.10.0, which
 * also said ST7789 but put the backlight on IO27 and used rotation 1. That
 * profile was never run, and the E32R32P was found mis-flashed with it. The
 * backlight here is IO21, from the reporter's own working configuration.
 *
 * Measured on the inverted sibling and carried over unchanged: touch pins,
 * backlight IO21, rotation, RGB LED order, no battery sense (GPIO34 is the
 * light sensor), DAC audio on GPIO26, 4 MB flash. Inherited from the
 * E32R28T-1 and not exercised: the SD card.
 *
 * The display's own SPI pins are not here -- TFT_eSPI reads them from the `-D`
 * flags in platformio.ini, and BoardConfig.h cross-checks the two.
 */
inline constexpr BoardProfile BOARD = {
    "ESP32-2432S028-st7789",
    "CYD789",

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

    /* The CYD's published order -- see the note above. Common anode. */
    RgbLedProfile{
        /* r           */ 4,
        /* g           */ 16,
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

    /* No battery sense. GPIO34 on a CYD is the light sensor, not a battery
     * divider; reading it as one showed a light level as a charge. PIN_NONE
     * makes the gauge show no digits, which is the honest answer. */
    BatteryProfile{
        /* adcPin        */ PIN_NONE,
        /* dividerRatio  */ 0.0f,
        /* sensorMaxVolts*/ 0.0f,
    },

    /* 4 MB part, huge_app.csv: 3 MB for the app. Read back from the chip at
     * boot -- see the [boot] mac=... flash= line. */
    MemoryProfile{
        /* flashBytes */ 4u * 1024u * 1024u,
    },
};
