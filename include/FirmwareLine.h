// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "AppVersion.h"

/* Which release line this image belongs to, and so which version it reports.
 *
 * Braino! ships as two release lines from one codebase. The generic boards are
 * the `braino` line, versioned by BRAINO_VERSION and tagged `v<version>`. The
 * Attaky Core is the `attaky` line, versioned by ATTAKY_VERSION and tagged
 * `attaky-v<version>`, so it can be released when it has been tested on its
 * hardware without moving the generic boards, and the other way round.
 *
 * The line is a fact about the build, not the board, so it is stated where the
 * build is: `-D GUME_LINE_ATTAKY=1` on the environment in platformio.ini, which
 * tools/envs.py reads too. Nothing else may decide it.
 *
 * Everything that tells somebody which version this is -- the boot banner,
 * `identify`, About, System Info, the update comparison -- reads
 * FIRMWARE_VERSION, never one of the two constants directly. An Attaky image
 * also says which Braino it is built from (FIRMWARE_BASE_VERSION), because a
 * bug report against `attaky 1.0.2` is a report against some Braino code, and
 * the version is what says which. */

#ifndef GUME_LINE_ATTAKY
#define GUME_LINE_ATTAKY 0
#endif

#if GUME_LINE_ATTAKY
#define FIRMWARE_LINE          "attaky"
#define FIRMWARE_VERSION       ATTAKY_VERSION
#define FIRMWARE_BASE_VERSION  BRAINO_VERSION
#else
#define FIRMWARE_LINE          "braino"
#define FIRMWARE_VERSION       BRAINO_VERSION
#endif
