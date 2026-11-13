// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once
#include <cstddef>
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DEFAULT (1 << 12)
inline size_t heap_caps_get_largest_free_block(unsigned) { return 110000; }
inline size_t heap_caps_get_free_size(unsigned) { return 200000; }
