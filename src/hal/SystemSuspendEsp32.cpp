// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The boards' answer to SystemSuspend: they cannot. esp_deep_sleep needs a
 * wake source, and none of these boards wires one -- the touch controllers
 * raise no interrupt the RTC can see, and BOOT is a strapping pin. A deep
 * sleep nothing can end is a device that looks dead, so the setting is simply
 * not offered here. See SystemSuspend.h. */

#include "SystemSuspend.h"

namespace SystemSuspend {

bool supported() { return false; }
bool enter() { return false; }
const char* wakeHint() { return ""; }

}  // namespace SystemSuspend
