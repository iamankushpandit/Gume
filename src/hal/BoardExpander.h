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
 * -- on the Attaky Core, the touch controller's reset and the status LED on
 * one, and the other chips' interrupt lines on a second that is only ever
 * parked. Everything but driveLed() is boot-time only: Board::begin() calls
 * it once, after Wire is up. driveLed() runs when the LED's colour changes,
 * or has not yet been seen to reach the part, which Board decides -- never
 * merely because it was called. All of it from the loop task, as the touch
 * and key reads are. Built only where
 * GUME_HAS_IO_EXPANDER is set -- the Attaky Core. A keyboard module's own
 * AW9523s are its driver's business (hal/modules/KeyboardModule.cpp), since
 * the module is not the board. */
#if GUME_HAS_IO_EXPANDER
namespace BoardExpander {

/* Hold (false) or release (true) the touch reset named by
 * BOARD.touch.resetExpanderAddress / resetExpanderPort. Holding also sets
 * the expander's every other port to an input. */
void driveTouchReset(bool released);

/* Every port of the AW9523 at `address` an input, in GPIO mode. For
 * navigation keys on an expander the touch reset has not already set up --
 * on the Attaky Core they share it, and driveTouchReset() has done this --
 * and, through park(), for an expander the firmware drives no port of, which
 * would otherwise go on driving its power-on levels. */
void allInputs(uint8_t address);

/* The AW9523 at `address` put out of the way: every port an input, as
 * allInputs() leaves it, and its interrupt output switched off for all
 * sixteen. With every port an input the part would otherwise pull its INTN
 * line low on the first change of any of them and hold it there until its
 * input registers were read, which nothing here does. For
 * BOARD.parked.parkedExpander. */
void park(uint8_t address);

/* Both INPUT registers of the AW9523 at `address`: the level on each of its
 * sixteen lines, whatever drives it. False if the part did not answer.
 * Reading them also clears the part's interrupt output. */
bool readInputs(uint8_t address, uint8_t& p0, uint8_t& p1);

/* The status LED on BOARD.rgb.expander: its ports made outputs, driven to
 * off. After driveTouchReset(true), because that writes the whole bank. */
void setupLed();

/* Each channel on or off, honouring BOARD.rgb.commonAnode. Read-modify-write
 * of the output register, so only the LED's own bits change -- the touch
 * reset beside them on the Attaky Core is never written from here.
 *
 * True only when the part is now known to hold that state: the register was
 * read, and either already said so or took the write. False means the bus
 * did not answer and NOTHING may be assumed about the LED -- the caller must
 * not record the colour as shown, or a single failed transaction leaves a
 * light on that the firmware believes is off and never tries again. */
bool driveLed(bool red, bool green, bool blue);

}  // namespace BoardExpander
#endif
