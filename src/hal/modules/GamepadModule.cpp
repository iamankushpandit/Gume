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
 * each half's RGB LED off, if it is wired as documented -- the probe logs
 * what those lines read, so that can be seen (logLedLines()). A failed read
 * is not a key released: the last state stands, so a bus glitch cannot
 * invent a press. */

#if GUME_HAS_ATTAKY_MODULES
#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint8_t TCA9555_INPUT_P0 = 0x00;
constexpr uint8_t TCA9555_INPUT_P1 = 0x01;
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

/* One input register of one half; false on a bus error. */
bool readInput(uint8_t address, uint8_t reg, uint8_t& level) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(static_cast<int>(address), 1) != 1) {
        return false;
    }
    level = static_cast<uint8_t>(Wire.read());
    return true;
}

/* Port 0 of one half; false on a bus error. Active low. */
bool readPort0(uint8_t address, uint8_t& level) {
    return readInput(address, TCA9555_INPUT_P0, level);
}

/* What each half's LED lines read with nothing driving them, logged once.
 *
 * Attaky document each half's RGB LED on P1_5 (blue), P1_6 (green) and P1_7
 * (red) as active low: anode to the supply, cathode to the port. Wired that
 * way a port left an input sits at the supply through the LED and the part's
 * own pull-up, so all three bits read HIGH (the top three bits of P1, 0xE0)
 * and no current flows: the LED is off. A bit that reads LOW says that
 * channel is wired the other way round -- anode to the port, cathode to
 * ground -- and is then glowing dimly through the pull-up, which no input
 * setting can put out; only driving the port low would.
 *
 * The test is one-sided. LOW is conclusive. HIGH is what the documented
 * wiring gives, but a green or blue channel wired the other way could also
 * read HIGH, its forward voltage at a few microamps being close to the
 * part's input threshold; so all-HIGH with a light still showing wants a
 * meter on the LED, not a conclusion from this line. Nothing here drives
 * those ports: this is the measurement that says whether anything should.
 * A half that did not answer logs 0x00 with "?" after it. */
void logLedLines(uint8_t left, uint8_t right) {
    uint8_t l = 0;
    uint8_t r = 0;
    const bool lOk = readInput(left, TCA9555_INPUT_P1, l);
    const bool rOk = readInput(right, TCA9555_INPUT_P1, r);
    Serial.printf("[gamepad] leds as inputs: left P1=0x%02X%s right P1=0x%02X%s\n",
                  l, lOk ? "" : "?", r, rOk ? "" : "?");
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
    if (attached) {
        logLedLines(g.padLeftExpander, g.padRightExpander);
    }
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
    /* panelSleep     */ nullptr,
};
const Modules::Registration REGISTRATION(DRIVER);

}  // namespace

#endif  /* GUME_HAS_ATTAKY_MODULES */
