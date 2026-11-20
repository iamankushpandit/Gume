// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* Real suspend: the whole device stopped, memory kept, until something wakes
 * it. Panel sleep (Board::displaySleep()) leaves the processor running so a
 * touch can wake it; this does not, so it can only exist where something other
 * than the processor can start it again.
 *
 * Same shape as PowerProbe: one .cpp per platform. On the ESP32 boards there
 * is no wake source wired -- the touch controller cannot raise one and the
 * BOOT key is a strapping pin -- so SystemSuspendEsp32.cpp answers that it
 * cannot, and the Deep sleep setting is not offered there at all. A Linux
 * handheld can (host/src/HostSuspend.cpp), woken by its power button.
 *
 * The runtime decides WHEN (BrainoApp::tickDeepSleep(): asleep for a while,
 * with the owner's setting on); the platform decides HOW, and may refuse. */

#include <stdint.h>

namespace SystemSuspend {

/* How long the panel has been asleep before the runtime asks to suspend.
 * Long enough that a console put down for a moment still wakes at a touch.
 * Here rather than in the runtime because the Settings footnote states it. */
constexpr uint32_t AFTER_ASLEEP_MS = 15UL * 60UL * 1000UL;

/* Whether this device can suspend and be woken again. Fixed for the life of
 * the program, so cheap to ask from a render path. */
bool supported();

/* Suspend now. Returns once the device has woken, true if it really was
 * suspended. False means it declined (on the charger, say, where the battery
 * is not the question) or failed, and the caller carries on asleep. Blocks for
 * as long as the device sleeps, which can be hours: call it inside a
 * Watchdog::Pause. */
bool enter();

/* What wakes the device from it, one word for the Settings footnote's
 * "...asleep, <hint> wakes it.": "power". */
const char* wakeHint();

}  // namespace SystemSuspend
