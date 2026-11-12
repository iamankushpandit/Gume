// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* System Info's Batt tab: the drain diagnostic, read with the device in hand.
 *
 * In its own file because SystemInfoApp.cpp is already past the size the
 * modularity rule allows. Everything here reads PowerMonitor's one report. */

#include "SystemInfoApp.h"

#include "hal/PowerMonitor.h"

namespace {

void formatDuration(uint32_t seconds, char* out, size_t size) {
    snprintf(out, size, "%lu:%02lu h", static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>((seconds % 3600) / 60));
}

/* "612 mA  9.1%/h  1:32 h", leaving out what this platform cannot say yet. */
void formatCost(const PowerLedger::Row& row, char* out, size_t size) {
    size_t used = 0;
    if (row.milliamps() >= 0) {
        used += snprintf(out + used, size - used, "%ld mA  ", static_cast<long>(row.milliamps()));
    }
    const int32_t rate = row.tenthsPercentPerHour();
    if (rate >= 0) {
        used += snprintf(out + used, size - used, "%ld.%ld%%/h  ", static_cast<long>(rate / 10),
                         static_cast<long>(rate % 10));
    }
    formatDuration(row.seconds, out + used, size - used);
}

}   // namespace

void SystemInfoApp::buildPowerRows(GameHost& host) {
    (void)host;
    /* Static: a report is several hundred bytes, and this is the one screen
     * reading it. */
    static PowerMonitor::Report rep;
    PowerMonitor::report(rep);
    const PowerReading& now = rep.now;
    const PowerLedger& ledger = rep.ledger;
    char text[RowList::VALUE_MAX];
    char label[PowerLedger::LABEL_SIZE];

    rows_.clear();
    rows_.addSection("Now");
    rows_.addRow("Measured by", rep.source);
    if (now.percent >= 0) {
        if (now.cellMillivolts != PowerReading::UNKNOWN) {
            snprintf(text, sizeof(text), "%d%%  %ld.%02ld V", now.percent,
                     static_cast<long>(now.cellMillivolts / 1000),
                     static_cast<long>((now.cellMillivolts % 1000) / 10));
        } else {
            snprintf(text, sizeof(text), "%d%%", now.percent);
        }
        rows_.addRow("Battery", text);
    } else {
        rows_.addRow("Battery", "Not measured");
    }
    if (now.milliamps != PowerReading::UNKNOWN) {
        snprintf(text, sizeof(text), "%ld mA", static_cast<long>(now.milliamps));
        rows_.addRow(now.milliamps < 0 ? "Charging at" : "Draw", now.milliamps < 0 ? text + 1 : text);
    } else {
        rows_.addRow("Draw", "No current sensor", Ui::muted());
    }
    rows_.addRow("On charger", now.onCharger == 1 ? "Yes" : now.onCharger == 0 ? "No" : "Cannot tell");
    if (now.cpuPercent >= 0 && now.cpuMhz != PowerReading::UNKNOWN) {
        snprintf(text, sizeof(text), "%d%%  %ld MHz", now.cpuPercent, static_cast<long>(now.cpuMhz));
        rows_.addRow("CPU", text);
    } else if (now.cpuPercent >= 0) {
        snprintf(text, sizeof(text), "%d%%", now.cpuPercent);
        rows_.addRow("CPU", text);
    }
    if (now.lampPercent >= 0) {
        snprintf(text, sizeof(text), "%d%%", now.lampPercent);
        rows_.addRow("Real lamp", text);
    }
    for (uint8_t i = 0; i < now.detailCount; ++i) {
        rows_.addRow(now.details[i].label, now.details[i].value);
    }

    rows_.addSection("Result");
    formatDuration(ledger.batterySeconds(), text, sizeof(text));
    rows_.addRow("On battery", text);
    if (ledger.chargerSeconds() > 0) {
        formatDuration(ledger.chargerSeconds(), text, sizeof(text));
        rows_.addRow("On charger", text);
    }
    uint8_t order[PowerLedger::MAX_STATES];
    ledger.ranked(order);
    if (ledger.count() > 0 && ledger.sharePercent(order[0]) > 0) {
        PowerLedger::label(ledger.row(order[0]).state, label, sizeof(label));
        snprintf(text, sizeof(text), "%s (%u%%)", label, ledger.sharePercent(order[0]));
        rows_.addRow("Most drain", text, Ui::warning());
    }
    const PowerLedger::Row* current = ledger.find(rep.state);
    const int32_t rate = current != nullptr ? current->tenthsPercentPerHour() : -1;
    if (rate > 0 && now.percent >= 0) {
        formatDuration(static_cast<uint32_t>(now.percent) * 36000UL / static_cast<uint32_t>(rate),
                       text, sizeof(text));
        rows_.addRow("Left like this", text);
    }

    rows_.addSection("States");
    if (ledger.count() == 0) {
        rows_.addRow("Measuring", "Nothing on battery yet", Ui::muted());
    }
    for (uint8_t i = 0; i < ledger.count(); ++i) {
        const PowerLedger::Row& row = ledger.row(order[i]);
        PowerLedger::label(row.state, label, sizeof(label));
        formatCost(row, text, sizeof(text));
        rows_.addRow(label, text);
    }
    rows_.addRow("Key", "W Wi-Fi  B Bluetooth  L lights", Ui::muted());
    rows_.addAction("Start over");
}
