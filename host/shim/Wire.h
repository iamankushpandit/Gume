// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* I2C, with exactly one device on the bus: an emulated FT6336U touch
 * controller at the host board's touch address. It answers the one
 * transaction BoardTouch.cpp makes -- point at TD_STATUS, read five bytes --
 * with the pointer the host platform is tracking, in the panel's native
 * portrait frame, so Board::mapTouch() rotates it exactly as it would a real
 * controller's report. Anything else on the bus NACKs. */

#include <cstddef>
#include <cstdint>

class TwoWire {
public:
    bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0);
    bool end() { return true; }
    void setClock(uint32_t) {}
    void beginTransmission(uint8_t address);
    void beginTransmission(int address) { beginTransmission(static_cast<uint8_t>(address)); }
    size_t write(uint8_t value);
    size_t write(const uint8_t* data, size_t len);
    uint8_t endTransmission(bool sendStop = true);
    uint8_t requestFrom(uint8_t address, uint8_t quantity, bool sendStop = true);
    uint8_t requestFrom(int address, int quantity, int sendStop = 1) {
        return requestFrom(static_cast<uint8_t>(address), static_cast<uint8_t>(quantity),
                           sendStop != 0);
    }
    int available();
    int read();

private:
    uint8_t address_ = 0;
    uint8_t reg_ = 0;
    bool haveReg_ = false;
    uint8_t rx_[16] = {0};
    uint8_t rxLen_ = 0;
    uint8_t rxAt_ = 0;
};

extern TwoWire Wire;
