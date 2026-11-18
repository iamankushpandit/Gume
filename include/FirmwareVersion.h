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
#include "BoardConfig.h"

/* Which release this is, and which device it was built for, in one string.
 *
 * Every platform ships the same BRAINO_VERSION under the same tag. What an
 * image REPORTS is FIRMWARE_VERSION: that version, a "+", and the board's
 * GUME_VERSION_TAG -- 5.14.0+e32r28t1, 5.14.0+attaky, 5.14.0+h700. The owner
 * decided it on 2026-11-13, retiring a separate release line for one board:
 * one release number everywhere, and a version string that says which device
 * it is for and nothing else.
 *
 * The part after "+" is semver build metadata, so it never decides which
 * version is newer. The update check compares BRAINO_VERSION alone, and the
 * manifest, the release tag and every published version carry only that --
 * tools/check_docs.py and tools/pack_release.py refuse a published version
 * with a "+" in it.
 *
 * This is the one accessor. The boot banner, `identify`, About and System
 * Info read FIRMWARE_VERSION; nothing under src/ reads BRAINO_VERSION except
 * the update comparison (check_docs.py holds that line). It stays a macro,
 * a string literal, so a screen can concatenate it and nothing allocates.
 *
 * The tag is a board fact, declared in each board header beside the other
 * GUME_ macros: lower case, [0-9a-z-], the model as an owner would know it.
 * The host build defaults to "host" and a handheld image names its family
 * from CMake. */

#ifndef GUME_VERSION_TAG
#error "The board header must define GUME_VERSION_TAG, the device part of the version."
#endif

#define FIRMWARE_VERSION BRAINO_VERSION "+" GUME_VERSION_TAG
