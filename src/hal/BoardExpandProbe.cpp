// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Board.h"

/* Which Expand module is on the SD slot's lines, asked before the slot is.
 *
 * The Attaky Core takes one Expand module at a time, and three of them use
 * IO5-8: Media I/O for its microSD slot, LoRa/GPS for its radio's SPI, and
 * FM/AM for a tuner GPIO. This firmware is built for Media I/O, so on a Core
 * wearing either of the others, mountSd() would clock those lines as an SD
 * card into somebody else's chip -- and beginAudio() would clock the Media
 * I/O amplifier's I2S on IO17/IO18/IO21, which are the LoRa/GPS module's GPS
 * UART and radio interrupt and the FM/AM module's I2S. This runs once in
 * Board::begin(), before both, and if it finds either module the SD slot
 * stays down and the console stays silent for the boot.
 *
 * Passive first, in the order Attaky's shared/module-detection-and-safe-
 * boot.md gives, and nothing here drives a line any Expand module owns:
 *
 *  1. FM/AM: its TEF6687 answers at BOARD.expandProbe.fmRadioAddress on a bus
 *     of its own (IO42/IO41), which no other module wires. One address
 *     write, then the bus is torn down and its pins left as inputs.
 *  2. LoRa/GPS: its AT6558 sends NMEA sentences every second without being
 *     asked. The UART's receiver only is attached to gpsRx -- no TX pin, so
 *     the pin is an input throughout -- for GPS_LISTEN_MS, and one sentence
 *     whose checksum adds up is enough. A checksum, not a '$': on Media I/O
 *     that line is the amplifier's bit clock, idle and pulled, and a floating
 *     line can produce a '$' but not a whole sentence that sums.
 *
 * Media I/O has no identity a host can read, so it is found by elimination,
 * and finding nothing reads the same as no module at all -- both mean the
 * slot and the speaker come up exactly as they did before this existed. A
 * probe that could not run (the radio's bus would not start) is not a
 * negative answer: both stay down, which is Attaky's rule and the safe one.
 *
 * It costs GPS_LISTEN_MS of every boot on a board that has it, and nothing on
 * any other: hasExpandProbe() is a compile-time constant, and the whole body
 * is capacitive-only because that is what links Wire (see src/hal/CLAUDE.md
 * on `if constexpr`). */

#if GUME_TOUCH_CAPACITIVE
#include <Wire.h>

#include "Watchdog.h"

namespace {

/* Attaky's figure: the GPS reports once a second, so this is a second and a
 * half of sentences, and a whole one always lands inside it. */
constexpr uint32_t GPS_LISTEN_MS = 1500;
constexpr uint32_t FM_PROBE_HZ = 100000;
constexpr size_t NMEA_MAX = 82;   // NMEA 0183's longest sentence, '$' to LF

/* Fed one byte at a time; true on the end of a sentence whose talker is
 * letters and whose checksum matches. */
class NmeaSpotter {
public:
    bool feed(char c) {
        if (c == '$') {
            length_ = 0;
            sum_ = 0;
            inBody_ = true;
            hexLeft_ = 0;
            return false;
        }
        if (length_ >= NMEA_MAX) {
            inBody_ = false;
            hexLeft_ = 0;
        }
        ++length_;
        if (hexLeft_ > 0) {
            const int digit = hexValue(c);
            if (digit < 0) {
                hexLeft_ = 0;
                return false;
            }
            given_ = static_cast<uint8_t>((given_ << 4) | digit);
            return --hexLeft_ == 0 && given_ == sum_ && length_ > 8;
        }
        if (!inBody_) return false;
        if (c == '*') {
            inBody_ = false;
            hexLeft_ = 2;
            given_ = 0;
            return false;
        }
        /* The talker and sentence type are capitals ("GPGGA", "GNRMC"); a
         * line of noise rarely starts that way twice. */
        if (length_ <= 2 && !(c >= 'A' && c <= 'Z')) {
            inBody_ = false;
            return false;
        }
        if (c < 0x20 || c > 0x7E) {
            inBody_ = false;
            return false;
        }
        sum_ ^= static_cast<uint8_t>(c);
        return false;
    }

private:
    static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    size_t length_ = NMEA_MAX;
    uint8_t sum_ = 0;
    uint8_t given_ = 0;
    uint8_t hexLeft_ = 0;
    bool inBody_ = false;
};

/* 1 found, 0 not, -1 could not ask. */
int fmRadioAnswers() {
    const auto& p = BOARD.expandProbe;
    if (!Wire1.begin(p.fmSda, p.fmScl, FM_PROBE_HZ)) {
        return -1;
    }
    Wire1.beginTransmission(p.fmRadioAddress);
    const bool found = Wire1.endTransmission() == 0;
    Wire1.end();
    /* Quiet again: the bus belongs to the FM module, and this firmware does
     * not use it. */
    pinMode(p.fmSda, INPUT);
    pinMode(p.fmScl, INPUT);
    return found ? 1 : 0;
}

bool gpsSpeaks() {
    const auto& p = BOARD.expandProbe;
    /* TX -1: the core routes no transmit pin, so gpsRx is only ever read. */
    Serial1.begin(p.gpsBaud, SERIAL_8N1, p.gpsRx, -1);
    NmeaSpotter spotter;
    bool found = false;
    const uint32_t start = millis();
    while (!found && millis() - start < GPS_LISTEN_MS) {
        while (!found && Serial1.available() > 0) {
            found = spotter.feed(static_cast<char>(Serial1.read()));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    Serial1.end();   // detaches the pin and deletes the driver
    /* Pulled down rather than floating: on Media I/O this is the amplifier's
     * bit clock, and that is how Attaky park it; beginAudio() takes it from
     * here. With a GPS on it, a plain input -- the GPS drives it, and a pull
     * against an output it owns is a load for no reason. */
    pinMode(p.gpsRx, found ? INPUT : INPUT_PULLDOWN);
    return found;
}

}  // namespace
#endif  /* GUME_TOUCH_CAPACITIVE */

const char* Board::expandModuleName(ExpandModule module) {
    switch (module) {
        case ExpandModule::FmAm:       return "FM-AM";
        case ExpandModule::LoraGps:    return "LoRa-GPS";
        case ExpandModule::ProbeError: return "probe-error";
        case ExpandModule::NoneOrMedia:
        default:                       return "none-or-media";
    }
}

void Board::probeExpandModule() {
    expandModule_ = ExpandModule::NoneOrMedia;
#if GUME_TOUCH_CAPACITIVE
    if (!BOARD.hasExpandProbe()) {
        return;
    }
    /* Seconds at worst, at boot. The watchdog is not armed yet (the first
     * feed() does that), so this is a statement of intent rather than a
     * necessity -- and it stays right if the probe is ever called later. */
    const Watchdog::Pause wdtPause;

    if (BOARD.expandProbe.fmRadioAddress != 0) {
        const int fm = fmRadioAnswers();
        if (fm < 0) {
            expandModule_ = ExpandModule::ProbeError;
        } else if (fm > 0) {
            expandModule_ = ExpandModule::FmAm;
        }
    }
    if (expandModule_ == ExpandModule::NoneOrMedia &&
        BOARD.expandProbe.gpsRx != PIN_NONE && gpsSpeaks()) {
        expandModule_ = ExpandModule::LoraGps;
    }
    /* mountSd() and beginAudio() both act on this; the line says so once. */
    const bool clear = expandModule_ == ExpandModule::NoneOrMedia;
    Serial.printf("[expand] module=%s sd=%s audio=%s\n",
                  expandModuleName(expandModule_),
                  clear ? "allowed" : "skipped", clear ? "allowed" : "skipped");
#endif
}
