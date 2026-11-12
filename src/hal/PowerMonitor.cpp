// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "PowerMonitor.h"

#include "Board.h"
#include "Watchdog.h"

namespace PowerMonitor {
namespace {

Board* board_ = nullptr;
TaskHandle_t task_ = nullptr;
portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

/* All four are touched only inside mux_. Static rather than on the task's stack
 * because they are the whole of what this module costs in RAM. */
Activity activity_;
PowerReading now_;
PowerLedger::State state_;
PowerLedger ledger_;

/* The firmware's own answers, for whatever the probe left open. This is what
 * lets a platform with an empty probe still get a percentage-per-hour analysis. */
void fillFromFirmware(PowerReading& r) {
    if (r.percent < 0) r.percent = board_->getBatteryPercent();
    if (r.cellMillivolts == PowerReading::UNKNOWN && BOARD.hasBatterySense()) {
        r.cellMillivolts = static_cast<int32_t>(board_->getBatteryVoltage() * 1000.0f);
    }
    if (r.cpuPercent < 0) {
        const Watchdog::Stats stats = Watchdog::stats();
        if (stats.lastFrameMs > 0) {
            r.cpuPercent = static_cast<int16_t>(stats.lastWorkMs * 100 / stats.lastFrameMs);
        }
    }
}

PowerLedger::State stateFrom(const Activity& a, const PowerReading& r) {
    PowerLedger::State s;
    s.activity = a.activity;
    s.lamp = PowerLedger::lampBand(r.lampPercent >= 0 ? r.lampPercent : a.lampPercent);
    s.wifi = a.wifi;
    s.bluetooth = a.bluetooth;
    s.light = r.lightOn;
    return s;
}

void logLine(const Report& rep) {
    char label[PowerLedger::LABEL_SIZE];
    PowerLedger::label(rep.state, label, sizeof(label));
    Serial.printf("[power] t=%lu pct=%d mv=%ld ma=%ld charger=%d cpu=%d mhz=%ld state=\"%s\"",
                  static_cast<unsigned long>(millis() / 1000), rep.now.percent,
                  static_cast<long>(rep.now.cellMillivolts == PowerReading::UNKNOWN ? -1 : rep.now.cellMillivolts),
                  static_cast<long>(rep.now.milliamps == PowerReading::UNKNOWN ? -1 : rep.now.milliamps),
                  rep.now.onCharger, rep.now.cpuPercent,
                  static_cast<long>(rep.now.cpuMhz == PowerReading::UNKNOWN ? -1 : rep.now.cpuMhz), label);
    for (uint8_t i = 0; i < rep.now.detailCount; ++i) {
        Serial.printf(" %s=\"%s\"", rep.now.details[i].label, rep.now.details[i].value);
    }
    Serial.println();
}

void logTable(const PowerLedger& ledger) {
    uint8_t order[PowerLedger::MAX_STATES];
    ledger.ranked(order);
    Serial.printf("[power] on battery %lus, on charger %lus\n",
                  static_cast<unsigned long>(ledger.batterySeconds()),
                  static_cast<unsigned long>(ledger.chargerSeconds()));
    for (uint8_t i = 0; i < ledger.count(); ++i) {
        const PowerLedger::Row& row = ledger.row(order[i]);
        char label[PowerLedger::LABEL_SIZE];
        PowerLedger::label(row.state, label, sizeof(label));
        Serial.printf("[power] %s: share=%u%% %lus ma=%ld lost=%ld%% cpu=%d\n", label,
                      ledger.sharePercent(order[i]), static_cast<unsigned long>(row.seconds),
                      static_cast<long>(row.milliamps()), static_cast<long>(row.percentDrop),
                      row.cpuPercent());
    }
}

void taskBody(void*) {
    PowerProbe& probe = platformPowerProbe();
    uint32_t lastMs = millis();
    uint32_t samples = 0;
    /* Static: a Report is several hundred bytes, and this task's stack is sized
     * for what it calls rather than for what it copies. */
    static Report rep;
    for (;;) {
        PowerReading reading;
        probe.read(reading);
        fillFromFirmware(reading);

        const uint32_t nowMs = millis();
        const uint32_t seconds = (nowMs - lastMs) / 1000;
        lastMs += seconds * 1000;   // the remainder belongs to the next sample

        portENTER_CRITICAL(&mux_);
        const PowerLedger::State state = stateFrom(activity_, reading);
        now_ = reading;
        state_ = state;
        ledger_.add(state, reading, seconds);
        portEXIT_CRITICAL(&mux_);

        ++samples;
        if (samples % LOG_LINE_EVERY == 1 || samples % LOG_TABLE_EVERY == 0) report(rep);
        if (samples % LOG_LINE_EVERY == 1) logLine(rep);
        if (samples % LOG_TABLE_EVERY == 0) logTable(rep.ledger);
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}

}   // namespace

void begin(Board& board) {
    if (task_ != nullptr) return;
    board_ = &board;
    Serial.printf("[power] measuring by: %s\n", platformPowerProbe().source());
    xTaskCreatePinnedToCore(taskBody, "power", 4096, nullptr, 1, &task_, 0);
}

void publish(const Activity& activity) {
    portENTER_CRITICAL(&mux_);
    activity_ = activity;
    portEXIT_CRITICAL(&mux_);
}

void report(Report& out) {
    out.source = platformPowerProbe().source();
    portENTER_CRITICAL(&mux_);
    out.now = now_;
    out.state = state_;
    out.ledger = ledger_;
    portEXIT_CRITICAL(&mux_);
}

void reset() {
    portENTER_CRITICAL(&mux_);
    ledger_.reset();
    portEXIT_CRITICAL(&mux_);
    Serial.println("[power] measurement started over");
}

void printReply(Print& out) {
    static Report rep;   // the console runs on the loop, one command at a time
    report(rep);
    char label[PowerLedger::LABEL_SIZE];
    PowerLedger::label(rep.state, label, sizeof(label));
    out.printf("ok source=\"%s\" pct=\"%d\" charger=\"%d\" state=\"%s\" battery_s=\"%lu\" charger_s=\"%lu\"",
               rep.source, rep.now.percent, rep.now.onCharger, label,
               static_cast<unsigned long>(rep.ledger.batterySeconds()),
               static_cast<unsigned long>(rep.ledger.chargerSeconds()));
    if (rep.now.cellMillivolts != PowerReading::UNKNOWN) {
        out.printf(" mv=\"%ld\"", static_cast<long>(rep.now.cellMillivolts));
    }
    if (rep.now.milliamps != PowerReading::UNKNOWN) {
        out.printf(" ma=\"%ld\"", static_cast<long>(rep.now.milliamps));
    }
    uint8_t order[PowerLedger::MAX_STATES];
    rep.ledger.ranked(order);
    for (uint8_t i = 0; i < rep.ledger.count(); ++i) {
        const PowerLedger::Row& row = rep.ledger.row(order[i]);
        PowerLedger::label(row.state, label, sizeof(label));
        out.printf(" s%u=\"%s|%u%%|%lus|%ldmA|%ld%%\"", i, label, rep.ledger.sharePercent(order[i]),
                   static_cast<unsigned long>(row.seconds), static_cast<long>(row.milliamps()),
                   static_cast<long>(row.percentDrop));
    }
    for (uint8_t i = 0; i < rep.now.detailCount; ++i) {
        out.printf(" d%u=\"%s: %s\"", i, rep.now.details[i].label, rep.now.details[i].value);
    }
    out.println();
}

}   // namespace PowerMonitor
