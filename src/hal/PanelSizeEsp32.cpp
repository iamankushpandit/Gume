// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The ESP32 boards' answer to PanelSize: the profile's panel. A board cannot
 * have any other, so this is what SCREEN_WIDTH / SCREEN_HEIGHT already say,
 * asked through the interface every platform shares. */

#include "PanelSize.h"

int16_t PanelSize::nativeWidth() { return BOARD.panel.nativeWidth; }
int16_t PanelSize::nativeHeight() { return BOARD.panel.nativeHeight; }
