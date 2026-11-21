// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The ESP32 boards' answer to DevicePose: no board has a motion sensor, so
 * there is no pose and Settings offers no Auto. The orientation is the
 * owner's choice, as it always was. */

#include "DevicePose.h"

bool DevicePose::available() { return false; }
DevicePose::Pose DevicePose::current() { return Pose::Unknown; }
