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

enum AppCapability : uint32_t {
    APP_CAP_NONE            = 0,
    APP_CAP_DEVICE_STATUS   = 1UL << 0,
    APP_CAP_DEVICE_SETTINGS = 1UL << 1,
    APP_CAP_NETWORK         = 1UL << 2,
    APP_CAP_PROFILES        = 1UL << 3,
    APP_CAP_SCORES          = 1UL << 4,
    APP_CAP_FACTORY_RESET   = 1UL << 5,
    APP_CAP_DIAGNOSTICS     = 1UL << 6,
    /* Not what the app may do but who may open it: only while the admin
     * profile is active. A non-admin player does not see the app at all --
     * no launcher tile, and for Settings no gear -- and launch() refuses it
     * by every other route. See AppDefinition::availableTo(). */
    APP_CAP_ADMIN_ONLY      = 1UL << 7,
};
