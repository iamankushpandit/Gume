// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* What the Wi-Fi radio is carrying, for the battery diagnostic's [drain]
 * lines. Braino itself asks for an NTP answer every few hours, a timezone
 * once and an update manifest once a day, yet the first measured nights
 * showed some four hundred packets a minute arriving and a few leaving. A
 * radio that wakes for each of those is part of the sleep drain, so these
 * say what they are: broadcast or addressed to us, which protocol, and which
 * processes on the handheld hold a socket at all.
 *
 * Counts, protocols, local port numbers and process names only. No address,
 * no network name and nothing from inside a packet is read or printed. */

#include <ostream>

namespace HostNetTraffic {

/* Appends this minute's Wi-Fi fields to a [drain] line; `seconds` is the time
 * since the last call, zero on the first. Main thread. */
void appendMinute(std::ostream& out, double seconds);

/* Prints one [drain] sockets line: every process holding an internet,
 * raw or packet socket, with each socket's protocol and local port. Needs
 * root to see other processes' sockets, which muOS gives. */
void logSockets();

}   // namespace HostNetTraffic
