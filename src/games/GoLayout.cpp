// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "GoDrawArt.h"

/* Go's geometry: the board, its points, the panel, the lobby's rectangles and
 * the result card. Every rectangle is derived from the board's step and
 * margin, which are the only two numbers that change between 9x9 and 19x19.
 * The constants are in GoDrawArt.h; the pixels in GoDraw.cpp and GoPanel.cpp. */

using namespace GoArt;

// ---- geometry ---------------------------------------------------------------------

int16_t GoGame::step() const { return big() ? STEP_BIG : STEP_SMALL; }
int16_t GoGame::margin() const { return big() ? MARGIN_BIG : MARGIN_SMALL; }

Rect GoGame::boardRect() const {
    const int16_t box = big() ? BOX_BIG : BOX_SMALL;
    return Rect{BX, BY, box, box};
}

int16_t GoGame::pointX(uint16_t p) const {
    return static_cast<int16_t>(BX + margin() + Go::colOf(state_.n, p) * step());
}

int16_t GoGame::pointY(uint16_t p) const {
    return static_cast<int16_t>(BY + margin() + Go::rowOf(state_.n, p) * step());
}

Rect GoGame::pointRect(uint16_t p) const {
    /* The cell: a step square centred on the point, clipped to the board so
     * an edge point's cell does not reach past the wood. */
    const Rect b = boardRect();
    const int16_t half = static_cast<int16_t>(step() / 2);
    int16_t x0 = static_cast<int16_t>(pointX(p) - half);
    int16_t y0 = static_cast<int16_t>(pointY(p) - half);
    int16_t x1 = static_cast<int16_t>(x0 + step());
    int16_t y1 = static_cast<int16_t>(y0 + step());
    if (x0 < b.x) x0 = b.x;
    if (y0 < b.y) y0 = b.y;
    if (x1 > b.x + b.w) x1 = static_cast<int16_t>(b.x + b.w);
    if (y1 > b.y + b.h) y1 = static_cast<int16_t>(b.y + b.h);
    return Rect{x0, y0, static_cast<int16_t>(x1 - x0), static_cast<int16_t>(y1 - y0)};
}

uint16_t GoGame::pointAt(int16_t x, int16_t y) const {
    if (!boardRect().contains(x, y, TOUCH_HIT_SLOP)) return Go::NO_POINT;
    /* The nearest point, never none: on a board every tap means somewhere,
     * and the ghost-then-confirm is what makes that safe. */
    int16_t col = static_cast<int16_t>((x - BX - margin() + step() / 2) / step());
    int16_t row = static_cast<int16_t>((y - BY - margin() + step() / 2) / step());
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col >= state_.n) col = static_cast<int16_t>(state_.n - 1);
    if (row >= state_.n) row = static_cast<int16_t>(state_.n - 1);
    return Go::point(state_.n, static_cast<uint8_t>(row), static_cast<uint8_t>(col));
}

bool GoGame::isStar(uint16_t p) const {
    const uint8_t r = Go::rowOf(state_.n, p);
    const uint8_t c = Go::colOf(state_.n, p);
    if (big()) {
        return (r == 3 || r == 9 || r == 15) && (c == 3 || c == 9 || c == 15);
    }
    return ((r == 2 || r == 6) && (c == 2 || c == 6)) || (r == 4 && c == 4);
}

int16_t GoGame::panelX() const {
    return static_cast<int16_t>(BX + boardRect().w + PANEL_GAP);
}

int16_t GoGame::panelW() const {
    return static_cast<int16_t>(GAME_CANVAS_WIDTH - panelX() - 4);
}

Rect GoGame::turnRect() const {
    return big() ? Rect{panelX(), BTURN_Y, panelW(), BTURN_H}
                 : Rect{panelX(), TURN_Y, panelW(), TURN_H};
}

Rect GoGame::capturesRect() const {
    return Rect{panelX(), CAPT_Y, panelW(), CAPT_H};
}

Rect GoGame::infoRect() const {
    if (big()) {
        const int16_t w = MAG_H;
        return Rect{static_cast<int16_t>(panelX() + (panelW() - w) / 2), MAG_Y, w, MAG_H};
    }
    return Rect{panelX(), INFO_Y, panelW(), INFO_H};
}

Rect GoGame::placeRect() const {
    return Rect{panelX(), PLACE_Y, panelW(), PLACE_H};
}

Rect GoGame::passRect() const {
    if (big()) {
        return Rect{panelX(), BPASS_Y, static_cast<int16_t>(panelW() / 2 - 2), BPASS_H};
    }
    return Rect{panelX(), PASS_Y, panelW(), PASS_H};
}

Rect GoGame::undoRect() const {
    if (big()) {
        return Rect{static_cast<int16_t>(panelX() + panelW() / 2 + 2), BPASS_Y,
                    static_cast<int16_t>(panelW() / 2 - 2), BPASS_H};
    }
    return Rect{panelX(), UNDO_Y, panelW(), UNDO_H};
}

Rect GoGame::actionRect() const {
    return big() ? Rect{panelX(), BACTION_Y, panelW(), BACTION_H}
                 : Rect{panelX(), ACTION_Y, panelW(), ACTION_H};
}

Rect GoGame::lobbyChipRect(uint8_t index, uint8_t count) {
    const int16_t w = static_cast<int16_t>((GAME_CANVAS_WIDTH - 16 - 5 * (count - 1)) / count);
    return Rect{static_cast<int16_t>(8 + index * (w + 5)), CHIP_Y, w, CHIP_H};
}

Rect GoGame::lobbyRowRect(uint8_t row) {
    return Rect{8, static_cast<int16_t>(ROW_Y + row * ROW_PITCH), GAME_CANVAS_WIDTH - 16, ROW_H};
}

Rect GoGame::resultRect() const { return RESULT_RECT; }

Rect GoGame::resultButtonRect(uint8_t index) const {
    const Rect r = RESULT_RECT;
    return Rect{static_cast<int16_t>(r.x + 14 + index * 122), static_cast<int16_t>(r.y + r.h - 36),
                110, 26};
}

void GoGame::coordinate(uint16_t p, char* out, size_t len) const {
    if (p == Go::NO_POINT || p == Go::PASS) {
        snprintf(out, len, "-");
        return;
    }
    snprintf(out, len, "%c%u", COLS[Go::colOf(state_.n, p)],
             static_cast<unsigned>(state_.n - Go::rowOf(state_.n, p)));
}
