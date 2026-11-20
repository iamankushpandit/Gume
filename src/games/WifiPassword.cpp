// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "WifiApp.h"
#include <string.h>
#include "hal/Board.h"

/* The password phase of the Wi-Fi screen -- the field, Show, the on-screen
 * keys and what a keyboard types -- split from WifiApp.cpp, which keeps the
 * other phases, the scan and the routing. */

namespace {
/* With a keyboard typing, the symbol page's rows from here down stay on the
 * panel: row 0 is the digits, which the keyboard has. */
constexpr uint8_t SYMBOL_FIRST_ROW = 1;
}  // namespace

Rect WifiApp::keyRect(uint8_t row, uint8_t col) const {
    return baseRect(static_cast<int16_t>(2 + col * 32),
                    static_cast<int16_t>(96 + row * 28), 28, 24);
}

/* Inside the password field (8, 54, 304, 34), against its right-hand end, so
 * it reads as a property of the field rather than one more key. A button
 * rather than a bare box: it is the one control on this screen a young player
 * might press by accident, so it gets the same raised look as everything else
 * that can be pressed -- and the handheld's d-pad finds controls by it. */
Rect WifiApp::showRect() const {
    return baseRect(234, 59, 74, 24);
}

/* The password, from a physical keyboard. Unlike the name fields this
 * takes exactly what was typed -- a password is case-sensitive and may
 * hold the punctuation on the keys -- with the same 63-character limit as
 * the on-screen keys. Delete and Enter are DEL and JOIN. A symbol the
 * keyboard does not have is on the panel beside it: see renderPassword(). */
void WifiApp::typePassword(GameHost& host, char typed) {
    if (typed == '\n') {
        startConnect(host);
        return;
    }
    if (typed == '\b') {
        if (password_.length() > 0) password_.remove(password_.length() - 1);
    } else if (typed >= 0x20 && typed <= 0x7E) {
        if (password_.length() < 63) password_ += typed;
    } else {
        return;
    }
    markDirty();
}

void WifiApp::updatePassword(GameHost& host, const TouchPoint& touch) {
    const char (*keys)[11] = symbols_ ? KEYS_SYMBOL
                                         : (capsLock_ ? KEYS_UPPER : KEYS_LOWER);
    /* The field is repainted on every keystroke anyway, so the checkbox
     * costs the same partial repaint as a key; the grid is untouched. */
    if (showRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        showPassword_ = !showPassword_;
        markDirty();
        return;
    }
    if (keypad_.hidden()) {
        /* A keyboard is typing the password: the symbols it cannot type,
         * the keyboard button where CAPS was, JOIN, Show and back are all
         * there is to press. */
        for (uint8_t row = SYMBOL_FIRST_ROW; row < 4; ++row) {
            for (uint8_t col = 0; col < 10; ++col) {
                if (keyRect(row, col).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                    if (password_.length() < 63) password_ += KEYS_SYMBOL[row][col];
                    markDirty(); return;
                }
            }
        }
        if (baseRect(2, 208, 52, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            keypad_.show();
            keyboardPainted_ = false;
            markFullDirty();
            return;
        }
        if (baseRect(246, 208, 72, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) { startConnect(host); return; }
        if (baseRect(8, 34, 60, 20).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::List;
            markFullDirty();
        }
        return;
    }
    for (uint8_t row = 0; row < 4; ++row) {
        for (uint8_t col = 0; col < 10; ++col) {
            if (keyRect(row, col).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                if (password_.length() < 63) password_ += keys[row][col];
                markDirty(); return;
            }
        }
    }
    if (baseRect(2, 208, 52, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        capsLock_ = !capsLock_; symbols_ = false;
        keyboardPainted_ = false; markDirty(); return;
    }
    if (baseRect(58, 208, 52, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        symbols_ = !symbols_; keyboardPainted_ = false; markDirty(); return;
    }
    if (baseRect(114, 208, 74, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (password_.length() < 63) password_ += ' ';
        markDirty(); return;
    }
    if (baseRect(192, 208, 50, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (password_.length() > 0) password_.remove(password_.length() - 1);
        markDirty(); return;
    }
    if (baseRect(246, 208, 72, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) { startConnect(host); return; }
    if (baseRect(8, 34, 60, 20).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        phase_ = Phase::List;
        markFullDirty();
    }
}

void WifiApp::renderPassword(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const char (*keys)[11] = symbols_ ? KEYS_SYMBOL
                                         : (capsLock_ ? KEYS_UPPER : KEYS_LOWER);
    const String ssid = selectedSsid_.length() ? selectedSsid_ : board.wifiSsid();
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("< back   ") + ssid, baseX(8), baseY(36), 2);
    /* The one shape on this screen that is not built from a Rect, and so
     * the one the mapping pass missed: it was still drawn at the design
     * size while the text inside it had moved, which put the caption
     * below the box it belongs in. */
    const Rect field = baseRect(8, 54, 304, 34);
    tft.fillRoundRect(field.x, field.y, field.w, field.h, 4, Ui::surface());
    tft.drawRoundRect(field.x, field.y, field.w, field.h, 4, Ui::outline());
    tft.setTextColor(password_.length() > 0 ? Ui::text() : Ui::muted(), Ui::surface());
    tft.setTextDatum(ML_DATUM);
    /* One '*' per character unless Show is ticked: the length is still
     * there to count, which is what catches a missed or doubled key. */
    String shown = password_.length() > 0 ? password_ : "Password";
    if (password_.length() > 0 && !showPassword_) {
        char stars[64];
        const size_t n = password_.length() < sizeof(stars) - 1 ? password_.length()
                                                                 : sizeof(stars) - 1;
        memset(stars, '*', n);
        stars[n] = '\0';
        shown = stars;
    }
    const Rect show = showRect();
    const int16_t fieldTextMax = static_cast<int16_t>(show.x - field.x - 16);
    while (shown.length() > 2 && tft.textWidth(shown, 2) > fieldTextMax) {
        shown.remove(shown.length() - 1);
    }
    /* The field is not cleared above the text's own width on a shorter
     * repaint, so wipe the text area first: deleting a character, or
     * switching '*'s for letters, must not leave the old tail behind. */
    tft.fillRect(static_cast<int16_t>(field.x + 2), static_cast<int16_t>(field.y + 2),
                 static_cast<int16_t>(show.x - field.x - 4), static_cast<int16_t>(field.h - 4),
                 Ui::surface());
    /* Centred in the field rather than at a mapped y of its own, so the
     * two cannot drift apart again. */
    tft.drawString(shown, static_cast<int16_t>(field.x + 8),
                   static_cast<int16_t>(field.y + field.h / 2), 2);

    /* Show: a box that fills with the success colour and carries a tick
     * when the password is visible, and the word beside it. */
    Ui::drawButton(tft, show, "", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    const int16_t box = 14;
    const int16_t bx = static_cast<int16_t>(show.x + 6);
    const int16_t by = static_cast<int16_t>(show.y + (show.h - box) / 2);
    tft.fillRoundRect(bx, by, box, box, 3, showPassword_ ? Ui::success() : Ui::surface());
    tft.drawRoundRect(bx, by, box, box, 3, Ui::outline());
    if (showPassword_) {
        const uint16_t ink = Ui::onFill(Ui::success());
        tft.drawLine(bx + 3, by + 7, bx + 6, by + 10, ink);
        tft.drawLine(bx + 6, by + 10, bx + 11, by + 4, ink);
        tft.drawLine(bx + 3, by + 8, bx + 6, by + 11, ink);
        tft.drawLine(bx + 6, by + 11, bx + 11, by + 5, ink);
    }
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(ML_DATUM);   // drawButton() leaves its own datum behind
    tft.drawString("Show", static_cast<int16_t>(bx + box + 6),
                   static_cast<int16_t>(show.y + show.h / 2), 2);
    /* THE KEYBOARD IS NOT REPAINTED PER KEYSTROKE.
     *
     * Every key used to call markFullDirty(), which clears the screen and
     * redraws forty-five buttons -- each a shadow roundrect, a fill, a
     * bevel, an outline and a glyph -- to change one character in the
     * field above them. On the panel that is the whole screen flashing at
     * typing speed, on the one screen where an owner is looking carefully
     * at what they have typed.
     *
     * What actually changes is the field, which is drawn above on every
     * repaint. The keys change only when the LAYER does -- caps, symbols
     * -- or when the screen underneath was cleared, and both of those say
     * so by clearing keyboardPainted_. */
    if (keypad_.hidden()) {
        /* A keyboard is typing: the hint where the digit row was, the
         * symbol page's three rows below it -- the keyboard has letters,
         * digits, space and only , . # of the punctuation, and a Wi-Fi
         * password may hold any of these -- the keyboard button where
         * CAPS was, and JOIN. The keyboard has its own Delete. Painted
         * once, like the keys. */
        if (!keyboardPainted_) {
            Ui::Typing::drawHint(tft, panelW_, baseY(100));
            for (uint8_t row = SYMBOL_FIRST_ROW; row < 4; ++row) {
                for (uint8_t col = 0; col < 10; ++col) {
                    const char buf[2] = {KEYS_SYMBOL[row][col], 0};
                    Ui::drawButton(tft, keyRect(row, col), buf, Ui::surface(), Ui::outline(), Ui::text(), false, 2);
                }
            }
            Ui::Typing::drawToggle(tft, baseRect(2, 208, 52, 24));
            Ui::drawButton(tft, baseRect(246, 208, 72, 24), "JOIN", Ui::accent(), Ui::outline(), TFT_WHITE, false, 2);
            keyboardPainted_ = true;
        }
    } else if (!keyboardPainted_) {
        for (uint8_t row = 0; row < 4; ++row) {
            for (uint8_t col = 0; col < 10; ++col) {
                char buf[2] = {keys[row][col], 0};
                Ui::drawButton(tft, keyRect(row, col), String(buf), Ui::surface(), Ui::outline(), Ui::text(), false, 2);
            }
        }
        keyboardPainted_ = true;
    }
    if (!keypad_.hidden()) {
        Ui::drawButton(tft, baseRect(2, 208, 52, 24), "CAPS",
                       capsLock_ ? Ui::accent() : Ui::surface(), Ui::outline(),
                       capsLock_ ? TFT_WHITE : Ui::text(), false, 1);
        Ui::drawButton(tft, baseRect(58, 208, 52, 24), symbols_ ? "abc" : "!#$",
                       symbols_ ? Ui::accent() : Ui::surface(), Ui::outline(),
                       symbols_ ? TFT_WHITE : Ui::text(), false, 1);
        Ui::drawButton(tft, baseRect(114, 208, 74, 24), "SPACE", Ui::surface(), Ui::outline(), Ui::text(), false, 1);
        Ui::drawButton(tft, baseRect(192, 208, 50, 24), "DEL", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
        Ui::drawButton(tft, baseRect(246, 208, 72, 24), "JOIN", Ui::accent(), Ui::outline(), TFT_WHITE, false, 2);
    }
}
