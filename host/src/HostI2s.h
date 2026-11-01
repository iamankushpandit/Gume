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

}   // namespace HostI2s
