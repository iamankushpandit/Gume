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

/*
 * The 50 US states with their capitals and postal codes.
 *
 * This replaces the world-outline game, which had to go for licensing reasons
 * (the outline artwork forbids resale). State data is plain public-domain fact,
 * so it carries no such restriction.
 */
struct StateFact {
    const char* code;      // "TX"
    const char* name;      // "Texas"
    const char* capital;   // "Austin"
    uint8_t tier;          // 1 = most familiar, 2 = well known, 3 = the rest
};

extern const StateFact STATE_FACTS[];
extern const uint8_t STATE_COUNT;
