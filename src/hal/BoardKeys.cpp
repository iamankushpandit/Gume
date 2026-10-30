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

/* Navigation keys: a D-pad, Enter and Back, on the boards that have them.
 *
 * Today that is the Attaky Core, whose face keys hang off an AW9523 I2C
 * expander on the touch controller's bus, and any module that provides
 * NavKeys -- the clip-on gamepad (hal/modules/GamepadModule.cpp). Board::begin()
 * has already left every port of the Core's expander an input except the
 * touch reset, so all this file does is read the input register, OR in what
 * the modules hold, find what changed, and say what it means on the screen.
 * The Core's keys and a gamepad's are one set from here up: one edge, one
 * repeat, one turn for the rotation.
 *
 * Built only where GUME_HAS_NAV_KEYS is set; no other board's image has
 * pollNavKey() at all.
 *
 * Sampled once per frame from the loop, like touch -- which is the honest
 * comparison: touch is also a few bytes over this same bus every frame, and
 * this is one byte more. It is input, read where input is read, not a sensor
 * whose sampling belongs on a task.
 *
 * What it does on top of the edge:
 *
 *  - A held direction repeats, after NAV_REPEAT_DELAY_MS and then every
 *    NAV_REPEAT_EVERY_MS, because walking a 47-tile launcher one press per
 *    tile is tiresome. Enter, Back and Power never repeat: a held Enter opening one
 *    screen after another is not something anybody means.
 *  - At most one event per frame. Two keys pressed in the same 20ms are rare
 *    enough that the second simply waits; inventing a queue for it would be
 *    machinery for a case nobody will notice.
 *  - The direction is turned into SCREEN terms here, never in a screen.
 */

namespace {

constexpr uint32_t NAV_REPEAT_DELAY_MS = 400;
constexpr uint32_t NAV_REPEAT_EVERY_MS = 140;

constexpr uint8_t AW9523_INPUT_P0 = 0x00;   // P1 is the register after it

uint8_t bitOf(NavKey key) {
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(key));
}
static_assert(static_cast<uint8_t>(NavKey::Power) < 8,
              "navDown_ holds one bit per NavKey in a byte");

bool isDirection(NavKey key) {
    return key == NavKey::Up || key == NavKey::Right ||
           key == NavKey::Down || key == NavKey::Left;
}

/* A physical direction, as the screen at `rotation` sees it.
 *
 * The keys are stated in the frame of the board's landscape rotation. Each
 * step up in TFT_eSPI rotation turns the picture a quarter anticlockwise --
 * read off pollTouch()'s per-rotation transform in BoardTouch.cpp, which was
 * checked on hardware with a finger at all four rotations -- so a key that
 * points up at the landscape rotation points one quarter further clockwise
 * on the screen for every step the rotation has moved past it. Clockwise
 * order is Up, Right, Down, Left, which turns that into index arithmetic.
 *
 * NOT YET SEEN ON A DEVICE WITH KEYS. If the D-pad is right at the landscape
 * rotation and wrong in portrait or flipped, the sign below is what to
 * change, and nothing else. */
NavKey turnForScreen(NavKey physical, uint8_t rotation) {
    if (!isDirection(physical)) {
        return physical;
    }
    static constexpr NavKey CLOCKWISE[4] = {NavKey::Up, NavKey::Right,
                                            NavKey::Down, NavKey::Left};
    uint8_t index = 0;
    while (CLOCKWISE[index] != physical) ++index;
    const uint8_t steps = static_cast<uint8_t>((rotation - BOARD.panel.landscapeRotation) & 3);
    return CLOCKWISE[(index + 4 - steps) & 3];
}

}  // namespace

NavKey Board::pollNavKey() {
    const struct {
        NavKey key;
        int8_t port;
    } wiring[] = {
        /* Power, Enter and Back first: if one lands in the same frame as a
         * direction, it is the one that means something -- and locking wins
         * over everything, because it is the one a person reaches for in a
         * hurry. */
        {NavKey::Power, BOARD.keys.keyPower},
        {NavKey::Enter, BOARD.keys.keyEnter}, {NavKey::Back, BOARD.keys.keyBack},
        {NavKey::Up, BOARD.keys.keyUp},       {NavKey::Down, BOARD.keys.keyDown},
        {NavKey::Left, BOARD.keys.keyLeft},   {NavKey::Right, BOARD.keys.keyRight},
    };

    /* One register per bank that has a key on it -- one byte, on the boards
     * there are. A failed read is not a key released: keep the last state, so
     * a glitch on the bus cannot manufacture a press when it clears. */
    uint8_t down = 0;
    if (BOARD.hasNavKeys()) {
        bool needBank[2] = {false, false};
        for (const auto& w : wiring) {
            if (w.port != PIN_NONE) needBank[w.port / 8] = true;
        }
        uint8_t bank[2] = {0, 0};
        for (uint8_t b = 0; b < 2; ++b) {
            if (!needBank[b]) continue;
            Wire.beginTransmission(BOARD.keys.keyExpander);
            Wire.write(static_cast<uint8_t>(AW9523_INPUT_P0 + b));
            if (Wire.endTransmission(false) != 0 ||
                Wire.requestFrom(static_cast<int>(BOARD.keys.keyExpander), 1) != 1) {
                return NavKey::None;
            }
            bank[b] = static_cast<uint8_t>(Wire.read());
        }
        for (const auto& w : wiring) {
            if (w.port == PIN_NONE) continue;
            const bool high = (bank[w.port / 8] >> (w.port % 8)) & 1;
            if (high != BOARD.keys.keysActiveLow) down |= bitOf(w.key);
        }
    }
#if GUME_HAS_ATTAKY_MODULES
    /* Every clipped-on module that provides keys, as they are physically held.
     * Each keeps its own last state across a failed read, as above. */
    down |= Modules::heldNavKeys();
#endif
    const uint8_t pressed = static_cast<uint8_t>(down & ~navDown_);
    navDown_ = down;

    const uint32_t now = millis();
    for (const auto& w : wiring) {
        if (!(pressed & bitOf(w.key))) continue;
        navHeld_ = isDirection(w.key) ? w.key : NavKey::None;
        navRepeatAtMs_ = now + NAV_REPEAT_DELAY_MS;
        return turnForScreen(w.key, displayRotation_);
    }

    if (navHeld_ != NavKey::None) {
        if (!(down & bitOf(navHeld_))) {
            navHeld_ = NavKey::None;
        } else if (static_cast<int32_t>(now - navRepeatAtMs_) >= 0) {
            navRepeatAtMs_ = now + NAV_REPEAT_EVERY_MS;
            return turnForScreen(navHeld_, displayRotation_);
        }
    }
    return NavKey::None;
}

#endif  /* GUME_HAS_NAV_KEYS */
