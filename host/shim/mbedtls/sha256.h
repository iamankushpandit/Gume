// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The one mbedtls call the firmware makes: a one-shot SHA-256, used to turn
 * the device-id seed into an id that cannot be reversed. A real SHA-256, so
 * the host id is made the same way a board's is. */

#include <cstddef>

int mbedtls_sha256(const unsigned char* input, size_t len, unsigned char output[32], int is224);
