// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* What a sleeping handheld switches off beyond the lamp. HostScreenPower
 * calls these as the panel goes to sleep and as it wakes, and only when it is
 * built to drive the real screen (GUME_HOST_SCREEN_POWER, on for cross-compiled
 * builds), so a desktop build never loses its Wi-Fi or its cores to Braino.
 *
 * The first long measurement asleep, with the lamp already off, read about
 * 290 mA from the fuel gauge. Braino's own part of that had become small; what
 * was left was the platform keeping three things running for nobody:
 *
 * - **Wi-Fi.** Joined, the radio wakes for every broadcast on the network --
 *   some 360 packets a minute on the test network -- and so does the SDIO bus
 *   it sits on. Braino needs it for an NTP answer every few hours and an update
 *   check once a day, and the clock keeps time without it. Asleep, the
 *   interface goes down; waking brings it up and wpa_supplicant rejoins, about
 *   twelve seconds, during which the Wi-Fi badge says so.
 * - **Three of the four cores.** A sleeping Braino polls ten times a second on
 *   one thread. The others are taken offline and brought back on waking.
 *
 * Either can be turned off for a measurement: BRAINO_SLEEP_WIFI=0,
 * BRAINO_SLEEP_CORES=0. The log names which are on.
 *
 * **The display output stays on unless BRAINO_SLEEP_LCD=1.** Disabling it
 * (`lcd0 disable`, a second after the lamp) was the third saver, and the RG40XXV
 * came back from a night of it mirrored left to right with every colour
 * inverted: re-enabling powers the panel up without the setup the bootloader
 * gave it at boot, and only a reboot puts that back. muOS ships the same toggle
 * switched off (`settings/advanced/disp_suspend` = 0) for that reason, and says
 * so in its brightness script. It did not pay for itself either: the gauge read
 * the same 265-275 mA asleep with all three savers as with none. */

namespace HostSleep {

/* The lamp has just gone off. */
void enter();

/* The panel is waking; returns once the display output is back, so that the
 * lamp can come on over a picture. */
void leave();

/* At start: undo whatever a previous run left switched off because it ended
 * asleep -- killed, crashed, or the power button. Its cores stay offline and
 * its display output stays disabled across a restart otherwise, and the
 * restart would come up to a black screen. Markers in /run/braino say what was
 * left off; /run is emptied at boot, so a fresh boot finds none. Wi-Fi does
 * the same from HostWifiOs, which owns the radio. */
void recover();

}   // namespace HostSleep
