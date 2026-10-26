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

/* One navigation key event, already in SCREEN terms.
 *
 * A board with a D-pad reports it through Board::pollNavKey(), which turns
 * the physical direction into the direction on the picture as it is
 * currently drawn -- the same job pollTouch() does for a finger -- so a screen
 * never corrects for rotation itself. At most one event per frame: a press,
 * or a repeat of a direction that is being held.
 *
 * Its own header, like TouchTypes.h, so a screen can read keys without
 * pulling in Board.h.
 *
 * Keys are a second way to do what touch already does, never the only way:
 * most boards have none, and the console has to be complete without them. */
enum class NavKey : uint8_t {
    None,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Back,
    /* A power key's short press. The runtime consumes it as the Lock button
     * and no screen ever sees it -- see BrainoApp::loop(). */
    Power,
};
