// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* What the Android build's own files share: the NativeActivity the system
 * started Braino in. There is no Java in this program -- the activity is
 * the system's own android.app.NativeActivity, and everything from here down
 * is the same src/ and host/ every other host build compiles.
 *
 * Main thread only: android_main()'s thread, which is the firmware's loop. */

struct android_app;

namespace HostAndroid {

/* Set once by android_main() before HostPlatform::begin(). */
void setApp(android_app* app);
android_app* app();

/* Pump the activity's looper once, waiting at most `timeoutMs` (0: don't
 * wait). Delivers window and input events to HostPlatform. False once the
 * system has asked the activity to go. */
bool pump(int timeoutMs);

}   // namespace HostAndroid
