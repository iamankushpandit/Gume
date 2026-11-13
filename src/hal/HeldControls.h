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

#include "hal/ControlJobs.h"

/* What a board's keys are holding this frame.
 *
 * A board with keys reports them through Board::pollControls(): the board's
 * own and any clipped-on module's, as one set, with the directions already
 * turned into the directions of the picture as it is drawn -- the job
 * pollTouch() does for a finger -- so nothing above the HAL corrects for
 * rotation. It reports what is HELD, not what happened: the selector
 * (engine/AppRuntimeSelector.cpp) works out presses, releases and repeats,
 * the way the Linux host does from SDL's events.
 *
 * Keys are a second way to do what touch already does, never the only way:
 * most boards have none, and the console has to be complete without them. */
struct HeldControls {
    /* 1 << ControlJobs::Control for every button held: A..Menu. The d-pad
     * is not in here; it is `dirs`. */
    uint32_t buttons = 0;
    /* HELD_UP | HELD_DOWN | HELD_LEFT | HELD_RIGHT, in SCREEN directions. */
    uint8_t dirs = 0;
    /* A power key's short press, held. The runtime turns it into Lock and no
     * screen ever sees it. */
    bool power = false;

    bool any() const { return buttons != 0 || dirs != 0 || power; }
    bool held(ControlJobs::Control c) const { return (buttons >> c) & 1u; }
};

constexpr uint8_t HELD_UP = 1;
constexpr uint8_t HELD_DOWN = 2;
constexpr uint8_t HELD_LEFT = 4;
constexpr uint8_t HELD_RIGHT = 8;

/* One character of a GamepadProfile layout (see BoardProfile.h) held down:
 * the control it names, added to `into`. Directions go in PHYSICAL here;
 * Board::pollControls() turns the whole set for the screen. */
inline void holdLayoutKey(char key, HeldControls& into) {
    using namespace ControlJobs;
    switch (key) {
        case 'U': into.dirs |= HELD_UP; break;
        case 'D': into.dirs |= HELD_DOWN; break;
        case 'L': into.dirs |= HELD_LEFT; break;
        case 'R': into.dirs |= HELD_RIGHT; break;
        case 'A': into.buttons |= maskOf(A); break;
        case 'B': into.buttons |= maskOf(B); break;
        case 'X': into.buttons |= maskOf(X); break;
        case 'Y': into.buttons |= maskOf(Y); break;
        case '<': into.buttons |= maskOf(L1); break;
        case '>': into.buttons |= maskOf(R1); break;
        case '{': into.buttons |= maskOf(L2); break;
        case '}': into.buttons |= maskOf(R2); break;
        case 'S': into.buttons |= maskOf(Select); break;
        case 'T': into.buttons |= maskOf(Start); break;
        case 'M': into.buttons |= maskOf(Menu); break;
        default: break;   // '~': no job
    }
}

/* The controls a layout names, as ControlJobs::assign() wants them. */
inline uint32_t layoutControls(const char* layout) {
    if (layout == nullptr) return 0;
    HeldControls all;
    for (uint8_t i = 0; i < 8 && layout[i] != '\0'; ++i) holdLayoutKey(layout[i], all);
    uint32_t present = all.buttons;
    if (all.dirs != 0) present |= ControlJobs::maskOf(ControlJobs::DPad);
    return present;
}
