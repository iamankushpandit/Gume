// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Modules.h"

/* Attaky's Expand: Media I/O module -- the microSD slot and the NS4168
 * speaker amplifier, and the Expand module this firmware is built around.
 *
 * The Attaky Core takes one Expand module at a time, and three of them use
 * IO5-8: Media I/O for its microSD slot, LoRa/GPS for its radio's SPI, and
 * FM/AM for a tuner GPIO. On a Core wearing either of the others, mounting the
 * SD card would clock those lines into somebody else's chip -- and starting
 * the speaker would clock the amplifier's I2S on IO17/IO18/IO21, which are the
 * LoRa/GPS module's GPS UART and radio interrupt and the FM/AM module's own
 * I2S. So the three share the "expand" slot, the table decides it once in
 * Board::begin() before either comes up, and the SD slot and the speaker are
 * used only if this module is the one there -- Board::mountSd() and the I2S
 * branch of beginAudio() ask Modules::allows() for Storage and Sound.
 *
 * Media I/O has no identity a host can read, so it is found by elimination:
 * present when neither of the others answered. That reads the same as no
 * Expand module at all, which is why its log tag says so, and both mean the
 * slot and the speaker come up exactly as they did before any of this
 * existed. A probe that could not run decides the slot "unknown", and then
 * this is not present either: both stay down. */

#if GUME_HAS_ATTAKY_MODULES

namespace {

constexpr Modules::Driver DRIVER = {
    /* name           */ "Media I/O",
    /* tag            */ "none-or-media",
    /* order          */ 60,
    /* provides       */ static_cast<uint8_t>(static_cast<uint8_t>(ModuleCapability::Storage) |
                                              static_cast<uint8_t>(ModuleCapability::Sound)),
    /* slot           */ "expand",
    /* byElimination  */ true,
    /* live           */ false,
    /* probe          */ nullptr,
    /* heldNavKeys    */ nullptr,
    /* typedChar      */ nullptr,
    /* readBattery    */ nullptr,
};
const Modules::Registration REGISTRATION(DRIVER);

}  // namespace

#endif  /* GUME_HAS_ATTAKY_MODULES */
