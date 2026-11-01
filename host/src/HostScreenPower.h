// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The handheld's real backlight, switched off while the firmware's panel is
 * asleep.
 *
 * On a board, Board::displaySleep() drives the backlight PWM to zero and
 * sends the panel Sleep In, and the lamp goes out. Here the PWM is only a
 * brightness multiplier on the window (HostPlatform::setBacklight()), so a
 * sleeping Braino used to draw black frames under a fully lit lamp -- the
 * single largest draw on a handheld, spent showing nothing. The panel shim
 * calls setAwake() on Sleep In and Sleep Out, and this turns the lamp itself
 * off and back on.
 *
 * Two ways in, tried in order, and the log says which one it found:
 *
 *   - /dev/disp, the Allwinner display engine's own device (the RG40XXV's
 *     H700): DISP_LCD_BACKLIGHT_DISABLE / _ENABLE, with the brightness read
 *     before and put back after.
 *   - /sys/class/backlight/<any>: bl_power and brightness, the kernel's
 *     generic backlight class.
 *
 * A Braino killed while asleep must not leave the console dark -- to the
 * player that is a dead device. So the brightness to come back to is written
 * to the data directory before the lamp goes out, and the next start
 * (Braino.sh restarts it at once) puts it back; SIGTERM, SIGINT and SIGHUP
 * restore it on the way out too.
 *
 * Built only with GUME_HOST_SCREEN_POWER (cross-compiled builds: handhelds).
 * A desktop build leaves the computer's own screen alone. */
namespace HostScreenPower {

/* Once, at start: restore a backlight a previous run left off, and arm the
 * signal handlers. */
void begin();

/* The panel shim's Sleep In (false) and Sleep Out (true). */
void setAwake(bool awake);

/* Put the lamp back on if it is off. Called on the way out. */
void restore();

}   // namespace HostScreenPower
