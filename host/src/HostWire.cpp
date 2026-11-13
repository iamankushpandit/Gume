// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The emulated FT6336U. See host/shim/Wire.h.
 *
 * TD_STATUS (0x02) is followed by the first touch point, so the five bytes
 * BoardTouch.cpp reads are: touch count, XH, XL, YH, YL -- with the event
 * flag in XH's top bits, which the firmware masks off. Coordinates are in
 * the panel's native portrait frame, the inverse of the quarter-turn
 * Board::mapTouch() applies: landscape (lx, ly) is reported as
 * (nativeWidth - 1 - ly, lx). */

#include <Wire.h>

#include "HostPlatform.h"
#include "boards/host.h"

TwoWire Wire;

namespace {
constexpr uint8_t FT_REG_TD_STATUS = 0x02;
}

bool TwoWire::begin(int, int, uint32_t) { return true; }

void TwoWire::beginTransmission(uint8_t address) {
    address_ = address;
    haveReg_ = false;
}

size_t TwoWire::write(uint8_t value) {
    if (!haveReg_) {
        reg_ = value;
        haveReg_ = true;
    }
    return 1;
}

size_t TwoWire::write(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) write(data[i]);
    return len;
}

uint8_t TwoWire::endTransmission(bool) {
    return address_ == BOARD.touch.i2cAddress ? 0 : 2;   // 2: address NACK
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, bool) {
    rxLen_ = 0;
    rxAt_ = 0;
    if (address != BOARD.touch.i2cAddress) return 0;
    if (quantity > sizeof(rx_)) quantity = sizeof(rx_);

    uint8_t regs[16] = {0};
    const HostPlatform::Contact c = HostPlatform::contact();
    if (c.down) {
        const uint16_t rawX = static_cast<uint16_t>(BOARD.panel.nativeWidth - 1 - c.y);
        const uint16_t rawY = static_cast<uint16_t>(c.x);
        regs[FT_REG_TD_STATUS] = 1;
        regs[FT_REG_TD_STATUS + 1] = static_cast<uint8_t>(0x80 | ((rawX >> 8) & 0x0F));   // contact event
        regs[FT_REG_TD_STATUS + 2] = static_cast<uint8_t>(rawX & 0xFF);
        regs[FT_REG_TD_STATUS + 3] = static_cast<uint8_t>((rawY >> 8) & 0x0F);
        regs[FT_REG_TD_STATUS + 4] = static_cast<uint8_t>(rawY & 0xFF);
    }
    for (uint8_t i = 0; i < quantity; ++i) {
        const uint8_t r = static_cast<uint8_t>(reg_ + i);
        rx_[i] = r < sizeof(regs) ? regs[r] : 0;
    }
    rxLen_ = quantity;
    return quantity;
}

int TwoWire::available() { return rxLen_ - rxAt_; }

int TwoWire::read() { return rxAt_ < rxLen_ ? rx_[rxAt_++] : -1; }
