// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The ESP object, answered for a host. Heap figures are reported as a fixed,
 * healthy picture rather than read from the PC: System Info and the screen
 * leak check compare them, and a desktop heap measured in gigabytes would
 * only make those comparisons noisy. The firmware's own allocation rules are
 * checked on the device, where they mean something. */

#include <cstdint>

class EspClass {
public:
    uint32_t getHeapSize() { return 327680; }
    uint32_t getFreeHeap() { return 200000; }
    uint32_t getMinFreeHeap() { return 200000; }
    uint32_t getMaxAllocHeap() { return 110000; }
    uint32_t getPsramSize() { return 0; }
    uint32_t getFreePsram() { return 0; }
    const char* getChipModel() { return "Host"; }
    uint8_t getChipRevision() { return 0; }
    uint8_t getChipCores() { return 1; }
    uint32_t getCpuFreqMHz() { return 0; }
    uint32_t getFlashChipSize() { return 16u * 1024u * 1024u; }
    uint32_t getFlashChipSpeed() { return 0; }
    uint32_t getSketchSize() { return 0; }
    uint32_t getFreeSketchSpace() { return 0; }
    const char* getSdkVersion() { return "host"; }
    uint64_t getEfuseMac();
    [[noreturn]] void restart();
};

extern EspClass ESP;
