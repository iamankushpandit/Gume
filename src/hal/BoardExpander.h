// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "BoardConfig.h"

/* The AW9523 I2C GPIO expanders some boards hang lines off instead of GPIOs
 * -- on the Attaky Core, the touch controller's reset and the status LED.
 * Everything but driveLed() is boot-time only: Board::begin() calls it once,
 * after Wire is up. driveLed() runs when the LED's colour changes, which
 * Board::setRgbColor() decides -- never merely because it was called. All
 * of it from the loop task, as the touch and key reads are. Capacitive builds
 * only, because only they carry Wire. */
#if GUME_TOUCH_CAPACITIVE
namespace BoardExpander {

/* Hold (false) or release (true) the touch reset named by
 * BOARD.touch.resetExpanderAddress / resetExpanderPort. Holding also sets
 * the expander's every other port to an input. */
void driveTouchReset(bool released);

/* Every port of the AW9523 at `address` an input, in GPIO mode. For
 * navigation keys on an expander the touch reset has not already set up --
 * on the Attaky Core they share it, and driveTouchReset() has done this. */
void allInputs(uint8_t address);

/* Set up one half of a keyboard matrix: columns (P0_0..P0_4) inputs, rows
 * (P1_0..P1_4) outputs driven LOW, the LED (P1_5..P1_7) off. False if the
 * expander did not answer -- the module is not attached. */
bool setupKeyboardHalf(uint8_t address);

/* The status LED on BOARD.rgb.expander: its ports made outputs, driven to
 * off. After driveTouchReset(true), because that writes the whole bank. */
void setupLed();

/* Each channel on or off, honouring BOARD.rgb.commonAnode. Read-modify-write
 * of the output register, so only the LED's own bits change -- the touch
 * reset beside them on the Attaky Core is never written from here. */
void driveLed(bool red, bool green, bool blue);

}  // namespace BoardExpander
#endif
