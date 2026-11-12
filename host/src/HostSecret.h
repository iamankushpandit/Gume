// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* Sealing for the few settings that must not be readable off the card.
 *
 * A board keeps NVS in its own flash, which takes tools and intent to read.
 * The host keeps it in nvs.txt on whatever disk it runs from -- on a handheld,
 * a microSD card that any laptop opens. So the Wi-Fi name and password and the
 * admin PIN are sealed before they reach that file (see HostNvs.cpp).
 *
 * The key is derived from the console's own processor: the chip serial Linux
 * exposes, read on every start, never stored, logged or transmitted, and used
 * only through a one-way hash. A card read in a laptop, or moved to another
 * console, cannot be opened. Someone holding the whole console and knowing
 * where to look can still derive the key -- the code is open -- which is the
 * same bar as reading a board's flash, and the honest limit of what software
 * without a secure element can do.
 *
 * The construction is HMAC-SHA256 throughout, over the SHA-256 the firmware
 * already uses: a random 16-byte nonce per value, a keystream of HMAC blocks,
 * and a 16-byte encrypt-then-MAC tag over the nonce, the value's name and the
 * ciphertext, so a value that was altered, or moved to another name, fails to
 * open rather than opening wrong. */

#include <cstdint>
#include <string>
#include <vector>

namespace HostSecret {

/* Whether a key could be made. Looked up once. On a handheld build only a
 * processor-bound source counts; a desktop build may fall back to the OS's
 * machine id, which protects nothing from someone with the disk and says so
 * in the log. */
bool available();

/* Seal `plain` to be stored under `name`. False when no key is available. */
bool seal(const std::string& name, const std::vector<uint8_t>& plain, std::vector<uint8_t>& out);

/* Open what seal() made for `name`. False when there is no key, or the value
 * was sealed with another key, or it has been altered. */
bool open(const std::string& name, const std::vector<uint8_t>& sealed, std::vector<uint8_t>& out);

}   // namespace HostSecret
