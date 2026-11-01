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

/* The handheld's RGB light, driven as the firmware's status LED.
 *
 * The host board profile declares three virtual RGB pins; the firmware's
 * own LED code (BoardFeedback.cpp: setRgbColor, pulseRgb, the saver's rally
 * colours) drives them through ledcWrite/digitalWrite exactly as it drives a
 * board's LED, and they land here. What the light shows:
 *
 *   - the firmware's colour, whenever it has one lit -- green for right, red
 *     for wrong, the rally colour in the saver;
 *   - otherwise, a rainbow: the light this hardware is sold showing, and
 *     part of what it is to hold one;
 *   - nothing at all when the player has switched the LED off in Settings,
 *     or while the screen is asleep;
 *   - the battery gauge (HostRingGauge.h), asleep or awake, for as long as the
 *     charger is in and for ten seconds after it comes out or showBattery() is
 *     called. Without the charger, a colour the firmware has lit on a screen
 *     that is on comes first, because right and wrong are answers to the
 *     player. With it, the gauge comes first, saver included: the firmware's
 *     colours look the same from here, and the owner asked to see the charge
 *     in every mode. The LED switch in Settings turns the gauge off too.
 *
 * On the Anbernic RG40XXV the light is the ring around each stick, behind a
 * small MCU on /dev/ttyS5 (protocol from muOS's script/device/rgb.sh). Where
 * no such device exists -- a PC -- this does nothing. */
namespace HostLed {

/* One channel of the firmware's LED, 0-255. */
void setChannel(int channel, uint8_t level);   // 0 red, 1 green, 2 blue

/* The panel went to sleep, or woke. */
void setDisplayAwake(bool awake);

/* Someone asked how much battery is left. Takes no time: it only leaves a note
 * for the light's thread, which reads the battery itself. */
void showBattery();

}   // namespace HostLed
