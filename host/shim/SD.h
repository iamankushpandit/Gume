// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "FS.h"
#include "SPI.h"

typedef enum { CARD_NONE, CARD_MMC, CARD_SD, CARD_SDHC, CARD_UNKNOWN } sdcard_type_t;

class SDFS : public fs::FS {
public:
    bool begin(uint8_t = 0, SPIClass& = SPI, uint32_t = 4000000, const char* = "/sd",
               uint8_t = 5, bool = false) {
        return false;
    }
    void end() {}
    sdcard_type_t cardType() { return CARD_NONE; }
    uint64_t cardSize() { return 0; }
    uint64_t totalBytes() { return 0; }
    uint64_t usedBytes() { return 0; }
};

extern SDFS SD;
