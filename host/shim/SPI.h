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

/* The host has no SPI bus. Board keeps an SPIClass for the SD slot, which the
 * host board does not wire. */
#define HSPI 2
#define VSPI 3
#define FSPI 1

class SPIClass {
public:
    explicit SPIClass(uint8_t bus = 0) { (void)bus; }
    void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
    void end() {}
};

extern SPIClass SPI;
