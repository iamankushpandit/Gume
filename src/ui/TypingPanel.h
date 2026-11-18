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
#include "ui/Ui.h"

#if GUME_HAS_KEYBOARD
#include "hal/modules/Modules.h"
#endif

/* A text field's screen when a physical keyboard is doing the typing.
 *
 * On a board that can carry a clip-on keyboard, and with one found at boot
 * and still answering, the five text fields -- a player's name, a Nearby
 * peer's name, both admin PIN pads and the Wi-Fi password -- leave their
 * on-screen keypad off. The space it took goes to the field, which is drawn
 * larger, and to one line saying where to type. What is not a key stays:
 * OK (or JOIN), Cancel or Back, and Show on the password. The keyboard has a
 * Delete key of its own, so no DEL is drawn.
 *
 * NOTHING MAY BECOME REACHABLE ONLY BY KEY. The keyboard comes off, and boot
 * is the only time anybody looks for it, so each of those screens also draws
 * a small button with a keyboard on it that brings the on-screen keypad back
 * for the rest of that visit. A field that takes characters the keyboard
 * does not have keeps those on screen as well: the keyboard types only
 * , . # of the punctuation, so the Wi-Fi password keeps its symbol page
 * (games/WifiPassword.cpp); names and PINs need nothing more. And if
 * the keyboard stops answering while the keypad is off, the keypad comes back
 * by itself: the keyboard driver counts the frames its scan failed in a row,
 * which costs nothing, because the scan runs every frame anyway.
 *
 * A screen keeps a KeypadChoice per field, calls begin() as the field opens,
 * lost() once a frame while it is up (and repaints fully when that says
 * true), and show() when the button is pressed. The choice is made per
 * visit: the next time the field opens, it asks the keyboard again.
 *
 * Only on a board with GUME_HAS_KEYBOARD. Everywhere else KeypadChoice is an
 * empty class whose hidden() is a constant false and the drawing helpers are
 * inline no-ops, so each screen's keyboard branch folds away at compile time
 * and the image carries none of it -- TypingPanel.cpp compiles to nothing. */
namespace Ui {

#if GUME_HAS_KEYBOARD

/* A keyboard was found at boot and its scan is answering now. */
inline bool keyboardTyping() {
    return Modules::allows(ModuleCapability::Typing) && Modules::keyboardAnswering();
}

class KeypadChoice {
public:
    /* The field has just opened: leave the keypad off if a keyboard is
     * typing. */
    void begin() { hidden_ = keyboardTyping(); }
    /* The keypad is off on this visit. */
    bool hidden() const { return hidden_; }
    /* The keyboard button was pressed: the keypad, for the rest of the
     * visit. The caller repaints fully -- the layout changed. */
    void show() { hidden_ = false; }
    /* Once a frame while the field is up: true, once, when the keyboard has
     * stopped answering with the keypad off, which is now back on. */
    bool lost() {
        if (hidden_ && !keyboardTyping()) {
            hidden_ = false;
            return true;
        }
        return false;
    }

private:
    bool hidden_ = false;
};

namespace Typing {

/* The bottom row every field but Wi-Fi's shares with its Cancel, which sits
 * centred at screenH-30: the keyboard button on the left, OK on the right. */
Rect toggleRect(int16_t screenW, int16_t screenH);
Rect acceptRect(int16_t screenW, int16_t screenH);

/* The field when the keypad is off: wide, 44px tall, centred in the space
 * between `top` and the bottom row with its hint line under it. */
Rect fieldRect(int16_t screenW, int16_t screenH, int16_t top);

/* The button that brings the keypad back: a raised button with a small
 * keyboard drawn on it, in the theme's text colour. */
void drawToggle(Renderer& tft, const Rect& r);

/* OK, coloured as the keypad's own OK is. */
void drawAccept(Renderer& tft, const Rect& r, const char* label);

/* "Type on the keyboard", centred at y (top), muted. */
void drawHint(Renderer& tft, int16_t screenW, int16_t y);

}  // namespace Typing

#else   /* no keyboard: nothing to choose, and no code */

class KeypadChoice {
public:
    void begin() {}
    constexpr bool hidden() const { return false; }
    void show() {}
    constexpr bool lost() const { return false; }
};

namespace Typing {
inline Rect toggleRect(int16_t, int16_t) { return Rect{}; }
inline Rect acceptRect(int16_t, int16_t) { return Rect{}; }
inline Rect fieldRect(int16_t, int16_t, int16_t) { return Rect{}; }
inline void drawToggle(Renderer&, const Rect&) {}
inline void drawAccept(Renderer&, const Rect&, const char*) {}
inline void drawHint(Renderer&, int16_t, int16_t) {}
}  // namespace Typing

#endif

}  // namespace Ui
