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
 * expanders on the touch bus, one a half. GamepadProfile in
 * include/BoardProfile.h is the whole module, as a layout string a half.
 *
 * It adds what it holds to the board's own keys: this driver reports every
 * key held now, directions PHYSICAL, and Board::pollControls() ORs that with
 * the Core's own keys before turning the set for the rotation. So the gamepad
 * and the Core's D-pad are one set of keys to everything above the HAL, and
 * turning the screen turns both the same way. What each key is FOR is not
 * decided here -- hal/ControlJobs.h does that, for every console alike.
 *
 * Read in the loop, beside the keyboard, and as cheaply: one input register
 * per half per frame -- every key is on port 0 of its half -- and nothing
 * else, held or idle. The probe leaves every port an input, which is the
 * part's power-on state but is written rather than trusted, since a previous
 * firmware may have left the LED ports driven; an input is also what leaves
 * each half's RGB LED off. A failed read is not a key released: the last
 * state stands, so a bus glitch cannot invent a press. */

#if GUME_HAS_ATTAKY_MODULES
#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint8_t TCA9555_INPUT_P0 = 0x00;
constexpr uint8_t TCA9555_CONFIG_P0 = 0x06;   // 1 = input

/* Each half's port 0 as last read, kept for a failed read. Loop task only. */
uint8_t s_level[2] = {0xFF, 0xFF};

bool setupHalf(uint8_t address) {
    Wire.beginTransmission(address);
    Wire.write(TCA9555_CONFIG_P0);
    Wire.write(0xFF);
    Wire.write(0xFF);   // CONFIG_P1: the register pointer advances in pairs
    return Wire.endTransmission() == 0;
}

/* Port 0 of one half; false on a bus error. Active low. */
bool readPort0(uint8_t address, uint8_t& level) {
    Wire.beginTransmission(address);
    Wire.write(TCA9555_INPUT_P0);
    if (Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(static_cast<int>(address), 1) != 1) {
        return false;
    }
    level = static_cast<uint8_t>(Wire.read());
    return true;
}

Modules::Probe probe() {
    const auto& g = BOARD.gamepad;
    if (g.padLeftExpander == 0) {
        return Modules::Probe::Absent;
    }
    /* Both halves or neither: a D-pad with no face buttons, or the reverse,
     * is a module that is not seated properly, and half of it would be a
     * puzzle. */
    const bool attached = setupHalf(g.padLeftExpander) && setupHalf(g.padRightExpander);
    return attached ? Modules::Probe::Present : Modules::Probe::Absent;
}

void heldControls(HeldControls& into) {
    const auto& g = BOARD.gamepad;
    const uint8_t address[2] = {g.padLeftExpander, g.padRightExpander};
    const char* const layout[2] = {g.padLeftLayout, g.padRightLayout};
    for (uint8_t half = 0; half < 2; ++half) {
        uint8_t level = 0;
        if (readPort0(address[half], level)) {
            s_level[half] = level;
        }
        if (layout[half] == nullptr) continue;
        for (uint8_t port = 0; port < 8 && layout[half][port] != '\0'; ++port) {
            if (!((s_level[half] >> port) & 1)) {   // active low
                holdLayoutKey(layout[half][port], into);
            }
        }
    }
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
    /* heldControls   */ heldControls,
    /* typedChar      */ nullptr,
    /* readBattery    */ nullptr,
};
const Modules::Registration REGISTRATION(DRIVER);

}  // namespace

#endif  /* GUME_HAS_ATTAKY_MODULES */
