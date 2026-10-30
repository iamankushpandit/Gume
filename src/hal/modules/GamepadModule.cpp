// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Modules.h"

/* A clip-on gamepad: Attaky's Control: Gamepad (Bottom), two TCA9555
 * expanders on the touch bus, one a half. See GamepadProfile in
 * include/BoardProfile.h for the wiring.
 *
 * Its D-pad, A and B join the navigation-key stream: this driver reports the
 * keys held now, in PHYSICAL directions, and Board::pollNavKey() ORs that with
 * the Core's own D-pad before it finds the edges, repeats a held direction
 * and turns it for the rotation. So the gamepad and the Core's D-pad are one
 * set of keys to everything above the HAL, and turning the screen turns both
 * the same way. A is Enter, B is Back.
 *
 * Read in the loop, beside the keyboard, and as cheaply: one input register
 * per half per frame -- every key read is on port 0 of its half -- and
 * nothing else, held or idle. The probe leaves every port an input, which is
 * the part's power-on state but is written rather than trusted, since a
 * previous firmware may have left the LED ports driven; an input is also what
 * leaves each half's RGB LED off. A failed read is not a key released: the
 * last state stands, so a bus glitch cannot invent a press. */

#if GUME_HAS_ATTAKY_MODULES
#include <Arduino.h>
#include <Wire.h>

#include "hal/NavKeys.h"

namespace {

constexpr uint8_t TCA9555_INPUT_P0 = 0x00;    // P1 is the register after it
constexpr uint8_t TCA9555_CONFIG_P0 = 0x06;   // 1 = input

/* What was held last frame, kept for a failed read. Loop task only. */
uint8_t s_held = 0;

uint8_t bitOf(NavKey key) {
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(key));
}

bool setupHalf(uint8_t address) {
    Wire.beginTransmission(address);
    Wire.write(TCA9555_CONFIG_P0);
    Wire.write(0xFF);
    Wire.write(0xFF);   // CONFIG_P1: the register pointer advances in pairs
    return Wire.endTransmission() == 0;
}

/* Both banks' input registers of one half, P0 in the low byte. Only the
 * banks some key is on are read. */
bool readHalf(uint8_t address, uint8_t banks, uint16_t& levels) {
    levels = 0xFFFF;   // released, active low
    for (uint8_t b = 0; b < 2; ++b) {
        if (!(banks & (1u << b))) continue;
        Wire.beginTransmission(address);
        Wire.write(static_cast<uint8_t>(TCA9555_INPUT_P0 + b));
        if (Wire.endTransmission(false) != 0 ||
            Wire.requestFrom(static_cast<int>(address), 1) != 1) {
            return false;
        }
        const uint8_t value = static_cast<uint8_t>(Wire.read());
        levels = static_cast<uint16_t>((levels & ~(0xFFu << (8 * b))) | (value << (8 * b)));
    }
    return true;
}

uint8_t bankOf(int8_t port) {
    return port == PIN_NONE ? 0 : static_cast<uint8_t>(1u << (port / 8));
}

bool down(uint16_t levels, int8_t port) {
    return port != PIN_NONE && !((levels >> port) & 1);   // active low
}

Modules::Probe probe() {
    const auto& g = BOARD.gamepad;
    if (g.padLeftExpander == 0) {
        return Modules::Probe::Absent;
    }
    /* Both halves or neither: a D-pad with no Enter, or the reverse, is a
     * module that is not seated properly, and half of it would be a puzzle. */
    const bool attached = setupHalf(g.padLeftExpander) && setupHalf(g.padRightExpander);
    return attached ? Modules::Probe::Present : Modules::Probe::Absent;
}

uint8_t heldNavKeys() {
    const auto& g = BOARD.gamepad;
    uint16_t left = 0;
    uint16_t right = 0;
    if (!readHalf(g.padLeftExpander,
                  bankOf(g.padUp) | bankOf(g.padDown) | bankOf(g.padLeft) | bankOf(g.padRight),
                  left) ||
        !readHalf(g.padRightExpander, bankOf(g.padA) | bankOf(g.padB), right)) {
        return s_held;
    }
    uint8_t held = 0;
    if (down(left, g.padUp)) held |= bitOf(NavKey::Up);
    if (down(left, g.padDown)) held |= bitOf(NavKey::Down);
    if (down(left, g.padLeft)) held |= bitOf(NavKey::Left);
    if (down(left, g.padRight)) held |= bitOf(NavKey::Right);
    if (down(right, g.padA)) held |= bitOf(NavKey::Enter);
    if (down(right, g.padB)) held |= bitOf(NavKey::Back);
    s_held = held;
    return held;
}

constexpr Modules::Driver DRIVER = {
    /* name           */ "Gamepad",
    /* tag            */ "gamepad",
    /* order          */ 20,
    /* provides       */ static_cast<uint8_t>(ModuleCapability::NavKeys),
    /* slot           */ nullptr,
    /* byElimination  */ false,
    /* live           */ false,
    /* probe          */ probe,
    /* heldNavKeys    */ heldNavKeys,
    /* typedChar      */ nullptr,
    /* readBattery    */ nullptr,
};
const Modules::Registration REGISTRATION(DRIVER);

}  // namespace

#endif  /* GUME_HAS_ATTAKY_MODULES */
