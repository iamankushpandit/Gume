// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Board.h"

/* Board's side of the module table (hal/modules/Modules.h): the two places a
 * module's answer becomes something the rest of the firmware already
 * understands -- a battery snapshot, a typed character. The drivers know their
 * chips; this file knows Board's state; neither knows the other's.
 *
 * Built only where GUME_HAS_ATTAKY_MODULES is set, and that is the whole of
 * how every other board's image carries none of it. */

#if GUME_HAS_ATTAKY_MODULES
#include "modules/Modules.h"

/* A battery module that does not answer is NO READING, not an empty cell.
 *
 * The module is detachable, so "it did not answer" is an ordinary state rather
 * than a fault, and the two wrong answers to it are both easy to write: a
 * zeroed SOC is 0%, which is the critical-battery banner on a console that may
 * be on USB with no module at all; and keeping the last good reading is a
 * badge that goes on claiming a charge nobody is measuring. So it publishes
 * what a board with no sense line publishes -- a zeroed sample and -1 -- and
 * every reader already treats that as "nothing to show": no badge, no low or
 * critical warning. When the module comes back, the next sample shows it, with
 * no deadband to climb through.
 *
 * The percentage is the module's own (a fuel gauge's state of charge), so no
 * LiPo curve and no gauge filter; the deadband stays, because the badge must
 * not twitch whoever computed the number. Battery task only. */
void Board::sampleModuleBattery() {
    float volts = 0.0f;
    uint8_t percent = 0;
    const bool answered = Modules::readBattery(volts, percent);

    BatteryTelemetry sample;
    if (!answered) {
        displayPct_ = -1;
    } else {
        sample.batteryVoltage = volts;
        settleDisplayPercent(static_cast<int8_t>(percent));
    }

    portENTER_CRITICAL(&batteryMux_);
    batteryPublished_.sample = sample;
    batteryPublished_.pct = displayPct_;
    portEXIT_CRITICAL(&batteryMux_);
}

#if GUME_HAS_KEYBOARD
char Board::pollTypedChar() {
    return Modules::typedChar();
}
#endif

#endif  /* GUME_HAS_ATTAKY_MODULES */
