// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The PC and handheld build's answer to SleepPower: nothing to do here.
 *
 * HostSleep follows the panel's Sleep In and Sleep Out and already takes three
 * of the four cores and the OS's Wi-Fi interface down (HostSleep.cpp), which
 * is the Linux way of doing what SleepPowerEsp32.cpp does on a board. Doing it
 * a second time from here would fight it. Answering false from stopStation()
 * leaves Board's network state machine exactly as it was before SleepPower
 * existed. */

#include "hal/SleepPower.h"

namespace SleepPower {

void lowerClock() {}
void restoreClock() {}
bool stopStation() { return false; }
bool startStation() { return false; }

}  // namespace SleepPower
