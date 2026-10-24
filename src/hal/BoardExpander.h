// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "BoardConfig.h"

/* The AW9523 I2C GPIO expanders some boards hang lines off instead of GPIOs
 * -- on the Attaky Core, the touch controller's reset. Boot-time only:
 * Board::begin() calls these once each, after Wire is up, and nothing on a
 * frame path touches an expander through here. Capacitive builds only,
 * because only they carry Wire. */
#if GUME_TOUCH_CAPACITIVE
namespace BoardExpander {

/* Hold (false) or release (true) the touch reset named by
 * BOARD.touch.resetExpanderAddress / resetExpanderPort. Holding also sets
 * the expander's every other port to an input. */
void driveTouchReset(bool released);

}  // namespace BoardExpander
#endif
