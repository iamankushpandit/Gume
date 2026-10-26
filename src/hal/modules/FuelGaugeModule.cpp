// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Modules.h"

/* The battery module's fuel gauge: Attaky's Power: Standard Cell reports the
 * cell through a MAX17048 on the touch bus, at BOARD.battery.fuelGaugeAddress.
 *
 * A live driver, unlike the others: the power module can come off while the
 * console runs from USB, so it has no boot probe, and the battery task asks it
 * every BATTERY_SAMPLE_MS whatever it said last time. Its answer is its
 * presence -- the module table marks it present or absent from each reading --
 * and a gauge that does not answer is no reading, never an empty cell. See
 * Board::sampleModuleBattery(), which turns a reading into the badge.
 *
 * Called from the battery task on core 0 while the loop reads touch on the
 * same bus. Arduino-ESP32 2.0's TwoWire holds its own mutex from
 * beginTransmission() through the repeated-start requestFrom(), so the two
 * cannot interleave; do not add a second lock. */

#if GUME_HAS_ATTAKY_MODULES
#include <Arduino.h>
#include <Wire.h>

namespace {

/* ---- MAX17048 fuel gauge ---------------------------------------------
 * Register map from the MAX17048/MAX17049 datasheet. Every register is a
 * 16-bit word, most significant byte first.
 *
 *   VCELL 0x02  cell voltage, 78.125 uV per LSB (12 bits of result over 16,
 *               which is the datasheet's 1.25 mV per count)
 *   SOC   0x04  state of charge: the high byte is whole percent, the low byte
 *               1/256 percent
 *
 * SOC is the gauge's ModelGauge estimate, already filtered over minutes, so it
 * goes to the percentage as it is: no LiPo curve and no gauge filter, which
 * exist to make a percentage out of a noisy voltage and would only be guessing
 * at what the chip has already worked out.
 *
 * SOC is allowed to read above 100 shortly after a full charge, while the
 * model settles; it is clamped here. */
constexpr uint8_t MAX17048_REG_VCELL = 0x02;
constexpr uint8_t MAX17048_REG_SOC = 0x04;
constexpr float MAX17048_VOLTS_PER_LSB = 78.125e-6f;

/* Whether the gauge answered last time: -1 not yet asked, 0 no, 1 yes. It
 * exists so the log says when that changes. Battery task only. */
int8_t s_answered = -1;

/* One register read: the register pointer, a repeated start, two bytes. A
 * gauge that is not there NACKs its address and requestFrom() returns 0 at
 * once. */
bool readWord(uint8_t reg, uint16_t& out) {
    const int address = BOARD.battery.fuelGaugeAddress;
    Wire.beginTransmission(static_cast<uint8_t>(address));
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(address, 2) != 2) {
        return false;
    }
    const uint8_t hi = static_cast<uint8_t>(Wire.read());
    const uint8_t lo = static_cast<uint8_t>(Wire.read());
    out = static_cast<uint16_t>((hi << 8) | lo);
    return true;
}

bool readBattery(float& volts, uint8_t& percent) {
    if (BOARD.battery.fuelGaugeAddress == 0) {
        return false;
    }
    uint16_t vcell = 0;
    uint16_t soc = 0;
    const bool answered = readWord(MAX17048_REG_VCELL, vcell) && readWord(MAX17048_REG_SOC, soc);

    const int8_t answeredNow = answered ? 1 : 0;
    if (answeredNow != s_answered) {
        s_answered = answeredNow;
        Serial.printf("[battery] fuel gauge at 0x%02X %s\n",
                      static_cast<unsigned>(BOARD.battery.fuelGaugeAddress),
                      answered ? "answering" : "not answering");
    }
    if (!answered) {
        return false;
    }
    volts = static_cast<float>(vcell) * MAX17048_VOLTS_PER_LSB;
    /* Whole percent, rounded on the 1/256 byte rather than truncated. */
    const uint32_t pct = (static_cast<uint32_t>(soc) + 128u) >> 8;
    percent = static_cast<uint8_t>(pct > 100 ? 100 : pct);
    return true;
}

constexpr Modules::Driver DRIVER = {
    /* name           */ "Power module",
    /* tag            */ "fuel-gauge",
    /* order          */ 30,
    /* provides       */ static_cast<uint8_t>(ModuleCapability::Battery),
    /* slot           */ nullptr,
    /* byElimination  */ false,
    /* live           */ true,
    /* probe          */ nullptr,
    /* heldNavKeys    */ nullptr,
    /* typedChar      */ nullptr,
    /* readBattery    */ readBattery,
};
const Modules::Registration REGISTRATION(DRIVER);

}  // namespace

#endif  /* GUME_HAS_ATTAKY_MODULES */
