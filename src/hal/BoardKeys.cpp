// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Board.h"

#if GUME_HAS_NAV_KEYS
#include <Wire.h>

#include "modules/Modules.h"

/* The keys a board has, as one held set.
 *
 * Today that is the Attaky Core: its own keys hang off an AW9523 I2C expander
 * on the touch controller's bus -- a D-pad, a centre key, the top-left
 * shoulder and the power key, on P0 of the expander at 0x59 -- and any module
 * that provides NavKeys adds its own (hal/modules/GamepadModule.cpp). The
 * Core's keys and a gamepad's are one set from here up.
 *
 * This file reports what is HELD. Presses, releases, repeats and what a key
 * is for are the selector's (engine/AppRuntimeSelector.cpp) and the job
 * table's (hal/ControlJobs.h), the same two pieces the Linux host's selector
 * uses, so a button means the same thing on every console.
 *
 * Board::begin() has already left every port of the Core's expander an input
 * except the touch reset, so all this does is read the input register, add
 * what the modules hold, and say what the directions mean on the screen.
 *
 * Built only where GUME_HAS_NAV_KEYS is set; no other board's image has
 * pollControls() at all.
 *
 * Sampled once per frame from the loop, like touch -- which is the honest
 * comparison: touch is also a few bytes over this same bus every frame, and
 * this is one byte more. It is input, read where input is read, not a sensor
 * whose sampling belongs on a task. */

namespace {

constexpr uint8_t AW9523_INPUT_P0 = 0x00;

/* The physical directions, turned to how the screen at `rotation` sees them.
 *
 * The keys are stated in the frame of the board's landscape rotation. Each
 * step up in TFT_eSPI rotation turns the picture a quarter anticlockwise --
 * read off pollTouch()'s per-rotation transform in BoardTouch.cpp, which was
 * checked on hardware with a finger at all four rotations -- so a key that
 * points up at the landscape rotation points one quarter further clockwise
 * on the screen for every step the rotation has moved past it. Clockwise
 * order is Up, Right, Down, Left, which turns that into index arithmetic.
 *
 * Seen on a device at the landscape rotation only. If the D-pad is right
 * there and wrong in portrait or flipped, the sign below is what to change,
 * and nothing else. */
uint8_t turnForScreen(uint8_t dirs, uint8_t rotation) {
    static constexpr uint8_t CLOCKWISE[4] = {HELD_UP, HELD_RIGHT, HELD_DOWN, HELD_LEFT};
    const uint8_t steps = static_cast<uint8_t>((rotation - BOARD.panel.landscapeRotation) & 3);
    uint8_t turned = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        if (dirs & CLOCKWISE[i]) turned |= CLOCKWISE[(i + 4 - steps) & 3];
    }
    return turned;
}

/* One key port of the Core's expander, held. */
bool coreHeld(uint8_t level, int8_t port) {
    if (port == PIN_NONE || port >= 8) return false;
    const bool high = (level >> port) & 1;
    return high != BOARD.keys.keysActiveLow;
}

/* Whether a module that brings its own face buttons was found at boot: then
 * the Core's centre key is Select, and without one it is A, the press. */
bool padAttached() {
#if GUME_HAS_ATTAKY_MODULES
    return Modules::allows(ModuleCapability::NavKeys);
#else
    return false;
#endif
}

}  // namespace

uint32_t Board::controlsPresent() const {
    using namespace ControlJobs;
    uint32_t present = 0;
    if (BOARD.hasNavKeys()) {
        if (BOARD.keys.keyUp != PIN_NONE) present |= maskOf(DPad);
        if (BOARD.keys.keyEnter != PIN_NONE) present |= padAttached() ? maskOf(Select) : maskOf(A);
        if (BOARD.keys.keyBack != PIN_NONE) present |= maskOf(L1);
    }
    if (padAttached()) {
        present |= layoutControls(BOARD.gamepad.padLeftLayout);
        present |= layoutControls(BOARD.gamepad.padRightLayout);
    }
    return present;
}

HeldControls Board::pollControls() {
    HeldControls held;
    if (BOARD.hasNavKeys()) {
        /* One register, P0. A failed read is not a key released: keep the
         * last level, so a glitch on the bus cannot manufacture a press when
         * it clears. */
        Wire.beginTransmission(BOARD.keys.keyExpander);
        Wire.write(AW9523_INPUT_P0);
        if (Wire.endTransmission(false) == 0 &&
            Wire.requestFrom(static_cast<int>(BOARD.keys.keyExpander), 1) == 1) {
            const uint8_t level = static_cast<uint8_t>(Wire.read());
            /* The one key log there is: the Core's own keys have not yet been
             * seen to answer on a device, and this line, printed only when
             * the port changes, is how they will be. */
            if (level != keyLevel_) {
                Serial.printf("[keys] core 0x%02X P0=%02X\n", BOARD.keys.keyExpander, level);
            }
            keyLevel_ = level;
        }
        using namespace ControlJobs;
        if (coreHeld(keyLevel_, BOARD.keys.keyUp)) held.dirs |= HELD_UP;
        if (coreHeld(keyLevel_, BOARD.keys.keyDown)) held.dirs |= HELD_DOWN;
        if (coreHeld(keyLevel_, BOARD.keys.keyLeft)) held.dirs |= HELD_LEFT;
        if (coreHeld(keyLevel_, BOARD.keys.keyRight)) held.dirs |= HELD_RIGHT;
        if (coreHeld(keyLevel_, BOARD.keys.keyEnter)) {
            held.buttons |= padAttached() ? maskOf(Select) : maskOf(A);
        }
        if (coreHeld(keyLevel_, BOARD.keys.keyBack)) held.buttons |= maskOf(L1);
        held.power = coreHeld(keyLevel_, BOARD.keys.keyPower);
    }
#if GUME_HAS_ATTAKY_MODULES
    /* Every clipped-on module that provides keys, as they are physically
     * held. Each keeps its own last state across a failed read, as above. */
    Modules::heldControls(held);
#endif
    held.dirs = turnForScreen(held.dirs, displayRotation_);
    return held;
}

#endif  /* GUME_HAS_NAV_KEYS */
