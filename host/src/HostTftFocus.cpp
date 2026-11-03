// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* What the d-pad's selector can land on, read off the panel as it is drawn
 * (see focusTargets() in TFT_eSPI.h). No screen knows about it: every control
 * worth selecting was drawn with a fill and an outline of one rectangle, which
 * is what Ui::drawButton() ends in. */

#include <TFT_eSPI.h>

#include <algorithm>

#include "BoardConfig.h"   // TOUCH_HIT_SLOP

namespace {
/* Ui::BUTTON_SHADOW_DX and DY, the offset of the fill drawn before the button.
 * Not read from Ui.h: a little slack costs nothing, and a theme may change it. */
constexpr int32_t SHADOW_REACH = 4;
constexpr int32_t SMALLEST_CONTROL = 12;
constexpr int MIN_CARD_CONTROLS = 4;
constexpr int32_t NAME_AIR = 3;   // between the player's name and the selector
}   // namespace

void TFT_eSPI::noteFocusFill(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    const int32_t dx = focusFill_.x - x;
    const int32_t dy = focusFill_.y - y;
    const bool shadowed = focusFill_.w == w && focusFill_.h == h && dx > 0 && dx <= SHADOW_REACH &&
                          dy >= 0 && dy <= SHADOW_REACH;
    focusFill_ = FocusFill{x, y, w, h, shadowed};
}

void TFT_eSPI::noteFocusOutline(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    if (focusFill_.x != x || focusFill_.y != y || focusFill_.w != w || focusFill_.h != h) return;
    if (w < SMALLEST_CONTROL || h < SMALLEST_CONTROL) return;

    /* The part inside the clip: a row half scrolled out of its list can still
     * be chosen, and is pressed where it can be seen. */
    const int32_t ax0 = std::max(x, vpX_);
    const int32_t ay0 = std::max(y, vpY_);
    const int32_t ax1 = std::min(x + w - 1, vpX_ + vpW_ - 1);
    const int32_t ay1 = std::min(y + h - 1, vpY_ + vpH_ - 1);
    if (ax0 > ax1 || ay0 > ay1) return;
    const int32_t origin = vpDatum_ ? vpX_ : 0;
    const int32_t originY = vpDatum_ ? vpY_ : 0;
    int32_t fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
    if (!toFrame(ax0 - origin, ay0 - originY, fx0, fy0)) return;
    if (!toFrame(ax1 - origin, ay1 - originY, fx1, fy1)) return;

    FocusMark* slot = nullptr;
    for (FocusMark& m : focus_) {
        if (m.valid && m.x == x && m.y == y && m.w == w && m.h == h) {
            slot = &m;
            break;
        }
        if (!slot && !m.valid) slot = &m;
    }
    if (!slot) return;
    *slot = FocusMark{true, focusFill_.shadowed, x, y, w, h,
                      static_cast<int16_t>(std::min(fx0, fx1)), static_cast<int16_t>(std::min(fy0, fy1)),
                      static_cast<int16_t>(std::max(fx0, fx1)), static_cast<int16_t>(std::max(fy0, fy1))};
}

/* A fill over a control's middle has taken it away, unless the fill sits inside
 * the control: that is the control repainting its own contents. A launcher tile
 * does that on every page turn, and a label's background does it all the time. */
void TFT_eSPI::forgetFocusUnder(int32_t x, int32_t y, int32_t w, int32_t h) {
    for (FocusMark& m : focus_) {
        if (!m.valid) continue;
        const int32_t cx = m.x + m.w / 2;
        const int32_t cy = m.y + m.h / 2;
        if (cx < x || cx >= x + w || cy < y || cy >= y + h) continue;
        const bool inside = x >= m.x && y >= m.y && x + w <= m.x + m.w && y + h <= m.y + m.h;
        if (!inside) m.valid = false;
    }
}

void TFT_eSPI::forgetFocus() {
    for (FocusMark& m : focus_) m.valid = false;
    focusFill_ = FocusFill{};
}

int TFT_eSPI::focusTargets(FocusBox* out, int max) const {
    int count = 0;
    const int32_t quarter = FRAME_W * FRAME_H / 4;
    int buttons = 0, cards = 0;
    for (const FocusMark& m : focus_) {
        if (m.valid) ++(m.button ? buttons : cards);
    }
    /* A card is a fill and an outline with no button's shadow. It might be a
     * control -- Memory's cards, Cinnamon's pads, Ludo's seats -- or something
     * to look at, like a quiz's question box or Color Mix's two colours. Beside
     * buttons, one to three of them have always been the second kind, and a stop
     * on them would make the player press past the question to reach an answer.
     * Measured across every screen: the fourth card is where they become a grid
     * of controls. */
    const bool cardsAreControls = buttons == 0 || cards >= MIN_CARD_CONTROLS;
    for (const FocusMark& m : focus_) {
        if (!m.valid || count >= max) continue;
        if (!m.button) {
            if (!cardsAreControls) continue;
            /* Too big, or with a control inside it, it is the panel under them. */
            if (m.w * m.h > quarter) continue;
            bool holdsAnother = false;
            for (const FocusMark& o : focus_) {
                if (!o.valid || &o == &m) continue;
                const int32_t cx = o.x + o.w / 2;
                const int32_t cy = o.y + o.h / 2;
                if (cx >= m.x && cx < m.x + m.w && cy >= m.y && cy < m.y + m.h) holdsAnother = true;
            }
            if (holdsAnother) continue;
        }
        out[count++] = FocusBox{m.fx0, m.fy0, m.fx1, m.fy1, false};
    }
    for (const Icon icon : {Icon::Home, Icon::Profile, Icon::Lock, Icon::Mute, Icon::Settings}) {
        const IconTrack* t = iconTrack(icon);
        if (!t || count >= max) continue;
        int32_t x0 = t->x, y0 = t->y, x1 = t->x + t->w - 1, y1 = t->y + t->h - 1;
        if (icon == Icon::Profile) {
            /* The name as drawn, not the rect it may fill: in landscape that
             * rect runs under the padlock, and the runtime takes a tap within
             * the padlock's slop as Lock before the launcher sees it. So the
             * box also stops short of that, and A presses on the name. */
            const int slot = static_cast<int>(t - icons_[static_cast<int>(Icon::Profile)]);
            const IconTrack& lock = icons_[static_cast<int>(Icon::Lock)][slot];
            /* With air round it: the selector's innermost line is drawn on
             * the box's edge, so a box cut to the glyphs lays it across them. */
            x0 = t->tx0 - NAME_AIR;
            y0 = t->ty0 - NAME_AIR;
            y1 = t->ty1 + NAME_AIR;
            x1 = t->tx1 + NAME_AIR;
            if (lock.w > 0 && lock.y < t->ty1 && lock.y + lock.h > t->ty0) {
                x1 = std::min<int32_t>(x1, lock.x - TOUCH_HIT_SLOP - 1);
            }
            if (x1 < x0) continue;
        }
        int16_t ax = 0, ay = 0, bx = 0, by = 0;
        logicalToFrame(x0, y0, ax, ay);
        logicalToFrame(x1, y1, bx, by);
        out[count++] = FocusBox{std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by), true};
    }
    return count;
}
