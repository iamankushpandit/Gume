// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Modules.h"

/* The power module's gauge. Attaky make three power modules and they carry
 * three different chips, all on the touch bus:
 *
 *   Power: Standard Cell 1.0   MAX17048 at 0x36 -- a fuel gauge, which
 *                              reports its own state of charge
 *   Power: 18650 Cell 1.0      INA226 at 0x40, 0x44 or 0x45
 *   Power: Standard Cell 1.1   INA219 at 0x44, 0x40 or 0x45
 *
 * The INA parts are current/voltage monitors: they give the cell's voltage,
 * and the percentage is the LiPo curve's, as on a board with a divider (the
 * caller turns PERCENT_FROM_VOLTS into curvePercent()). The chain, the probe
 * for each part and the register maths are Attaky's own, from their Retro-Go
 * patch for this hardware (patches/0002-atk-input-keymap-and-battery.patch):
 * the catalog documents only the MAX17048 module. A bare address ack is not
 * enough -- the INA parts' address ranges overlap and their voltage maths
 * differs -- so each is identified by a register only it answers that way.
 *
 * A live driver, unlike the others: the power module can come off while the
 * console runs from USB, so it has no boot probe, and the battery task asks it
 * every BATTERY_SAMPLE_MS whatever it said last time. A gauge that stops
 * answering is forgotten and the chain is tried again on the next sample, so
 * a module swapped while running is found. Its answer is its presence -- the
 * module table marks it present or absent from each reading -- and a gauge
 * that does not answer is no reading, never an empty cell. See
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

/* ---- the three parts -------------------------------------------------
 * Every register is a 16-bit word, most significant byte first.
 *
 *   MAX17048  VERSION 0x08  must answer; Attaky reject 0x0000 and 0xFFFF
 *             VCELL   0x02  78.125 uV per LSB
 *             SOC     0x04  high byte whole percent, low byte 1/256 --
 *                           the gauge's own ModelGauge estimate, already
 *                           filtered over minutes, so it goes to the badge as
 *                           it is. It can read above 100 just after a full
 *                           charge and is clamped.
 *   INA226    DIE_ID  0xFF  reads 0x2260 (read-only, so a stranger at the
 *                           address simply fails the compare)
 *             BUS     0x02  1.25 mV per LSB, all 16 bits
 *   INA219    CONFIG  0x00  write 0x019F and read it back: bit 14 always
 *                           reads 0 on a real INA219, so 0x019F, not 0x419F
 *             BUS     0x02  4 mV per LSB in bits 15..3 (masking without the
 *                           shift reads eight times too high)
 *
 * A reading outside 1.0-5.5 V is not believed: one bad frame keeps the last
 * good value rather than flashing an empty badge. */
enum class Gauge : uint8_t { None, Max17048, Ina226, Ina219 };

constexpr uint8_t MAX17048_ADDRESS_DEFAULT = 0x36;
constexpr uint8_t MAX17048_REG_VCELL = 0x02;
constexpr uint8_t MAX17048_REG_SOC = 0x04;
constexpr uint8_t MAX17048_REG_VERSION = 0x08;
constexpr uint8_t INA_REG_CONFIG = 0x00;
constexpr uint8_t INA_REG_BUS = 0x02;
constexpr uint8_t INA226_REG_DIE_ID = 0xFF;
constexpr uint16_t INA226_DIE_ID = 0x2260;
constexpr uint16_t INA219_CONFIG = 0x019F;
constexpr uint8_t INA226_ADDRESSES[] = {0x40, 0x44, 0x45};
constexpr uint8_t INA219_ADDRESSES[] = {0x44, 0x40, 0x45};
/* Where the Standard Cell 1.1 reads FULL, measured rather than assumed. On a
 * Cyber Deck Plus with the module's full-charge light lit, on USB, the INA219
 * read 4.072-4.076 V with the shunt at a fraction of a milliamp -- charging
 * had stopped. A LiPo curve that tops out at 4.20 V showed that as 87%. The
 * charger stops short of 4.2 V, or the INA sits behind something that drops a
 * little; either way 4.07 V is this module's full, and the badge has to say
 * 100 there. The 18650 module's INA226 has not been measured and keeps the
 * curve's own top. */
constexpr uint16_t INA219_FULL_MV = 4070;
constexpr uint32_t PLAUSIBLE_MIN_MV = 1000;
constexpr uint32_t PLAUSIBLE_MAX_MV = 5500;

/* What answered, where. Battery task only. */
Gauge s_gauge = Gauge::None;
uint8_t s_address = 0;
/* Whether the last sample answered: -1 not yet asked, 0 no, 1 yes. It exists
 * so the log says when that changes, once. */
int8_t s_answered = -1;
/* The last believable reading, for a single bad frame. */
uint32_t s_lastMv = 0;
uint8_t s_lastPercent = 0;

bool readWord(uint8_t address, uint8_t reg, uint16_t& out) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(static_cast<int>(address), 2) != 2) {
        return false;
    }
    const uint8_t hi = static_cast<uint8_t>(Wire.read());
    const uint8_t lo = static_cast<uint8_t>(Wire.read());
    out = static_cast<uint16_t>((hi << 8) | lo);
    return true;
}

bool writeWord(uint8_t address, uint8_t reg, uint16_t value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(static_cast<uint8_t>(value >> 8));
    Wire.write(static_cast<uint8_t>(value & 0xFF));
    return Wire.endTransmission() == 0;
}

const char* nameOf(Gauge g) {
    switch (g) {
        case Gauge::Max17048: return "MAX17048";
        case Gauge::Ina226: return "INA226";
        case Gauge::Ina219: return "INA219";
        default: return "none";
    }
}

/* Find whichever part is fitted, in Attaky's order. */
void probeChain() {
    s_gauge = Gauge::None;
    s_address = 0;
    const uint8_t maxAddress = BOARD.battery.fuelGaugeAddress != 0
                                   ? BOARD.battery.fuelGaugeAddress
                                   : MAX17048_ADDRESS_DEFAULT;
    uint16_t word = 0;
    if (readWord(maxAddress, MAX17048_REG_VERSION, word) && word != 0x0000 && word != 0xFFFF) {
        s_gauge = Gauge::Max17048;
        s_address = maxAddress;
        return;
    }
    for (const uint8_t a : INA226_ADDRESSES) {
        if (readWord(a, INA226_REG_DIE_ID, word) && word == INA226_DIE_ID) {
            s_gauge = Gauge::Ina226;
            s_address = a;
            return;
        }
    }
    for (const uint8_t a : INA219_ADDRESSES) {
        if (writeWord(a, INA_REG_CONFIG, INA219_CONFIG) && readWord(a, INA_REG_CONFIG, word) &&
            word == INA219_CONFIG) {
            s_gauge = Gauge::Ina219;
            s_address = a;
            return;
        }
    }
}

/* One reading from the part found, in millivolts, with its percentage where
 * the part knows one (the MAX17048) and PERCENT_FROM_VOLTS where it does not. */
bool readFound(uint32_t& mv, uint8_t& percent) {
    uint16_t raw = 0;
    if (!readWord(s_address, s_gauge == Gauge::Max17048 ? MAX17048_REG_VCELL : INA_REG_BUS, raw)) {
        return false;
    }
    switch (s_gauge) {
        case Gauge::Max17048: mv = static_cast<uint32_t>(raw) * 5u / 64u; break;
        case Gauge::Ina226:   mv = static_cast<uint32_t>(raw) * 5u / 4u; break;
        case Gauge::Ina219:   mv = static_cast<uint32_t>((raw & 0xFFF8u) >> 3) * 4u; break;
        default: return false;
    }
    if (mv < PLAUSIBLE_MIN_MV || mv > PLAUSIBLE_MAX_MV) {
        return false;
    }
    if (s_gauge == Gauge::Max17048) {
        uint16_t soc = 0;
        if (!readWord(s_address, MAX17048_REG_SOC, soc)) {
            return false;
        }
        /* Whole percent, rounded on the 1/256 byte rather than truncated. */
        const uint32_t pct = (static_cast<uint32_t>(soc) + 128u) >> 8;
        percent = static_cast<uint8_t>(pct > 100 ? 100 : pct);
    } else {
        percent = Modules::PERCENT_FROM_VOLTS;
    }
    return true;
}

}   // namespace

float Modules::gaugeFullVolts() {
    return s_gauge == Gauge::Ina219 ? INA219_FULL_MV / 1000.0f : 0.0f;
}

namespace {

bool readBattery(float& volts, uint8_t& percent) {
    if (s_gauge == Gauge::None) {
        probeChain();
    }
    uint32_t mv = 0;
    bool answered = s_gauge != Gauge::None && readFound(mv, percent);
    if (answered) {
        s_lastMv = mv;
        s_lastPercent = percent;
    } else if (s_gauge != Gauge::None && s_lastMv != 0 && s_answered == 1) {
        /* One bad frame from a part that was answering: keep the last good
         * reading, and forget the part so the chain is tried again next time. */
        s_gauge = Gauge::None;
        mv = s_lastMv;
        percent = s_lastPercent;
        answered = true;
    } else {
        s_gauge = Gauge::None;
    }

    const int8_t answeredNow = answered ? 1 : 0;
    if (answeredNow != s_answered) {
        s_answered = answeredNow;
        if (answered) {
            Serial.printf("[battery] gauge %s at 0x%02X, %lu mV\n", nameOf(s_gauge),
                          static_cast<unsigned>(s_address), static_cast<unsigned long>(mv));
        } else {
            Serial.println("[battery] no gauge answering (MAX17048, INA226, INA219 tried)");
        }
    }
    if (!answered) {
        return false;
    }
    volts = static_cast<float>(mv) / 1000.0f;
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
    /* heldControls   */ nullptr,
    /* typedChar      */ nullptr,
    /* readBattery    */ readBattery,
};
const Modules::Registration REGISTRATION(DRIVER);

}  // namespace

#endif  /* GUME_HAS_ATTAKY_MODULES */
