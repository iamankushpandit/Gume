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

struct GreWord {
    const char* word;
    const char* pos;      // "adj." / "n." / "v." / "adv."
    const char* meaning;  // short gloss
    const char* example;  // one sentence
};

extern const GreWord GRE_WORDS[];
extern const uint16_t GRE_WORD_COUNT;
