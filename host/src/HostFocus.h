// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The selector arithmetic lives in src/ui/FocusMath.h, shared with the
 * firmware's selector. host/ adapts to src/, so this is only its old name.
 * By relative path, so a build given only -Ihost/src still finds it. */
#include "../../src/ui/FocusMath.h"

namespace HostFocus = FocusMath;
