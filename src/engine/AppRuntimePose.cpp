// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* Auto-rotate: the screen turns when the device does.
 *
 * Board::layoutMode() and Board::screenFlipped() already answer from the
 * device's pose while Auto is on (hal/DevicePose.h), so everything that reads
 * them is right the next time it reads. What is left is to notice the turn
 * and act on it the way a change in Settings does -- applyOrientation(),
 * which turns the screen that is up if its rotation changed and does nothing
 * if not (a game in landscape is still landscape when the device stands up).
 *
 * The launcher is started over as well: its page size is four tiles in
 * portrait and six in landscape, and begin() is where it clamps its page to
 * the new count.
 *
 * Nothing turns while the saver, panel sleep or the lock screen is up --
 * applyOrientation() leaves those alone, and resumeUnderlyingScreen() applies
 * the rotation when they hand back. Every ESP32 board has no sensor, so
 * autoRotating() is false there and this costs one call a frame. */

#include "AppRuntime.h"

void BrainoApp::followPose() {
    if (!board_.autoRotating()) {
        poseTurn_ = 0xFF;   // Auto off, or no pose: the next one seen turns the screen
        return;
    }
    const uint8_t turn = static_cast<uint8_t>(
        (board_.layoutMode() == Board::LayoutMode::Vertical ? 1 : 0) |
        (board_.screenFlipped() ? 2 : 0));
    if (turn == poseTurn_) return;
    const bool first = poseTurn_ == 0xFF;
    poseTurn_ = turn;
    if (first && view_ != View::Game) return;
    applyOrientation();
    if (view_ == View::Game && activeGame_ == &launcher_) {
        relaunchActiveGame();
    }
}
