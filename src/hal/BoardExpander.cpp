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
 * Only the reset port is made an output. Every other port -- the Core's
 * buttons, its LED cathodes, the lines passed through to the modules -- is an
 * input, which is high-impedance and therefore the safest thing this
 * firmware can leave them, since it reads none of them. */
constexpr uint8_t AW9523_OUTPUT_P0 = 0x02;
constexpr uint8_t AW9523_CONFIG_P0 = 0x04;   // 1 = input, 0 = output
constexpr uint8_t AW9523_MODE_P0   = 0x12;   // 1 = GPIO, 0 = LED current
constexpr uint8_t AW9523_MODE_P1   = 0x13;

void aw9523Write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(BOARD.touch.resetExpanderAddress);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

}  // namespace

void driveTouchReset(bool released) {
    const uint8_t bank = BOARD.touch.resetExpanderPort / 8;   // 0 = P0, 1 = P1
    const uint8_t bit = static_cast<uint8_t>(1u << (BOARD.touch.resetExpanderPort % 8));
    /* Level first, then direction, so the line is never driven to a value
     * nobody chose. The other ports' output bits are don't-cares: they are
     * inputs. */
    aw9523Write(AW9523_OUTPUT_P0 + bank, released ? 0xFF : static_cast<uint8_t>(~bit));
    if (!released) {
        aw9523Write(AW9523_MODE_P0, 0xFF);
        aw9523Write(AW9523_MODE_P1, 0xFF);
        aw9523Write(AW9523_CONFIG_P0 + bank, static_cast<uint8_t>(~bit));
        aw9523Write(AW9523_CONFIG_P0 + (1 - bank), 0xFF);
    }
}

}  // namespace BoardExpander

#endif  /* GUME_TOUCH_CAPACITIVE */
