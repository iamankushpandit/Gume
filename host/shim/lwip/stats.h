// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once
#include <cstdint>
struct stats_proto {
    uint32_t xmit;
    uint32_t recv;
    uint32_t drop;
    uint32_t err;
};
struct stats_ {
    stats_proto link;
};
extern stats_ lwip_stats;
