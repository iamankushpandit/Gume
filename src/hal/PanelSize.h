// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>

#include "BoardConfig.h"

/* How big the panel is, as this device has it -- the one question about the
 * display whose answer a platform may only learn at run time.
 *
 * A board knows its panel when it is compiled: the profile states it, and
 * SCREEN_WIDTH / SCREEN_HEIGHT are derived from it. A Linux handheld or a PC
 * does not -- the same program runs on a 640x480 RG40XX V, a 1280x720
 * television and a window of whatever size it was given -- so it finds the
 * size when it starts and draws at that resolution rather than stretching a
 * 320x240 picture. Anything that means "the panel in front of the player"
 * reads it here, never SCREEN_*, so both kinds of platform are served by one
 * line of code.
 *
 * One implementation per platform: hal/PanelSizeEsp32.cpp answers from the
 * profile; host/src/HostPanelSize.cpp answers with what the host found at
 * start-up. The size does not change while the firmware runs, and it is never
 * smaller than the profile's panel, which BoardConfig.h has already checked
 * holds the game canvas.
 *
 * Playable games do not read it. They draw on the fixed GAME_CANVAS_* canvas,
 * and ScaledRenderer fits that to this size. System screens do not read it
 * either: they read the live tft.width() / tft.height(), which is this size
 * turned to the screen's rotation. */
namespace PanelSize {

/* In the panel's own orientation, as BoardProfile::panel states it. */
int16_t nativeWidth();
int16_t nativeHeight();

/* Turned to the profile's landscape rotation: the extent the game canvas has
 * to fill. */
inline int16_t landscapeWidth() {
    return (BOARD.panel.landscapeRotation & 1) ? nativeHeight() : nativeWidth();
}
inline int16_t landscapeHeight() {
    return (BOARD.panel.landscapeRotation & 1) ? nativeWidth() : nativeHeight();
}

}  // namespace PanelSize
