// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "BleBeacon.h"

/* The seam between the two halves of BleBeacon.
 *
 * BleBeaconPayload.cpp owns the wire format; BleBeacon.cpp owns the radio.
 * These three are the whole of what the radio needs from the format, and they
 * are declared here rather than in BleBeacon.h because nothing outside this
 * module may reach them -- the payload has exactly one description and exactly
 * one way of being produced.
 */
namespace BleBeacon {

/** Fill in deviceId / familyId / deviceName from the factory MAC. */
void deriveIdentity(Advertisement& a);

/** Compile the struct into the raw AD buffer the controller is handed. */
void buildPayload(Advertisement& a);

/** Parse "A4F2" into the two bytes the payload carries. */
bool parseDeviceId(const char* text, uint8_t out[2]);

}   // namespace BleBeacon
