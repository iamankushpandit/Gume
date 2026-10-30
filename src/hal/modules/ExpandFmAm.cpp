// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Modules.h"

/* Attaky's Expand: FM/AM radio module -- one of three Expand modules on the
 * "expand" slot. See ExpandMediaIo.cpp for why the slot is decided before the
 * SD slot and the speaker come up.
 *
 * Asked first, because it is the cheapest to ask and drives nothing any other
 * module owns: its TEF6687 tuner answers at BOARD.expandProbe.fmRadioAddress
 * on a bus of its own (fmSda/fmScl, IO42/IO41), which no other module wires.
 * One address write, then the bus is torn down and its pins left as inputs.
 *
 * A bus that would not start is not a negative answer -- it is Unknown, and
 * the table then decides the whole slot unknown, which leaves the SD slot and
 * the speaker down. That is Attaky's rule, and the safe one.
 *
 * Provides nothing the console uses; the tuner is not driven. Finding it is
 * what keeps the SD slot and the speaker down, because IO5-8 carry a tuner
 * GPIO on this module and IO17/IO18/IO21 its own I2S. */

#if GUME_HAS_ATTAKY_MODULES
#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint32_t FM_PROBE_HZ = 100000;

Modules::Probe probe() {
    const auto& p = BOARD.expandProbe;
    if (p.fmRadioAddress == 0) {
        return Modules::Probe::Absent;
    }
    if (!Wire1.begin(p.fmSda, p.fmScl, FM_PROBE_HZ)) {
        return Modules::Probe::Unknown;
    }
    Wire1.beginTransmission(p.fmRadioAddress);
    const bool found = Wire1.endTransmission() == 0;
    Wire1.end();
    /* Quiet again: the bus belongs to the FM module, and this firmware does
     * not use it. */
    pinMode(p.fmSda, INPUT);
    pinMode(p.fmScl, INPUT);
    return found ? Modules::Probe::Present : Modules::Probe::Absent;
}

constexpr Modules::Driver DRIVER = {
    /* name           */ "FM/AM radio",
    /* tag            */ "FM-AM",
    /* order          */ 40,
    /* provides       */ static_cast<uint8_t>(ModuleCapability::None),
    /* slot           */ "expand",
    /* byElimination  */ false,
    /* live           */ false,
    /* probe          */ probe,
    /* heldNavKeys    */ nullptr,
    /* typedChar      */ nullptr,
    /* readBattery    */ nullptr,
};
const Modules::Registration REGISTRATION(DRIVER);

}  // namespace

#endif  /* GUME_HAS_ATTAKY_MODULES */
