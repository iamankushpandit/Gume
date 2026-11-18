// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* What the device gives up while the panel sleeps, on top of the panel.
 *
 * Panel sleep (Board::displaySleep()) used to be all of it: the backlight and
 * the panel went dark and the rest of the device carried on exactly as if
 * somebody were playing -- the processor at full clock, polling a touch ten
 * times a second, and the Wi-Fi station associated with the router all
 * night. An E32R40T with a 3000 mAh pack went from full to about a tenth
 * overnight that way.
 *
 * Same shape as SystemSuspend: one .cpp per platform. The ESP32 boards lower
 * the processor clock and stop the station (SleepPowerEsp32.cpp). The PC and
 * handheld build answers no to both (host/src/HostSleepPower.cpp), because
 * HostSleep already takes the OS's own cores and Wi-Fi interface down when the
 * panel sleeps, and doing it twice would fight it.
 *
 * The runtime decides WHEN (BrainoApp::enterSleep(), tickStationRest(),
 * wakeFromSleep()); Board decides what the network state machine does about
 * it (Board::restWifiForSleep()); this says HOW. */

#include <stdint.h>

namespace SleepPower {

/* How long the panel has been asleep before the station is stopped. Long
 * enough that a console put down for a moment comes back still on the
 * network; short against a night. */
constexpr uint32_t STATION_REST_AFTER_MS = 5UL * 60UL * 1000UL;

/* The processor clock while the panel sleeps. 80 MHz is the lowest at which
 * the Wi-Fi and Bluetooth radios still run on an ESP32, and the APB bus stays
 * at 80 MHz either way, so the serial port, the SPI buses and the LEDC timers
 * keep their rates. */
constexpr uint32_t ASLEEP_CPU_MHZ = 80;

/* Slow the processor down, and put it back. Both are quick and do not block;
 * restoreClock() is called before anything is drawn again. Safe to call when
 * there is nothing to do. */
void lowerClock();
void restoreClock();

/* Stop the Wi-Fi station's radio WITHOUT de-initialising the driver.
 *
 * WiFi.mode(WIFI_OFF) would de-initialise it, and a de-init next to a live
 * Bluetooth controller is the sequence that has panicked this device (a call
 * through a null pointer in coexistence teardown). Stopping is not that: the
 * driver, its netif -- with the "braino" host name it was given at the last
 * start -- and the Bluetooth controller are all left exactly as they were,
 * so the beacon and the Nearby scanner carry on while asleep.
 *
 * Returns true only if a station was running and is now stopped. Blocks for
 * a few milliseconds while the driver says goodbye to the access point: call
 * it inside a Watchdog::Pause, never from a render path. */
bool stopStation();

/* Start what stopStation() stopped. The core re-applies its modem-sleep
 * choice on the start. Joining the network again is the caller's job. Same
 * blocking rule as stopStation(). */
bool startStation();

}  // namespace SleepPower
