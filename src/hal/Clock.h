// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <Arduino.h>

namespace Clock {
void begin();
String timeText();
/** "Tue Aug 11 2026", or "--" while the clock is unsynced. */
String dateText();
uint32_t minuteKey();
/** True when the displayed time came from NTP rather than the build clock. */
bool synced();
}

