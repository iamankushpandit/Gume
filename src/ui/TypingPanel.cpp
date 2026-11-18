// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "TypingPanel.h"

/* The pieces of a text field's screen with the keypad off. See TypingPanel.h.
 * Compiles to nothing on a board without GUME_HAS_KEYBOARD. */

#if GUME_HAS_KEYBOARD

namespace Ui {
namespace Typing {
namespace {
constexpr int16_t ROW_H = 22;          // Cancel's height, at screenH-30
constexpr int16_t ROW_FROM_BOTTOM = 30;
constexpr int16_t TOGGLE_W = 52;
constexpr int16_t ACCEPT_W = 60;
constexpr int16_t EDGE = 8;
constexpr int16_t FIELD_H = 44;
constexpr int16_t HINT_GAP = 10;       // field to hint
constexpr int16_t HINT_H = 16;         // font 2
}  // namespace

Rect toggleRect(int16_t, int16_t screenH) {
    return Rect{EDGE, static_cast<int16_t>(screenH - ROW_FROM_BOTTOM), TOGGLE_W, ROW_H};
}

Rect acceptRect(int16_t screenW, int16_t screenH) {
    return Rect{static_cast<int16_t>(screenW - EDGE - ACCEPT_W),
                static_cast<int16_t>(screenH - ROW_FROM_BOTTOM), ACCEPT_W, ROW_H};
}

/* Centred in what the keypad left: from `top` down to a few pixels above the
 * bottom row, with the hint counted in, so the hint can never reach the
 * buttons. */
Rect fieldRect(int16_t screenW, int16_t screenH, int16_t top) {
    const int16_t bottom = static_cast<int16_t>(screenH - ROW_FROM_BOTTOM - 8);
    const int16_t block = FIELD_H + HINT_GAP + HINT_H;
    int16_t y = static_cast<int16_t>(top + (bottom - top - block) / 2);
    if (y < top) y = top;
    const int16_t w = static_cast<int16_t>(min<int16_t>(280, screenW - 2 * 12));
    return Rect{static_cast<int16_t>((screenW - w) / 2), y, w, FIELD_H};
}

void drawToggle(Renderer& tft, const Rect& r) {
    /* A button first, so the d-pad's selector finds it as it finds every
     * other control, then the glyph inside: a rounded body, two rows of keys
     * and a space bar. 24x12, centred, clear of the selector's ring two
     * pixels in. */
    Ui::drawButton(tft, r, "", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
    const uint16_t ink = Ui::text();
    const int16_t gw = 24;
    const int16_t gh = 12;
    const int16_t gx = static_cast<int16_t>(r.x + (r.w - gw) / 2);
    const int16_t gy = static_cast<int16_t>(r.y + (r.h - gh) / 2);
    tft.drawRoundRect(gx, gy, gw, gh, 2, ink);
    for (int16_t row = 0; row < 2; ++row) {
        for (int16_t col = 0; col < 5; ++col) {
            tft.fillRect(static_cast<int16_t>(gx + 3 + col * 4),
                         static_cast<int16_t>(gy + 2 + row * 3), 2, 2, ink);
        }
    }
    tft.fillRect(static_cast<int16_t>(gx + 7), static_cast<int16_t>(gy + 8), 10, 2, ink);
}

void drawAccept(Renderer& tft, const Rect& r, const char* label) {
    Ui::drawButton(tft, r, label, Ui::rgb(45, 154, 96), Ui::outline(), Ui::text(), false, 2);
}

void drawHint(Renderer& tft, int16_t screenW, int16_t y) {
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Type on the keyboard", static_cast<int16_t>(screenW / 2), y, 2);
    tft.setTextDatum(TL_DATUM);
}

}  // namespace Typing
}  // namespace Ui

#endif  /* GUME_HAS_KEYBOARD */
