// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Modules.h"

/* Attaky's Expand: LoRa/GPS module -- one of three Expand modules on the
 * "expand" slot, all of which share IO5-8 and the speaker's I2S lines. See
 * ExpandMediaIo.cpp for why the slot is decided before the SD slot and the
 * speaker come up, and the module table (Modules.h) for how it is decided.
 *
 * Found passively, as Attaky's shared/module-detection-and-safe-boot.md lays
 * out: its AT6558 sends NMEA sentences every second without being asked. The
 * UART's receiver only is attached to BOARD.expandProbe.gpsRx -- no TX pin, so
 * the pin is an input throughout -- for GPS_LISTEN_MS, and one sentence whose
 * checksum adds up is enough. A checksum, not a '$': on Media I/O that line is
 * the amplifier's bit clock, idle and pulled, and a floating line can produce
 * a '$' but not a whole sentence that sums.
 *
 * It costs GPS_LISTEN_MS of every boot on the board that has it, and only
 * after the FM/AM probe has not answered: the table does not ask a second
 * module about a slot the first has already claimed.
 *
 * Provides nothing the console uses -- the radio and the GPS are not read.
 * Finding it is what keeps the SD slot and the speaker DOWN, because their
 * lines are this module's radio SPI, GPS UART and radio interrupt. */

#if GUME_HAS_ATTAKY_MODULES
#include <Arduino.h>

#include "hal/Watchdog.h"

namespace {

/* Attaky's figure: the GPS reports once a second, so this is a second and a
 * half of sentences, and a whole one always lands inside it. */
constexpr uint32_t GPS_LISTEN_MS = 1500;
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

Modules::Probe probe() {
    const auto& p = BOARD.expandProbe;
    if (p.gpsRx == PIN_NONE) {
        return Modules::Probe::Absent;
    }
    /* A second and a half, at boot. The watchdog is not armed yet (the first
     * feed() does that), so this is a statement of intent rather than a
     * necessity -- and it stays right if the probe is ever called later. */
    const Watchdog::Pause wdtPause;

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
    return found ? Modules::Probe::Present : Modules::Probe::Absent;
}

constexpr Modules::Driver DRIVER = {
    /* name           */ "LoRa/GPS",
    /* tag            */ "LoRa-GPS",
    /* order          */ 50,
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
