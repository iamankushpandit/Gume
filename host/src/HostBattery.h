// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <cstdint>

/* The handheld's battery, as the firmware's battery-sense line.
 *
 * The host board profile (when built with GUME_HOST_BATTERY) declares a
 * virtual ADC pin, and analogRead() on it answers with a voltage over the
 * profile's divider ratio. BoardPower.cpp then does exactly what it does on a
 * board: averages, low-passes, maps the voltage onto its LiPo curve, holds
 * the deadband, and drives the badge and the 15% / 5% warnings.
 *
 * The voltage is not the cell's. A handheld has a fuel gauge -- the AXP2202
 * counts the charge in and out -- and Linux reports its percentage as
 * /sys/class/power_supply/<battery>/capacity. The cell voltage is a poor
 * guess beside it, since it moves with the load and with charging. So the
 * answer is the voltage the firmware's own curve maps back to the gauge's
 * percentage, and the badge shows what the gauge says. Without a gauge it is
 * voltage_now, as before. */
namespace HostBattery {

/* Millivolts for the firmware's curve, or 0 when no battery is reported. */
uint32_t cellMillivolts();

/* What the light ring's gauge asks for. Read from sysfs, so not for a render
 * path. `percent` is -1 when there is no fuel gauge. `plugged` is the charger
 * supply's own `online`, not the battery's status: the AXP2202 has been seen
 * saying Full at 4067 mV, which no charger was holding up. */
struct Reading {
    int percent;
    bool plugged;
    bool charging;
};
Reading read();

/* The cell's voltage and the current leaving it, for the battery diagnostic.
 * Either is INT32_MIN (PowerReading::UNKNOWN) when the PMIC does not report it.
 * Current is positive out of the cell; the sign comes from the battery's status,
 * because drivers disagree about what sign current_now carries. */
struct Electrical {
    int32_t millivolts;
    int32_t milliamps;
};
Electrical electrical();

}   // namespace HostBattery
