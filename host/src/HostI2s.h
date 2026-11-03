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

/* The SDL audio device behind host/shim/driver/i2s.h. */
namespace HostI2s {

/* Open a signed 16-bit stereo device at `rate`. False when the host has no
 * audio output, in which case the firmware runs silent, as a board with no
 * audio path does. */
bool open(int rate);

/* Amplitude percentage applied to every sample written, 100 being unity. */
void setVolume(uint8_t percent);

/* The firmware's amplifier enable, which on a handheld is the audio device
 * itself: an open device keeps the codec, its transfers and the speaker
 * amplifier running, silent or not. release() closes it, off the caller's
 * thread because closing waits out the last buffer; the next sound reopens
 * it. hold() cancels a release that has not happened yet. */
void release();
void hold();

/* Times the device has been opened since start, for the battery diagnostic. */
uint32_t openCount();

}   // namespace HostI2s
