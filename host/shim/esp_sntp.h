// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* No SNTP client: the host's own clock is already set. */

inline void sntp_stop() {}
inline bool sntp_enabled() { return false; }
inline void sntp_setservername(int, const char*) {}
inline void sntp_init() {}
inline void esp_sntp_stop() {}
