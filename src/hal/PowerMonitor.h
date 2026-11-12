// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <Arduino.h>

#include "PowerLedger.h"
#include "PowerProbe.h"

class Board;

/* The battery drain diagnostic: "where did the charge go?" on any platform.
 *
 * A task reads the platform's PowerProbe every SAMPLE_MS and books the time to
 * the state the runtime last published, in a PowerLedger. System Info's Batt
 * tab, the console's `power` command and the `[power]` log lines all read that
 * one ledger, so the same answer is available with the device in hand or from a
 * log after the fact.
 *
 * It runs from boot rather than behind a switch, because a report of a flat
 * battery arrives after the fact and a diagnostic that had to be turned on
 * first would have nothing to say. It costs a few hundred bytes of RAM and a
 * read every ten seconds. Nothing is stored in NVS and nothing is transmitted:
 * the ledger is RAM, it starts over on a reboot, and the log lines go to serial
 * (or, on a handheld, to the log file on its own card). */
namespace PowerMonitor {

constexpr uint32_t SAMPLE_MS = 10000;
constexpr uint32_t LOG_LINE_EVERY = 6;      // samples: a minute
constexpr uint32_t LOG_TABLE_EVERY = 60;    // samples: ten minutes

/* What only the runtime knows. Published from the loop, read by the task. */
struct Activity {
    PowerLedger::Activity activity = PowerLedger::Activity::Awake;
    uint8_t lampPercent = 0;
    bool wifi = false;
    bool bluetooth = false;
};

struct Report {
    const char* source = "";
    PowerReading now;
    PowerLedger::State state;
    PowerLedger ledger;
};

void begin(Board& board);
void publish(const Activity& activity);
void report(Report& out);
/* Start the measurement over, for a clean before and after. */
void reset();
/* The console's reply: one line, `ok key="value" ...`. */
void printReply(Print& out);

}   // namespace PowerMonitor
