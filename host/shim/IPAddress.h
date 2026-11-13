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
#include <cstdio>
#include "WString.h"

class IPAddress {
public:
    IPAddress() = default;
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : b_{a, b, c, d} {}
    /* From an in_addr word, i.e. network byte order in memory. */
    static IPAddress fromRaw(uint32_t raw) {
        IPAddress ip;
        const auto* p = reinterpret_cast<const uint8_t*>(&raw);
        for (int i = 0; i < 4; ++i) ip.b_[i] = p[i];
        return ip;
    }
    uint32_t raw() const {
        uint32_t out = 0;
        auto* p = reinterpret_cast<uint8_t*>(&out);
        for (int i = 0; i < 4; ++i) p[i] = b_[i];
        return out;
    }
    uint8_t operator[](int i) const { return b_[i & 3]; }
    String toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b_[0], b_[1], b_[2], b_[3]);
        return String(buf);
    }

private:
    uint8_t b_[4] = {0, 0, 0, 0};
};
