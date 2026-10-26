// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "BoardExpander.h"

#if GUME_TOUCH_CAPACITIVE
#include <Wire.h>

namespace BoardExpander {

namespace {
/* A touch reset that lives on an AW9523 I2C expander rather than a GPIO --
 * the Attaky Core. Used twice in Board::begin(), at boot only: once to assert
 * reset, once to release it. Nothing on a frame path touches the expander.
 *
 * Every register this board's state depends on is written outright rather
 * than trusted, because the AW9523 does not power up as inputs: at 0x59 it
 * drives P0_4..P0_7 LOW, and a previous firmware may have left any port in
 * any state, the reset line included. A software reset would also do it, but
 * the part wants a settle after one and a boot-time delay() is exactly what
 * check_frame_rules.py keeps out of the firmware; five plain writes leave the
 * same pins in the same state without one.
 *
 * Only the reset port is made an output here. Every other port -- the Core's
 * buttons, its LED cathodes, the lines passed through to the modules -- is an
 * input, which is high-impedance and therefore the safest thing this
 * firmware can leave a line it does not use. setupLed() then makes the LED's
 * three ports outputs, by read-modify-write, and nothing else. */
constexpr uint8_t AW9523_OUTPUT_P0 = 0x02;
constexpr uint8_t AW9523_CONFIG_P0 = 0x04;   // 1 = input, 0 = output
constexpr uint8_t AW9523_MODE_P0   = 0x12;   // 1 = GPIO, 0 = LED current
constexpr uint8_t AW9523_MODE_P1   = 0x13;

void aw9523Write(uint8_t address, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

bool aw9523Read(uint8_t address, uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(static_cast<int>(address), 1) != 1) {
        return false;
    }
    value = static_cast<uint8_t>(Wire.read());
    return true;
}

/* Change only `mask`'s bits of one register. A failed read writes nothing:
 * guessing the other bits is how a colour would reach the touch reset. */
void aw9523Update(uint8_t address, uint8_t reg, uint8_t mask, uint8_t bits) {
    if (mask == 0) return;
    uint8_t value = 0;
    if (!aw9523Read(address, reg, value)) return;
    const uint8_t next = static_cast<uint8_t>((value & ~mask) | (bits & mask));
    if (next != value) aw9523Write(address, reg, next);
}

/* The LED's port bits in bank `bank` (0 = P0, 1 = P1), and of those, the
 * ones a channel that is `on` should drive HIGH. */
uint8_t ledMask(uint8_t bank) {
    uint8_t mask = 0;
    for (int8_t port : {BOARD.rgb.r, BOARD.rgb.g, BOARD.rgb.b}) {
        if (port != PIN_NONE && port / 8 == bank) mask |= static_cast<uint8_t>(1u << (port % 8));
    }
    return mask;
}

uint8_t ledLevels(uint8_t bank, bool red, bool green, bool blue) {
    uint8_t high = 0;
    const struct { int8_t port; bool on; } channels[] = {
        {BOARD.rgb.r, red}, {BOARD.rgb.g, green}, {BOARD.rgb.b, blue}};
    for (const auto& c : channels) {
        if (c.port == PIN_NONE || c.port / 8 != bank) continue;
        /* Common anode lights on LOW, so an unlit channel is the HIGH one. */
        if (c.on != BOARD.rgb.commonAnode) high |= static_cast<uint8_t>(1u << (c.port % 8));
    }
    return high;
}

}  // namespace

void driveTouchReset(bool released) {
    const uint8_t bank = BOARD.touch.resetExpanderPort / 8;   // 0 = P0, 1 = P1
    const uint8_t bit = static_cast<uint8_t>(1u << (BOARD.touch.resetExpanderPort % 8));
    /* Level first, then direction, so the line is never driven to a value
     * nobody chose. The other ports' output bits are don't-cares: they are
     * inputs. */
    const uint8_t at = BOARD.touch.resetExpanderAddress;
    aw9523Write(at, AW9523_OUTPUT_P0 + bank, released ? 0xFF : static_cast<uint8_t>(~bit));
    if (!released) {
        aw9523Write(at, AW9523_MODE_P0, 0xFF);
        aw9523Write(at, AW9523_MODE_P1, 0xFF);
        aw9523Write(at, AW9523_CONFIG_P0 + bank, static_cast<uint8_t>(~bit));
        aw9523Write(at, AW9523_CONFIG_P0 + (1 - bank), 0xFF);
    }
}

bool setupKeyboardHalf(uint8_t address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) {
        return false;
    }
    /* Rows held LOW between scans, so one read of the columns says whether
     * any key on this half is down -- the scan only walks the rows when one
     * is. Level before direction, so a row is never driven to a value nobody
     * chose; the LED bits high, which is off on this common-anode part. */
    aw9523Write(address, AW9523_MODE_P0, 0xFF);
    aw9523Write(address, AW9523_MODE_P1, 0xFF);
    aw9523Write(address, AW9523_OUTPUT_P0 + 1, 0xE0);
    aw9523Write(address, AW9523_CONFIG_P0, 0xFF);
    aw9523Write(address, AW9523_CONFIG_P0 + 1, 0x00);
    return true;
}

void setupLed() {
    const uint8_t at = BOARD.rgb.expander;
    for (uint8_t bank = 0; bank < 2; ++bank) {
        /* Level first, then direction, as everywhere here: the line is off
         * before it is ever driven. */
        aw9523Update(at, AW9523_OUTPUT_P0 + bank, ledMask(bank),
                     ledLevels(bank, false, false, false));
        aw9523Update(at, AW9523_CONFIG_P0 + bank, ledMask(bank), 0x00);
    }
}

void driveLed(bool red, bool green, bool blue) {
    for (uint8_t bank = 0; bank < 2; ++bank) {
        aw9523Update(BOARD.rgb.expander, AW9523_OUTPUT_P0 + bank, ledMask(bank),
                     ledLevels(bank, red, green, blue));
    }
}

void allInputs(uint8_t address) {
    aw9523Write(address, AW9523_MODE_P0, 0xFF);
    aw9523Write(address, AW9523_MODE_P1, 0xFF);
    aw9523Write(address, AW9523_CONFIG_P0, 0xFF);
    aw9523Write(address, AW9523_CONFIG_P0 + 1, 0xFF);
}

}  // namespace BoardExpander

#endif  /* GUME_TOUCH_CAPACITIVE */
