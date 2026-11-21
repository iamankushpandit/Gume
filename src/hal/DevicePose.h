// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>

/* Which way up the device is being held -- the one question about the panel
 * only a motion sensor can answer.
 *
 * No ESP32 board Braino supports has one, so the orientation there is the
 * owner's choice in Settings and nothing else. A device that does -- the
 * Meta Portal+, whose stand pivots the screen between landscape and portrait
 * -- reports its pose here, and with Auto chosen in Settings the layout and
 * the half-turn follow the device instead of the setting (Board::layoutMode(),
 * Board::screenFlipped()).
 *
 * One implementation per platform: hal/DevicePoseEsp32.cpp answers that there
 * is no sensor; host/src/HostDevicePose.cpp answers the same on a desktop;
 * host/android/src/HostDevicePoseAndroid.cpp reads the accelerometer.
 *
 * The poses are named for what the player sees, in the device's own terms:
 * Landscape is the way it stands to be read in landscape, and Portrait is a
 * quarter-turn from that, as Settings' Landscape and Portrait are. A pose is
 * reported only once it has settled, so a device carried across a room does
 * not turn the screen at every step, and a device lying flat keeps the last
 * pose it was held in. */
namespace DevicePose {

enum class Pose : uint8_t {
    Unknown = 0,       // no sensor, or none settled yet
    Landscape,
    LandscapeFlipped,  // Landscape, half a turn round
    Portrait,
    PortraitFlipped,   // Portrait, half a turn round
};

/* Whether this device has a sensor at all. Settings offers Auto only where it
 * does. Constant for the run. */
bool available();

/* The settled pose. Cheap: the runtime asks every frame. */
Pose current();

}  // namespace DevicePose
