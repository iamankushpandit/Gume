// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "GoDrawArt.h"

#include <string.h>

/* Board geometry and every pixel of Go.
 *
 * Nothing on the board repaints wholesale during play. Each point has a
 * dirty bit and repaintPoint() paints its whole cell idempotently -- the
 * wood, the grid through it, a star, the stone, the last-move marker, the
 * ghost, a dead cross, a territory mark -- so a move repaints the two to
 * four cells it touched. The panel is parts, each repainted when what it
 * shows changed. A full repaint happens on entering the screen, a new game,
 * Undo, the marking phase and the result card. The lobby's rectangles are
 * here with the rest of the geometry (GoLayout.cpp); the panel is drawn in
 * GoPanel.cpp and the lobby in GoNet.cpp.
 *
 * Every rectangle is derived from the board's step and margin, which are
 * the only two numbers that change between 9x9 and 19x19. */

using namespace GoArt;

// ---- the board -----------------------------------------------------------------------

void GoGame::drawStone(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t r, uint8_t colour,
                       bool faded) const {
    uint16_t fill = colour == Go::BLACK ? blackFill() : whiteFill();
    uint16_t edge = colour == Go::BLACK ? blackEdge() : whiteEdge();
    if (faded) {
        fill = mix(fill, wood());
        edge = mix(edge, wood());
    } else if (r >= 6) {
        tft.fillCircle(cx + 1, cy + 2, r, Ui::shade(wood(), 55));   // a little shadow
    }
    tft.fillCircle(cx, cy, r, fill);
    tft.drawCircle(cx, cy, r, edge);
    if (r >= 8 && !faded) {
        tft.fillCircle(cx - r / 3, cy - r / 3, r / 4,
                       colour == Go::BLACK ? Ui::shade(fill, 160) : Ui::rgb(255, 255, 255));
    }
}

/* Everything that sits on a point, drawn over a cell that already shows the
 * wood and the grid. Idempotent: what it paints depends only on the state. */
namespace {
struct PointArt {
    bool stone;
    uint8_t colour;
};
}   // namespace

void GoGame::repaintPoint(Ui::Renderer& tft, uint16_t p) const {
    const Rect c = pointRect(p);
    const int16_t cx = pointX(p);
    const int16_t cy = pointY(p);
    tft.fillRect(c.x, c.y, c.w, c.h, wood());
    /* An outline in the wood's own colour, under the grid, so nothing on the
     * panel changes: a fill and an outline of one rectangle is what the
     * d-pad's selector stops at, so it moves point to point and A plays. */
    tft.drawRect(c.x, c.y, c.w, c.h, wood());
    /* The grid through the cell, no further than the outer lines. */
    const int16_t gx0 = pointX(0);
    const int16_t gy0 = pointY(0);
    const int16_t gx1 = pointX(static_cast<uint16_t>(state_.n - 1));
    const int16_t gy1 = pointY(static_cast<uint16_t>(Go::points(state_.n) - 1));
    const int16_t hx0 = c.x < gx0 ? gx0 : c.x;
    const int16_t hx1 = static_cast<int16_t>(c.x + c.w - 1 > gx1 ? gx1 : c.x + c.w - 1);
    const int16_t vy0 = c.y < gy0 ? gy0 : c.y;
    const int16_t vy1 = static_cast<int16_t>(c.y + c.h - 1 > gy1 ? gy1 : c.y + c.h - 1);
    tft.drawFastHLine(hx0, cy, static_cast<int16_t>(hx1 - hx0 + 1), grid());
    tft.drawFastVLine(cx, vy0, static_cast<int16_t>(vy1 - vy0 + 1), grid());
    if (isStar(p)) tft.fillCircle(cx, cy, big() ? 1 : 2, grid());

    const int16_t r = static_cast<int16_t>(step() * 46 / 100);
    const bool showTerritory = phase_ == Phase::Marking || phase_ == Phase::Over;
    const bool dead = showTerritory && dead_[p] != 0;
    const uint8_t colour = state_.at[p];
    if (colour != Go::EMPTY) {
        drawStone(tft, cx, cy, r, colour, dead);
        if (dead) {
            const int16_t k = static_cast<int16_t>(r - 1);
            tft.drawLine(cx - k, cy - k, cx + k, cy + k, marker());
            tft.drawLine(cx - k, cy + k, cx + k, cy - k, marker());
        } else if (p == state_.last && phase_ != Phase::Over) {
            tft.drawCircle(cx, cy, r / 2 > 2 ? r / 2 : 2, marker());
        }
    }
    if (showTerritory && (colour == Go::EMPTY || dead) && own_[p] != Go::EMPTY) {
        const int16_t t = static_cast<int16_t>(step() * 16 / 100 > 1 ? step() * 16 / 100 : 1);
        tft.fillRect(cx - t, cy - t, 2 * t + 1, 2 * t + 1,
                     own_[p] == Go::BLACK ? blackFill() : whiteFill());
    }
    if (p == ghost_ && phase_ == Phase::Play) {
        const uint16_t fill = mix(state_.toMove == Go::BLACK ? blackFill() : whiteFill(), wood());
        tft.fillCircle(cx, cy, r, fill);
        tft.drawCircle(cx, cy, r, hilite());
        const int16_t a = static_cast<int16_t>(r + 1);
        const int16_t b = static_cast<int16_t>(r + 4);
        tft.drawFastHLine(cx - b, cy, b - a, hilite());
        tft.drawFastHLine(cx + a, cy, b - a, hilite());
        tft.drawFastVLine(cx, cy - b, b - a, hilite());
        tft.drawFastVLine(cx, cy + a, b - a, hilite());
    }
}

void GoGame::drawBoard(Ui::Renderer& tft) const {
    const Rect b = boardRect();
    tft.fillRect(b.x, b.y, b.w, b.h, wood());
    const uint16_t total = Go::points(state_.n);
    for (uint16_t p = 0; p < total; ++p) {
        /* Each cell whole, which draws the grid as it goes. Eighty-one or
         * three hundred and sixty-one small fills on a full repaint only. */
        repaintPoint(tft, p);
    }
}

// ---- the result --------------------------------------------------------------------------

void GoGame::drawResult(Ui::Renderer& tft) const {
    const Rect r = resultRect();
    tft.fillRoundRect(r.x + 2, r.y + Ui::BUTTON_SHADOW_DY, r.w, r.h, 8, Ui::shade(Ui::bg(), 60));
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, Ui::warning());

    char title[32];
    const uint8_t w = state_.winner;
    const uint8_t l = Go::other(w);
    if (Go::isCapture(state_.rules)) {
        snprintf(title, sizeof(title), "%.10s wins!", sideName(w));
    } else {
        const int16_t by = static_cast<int16_t>(score_.half[w] - score_.half[l]);
        snprintf(title, sizeof(title), "%.10s wins by %d.%d", sideName(w), by / 2, (by % 2) * 5);
    }
    Ui::drawLabel(tft, Rect{static_cast<int16_t>(r.x + 8), static_cast<int16_t>(r.y + 6),
                            static_cast<int16_t>(r.w - 16), 22},
                  title, Ui::warning(), 4, Align::Center);

    const int16_t colB = static_cast<int16_t>(r.x + 150);
    const int16_t colW = static_cast<int16_t>(r.x + 215);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.drawString("Black", colB, r.y + 40, 2);
    tft.drawString("White", colW, r.y + 40, 2);

    const char* labels[3];
    uint16_t b[3];
    uint16_t wv[3];
    uint8_t rows = 0;
    if (Go::isCapture(state_.rules)) {
        labels[rows] = "Stones captured";
        b[rows] = state_.captured[Go::BLACK];
        wv[rows] = state_.captured[Go::WHITE];
        ++rows;
        labels[rows] = "Needed to win";
        b[rows] = wv[rows] = Go::captureTarget(state_.rules);
        ++rows;
    } else if (state_.rules == Go::Rules::Area) {
        labels[0] = "Stones on the board";
        b[0] = score_.stones[Go::BLACK];
        wv[0] = score_.stones[Go::WHITE];
        labels[1] = "Points surrounded";
        b[1] = score_.territory[Go::BLACK];
        wv[1] = score_.territory[Go::WHITE];
        rows = 2;
    } else {
        labels[0] = "Points surrounded";
        b[0] = score_.territory[Go::BLACK];
        wv[0] = score_.territory[Go::WHITE];
        labels[1] = "Prisoners";
        b[1] = score_.prisoners[Go::BLACK];
        wv[1] = score_.prisoners[Go::WHITE];
        rows = 2;
    }
    int16_t y = static_cast<int16_t>(r.y + 60);
    char n[12];
    for (uint8_t i = 0; i < rows; ++i) {
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(labels[i], r.x + 14, y, 1);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(Ui::text(), Ui::surface());
        snprintf(n, sizeof(n), "%u", static_cast<unsigned>(b[i]));
        tft.drawString(n, colB, y, 2);
        snprintf(n, sizeof(n), "%u", static_cast<unsigned>(wv[i]));
        tft.drawString(n, colW, y, 2);
        y = static_cast<int16_t>(y + 20);
    }
    if (!Go::isCapture(state_.rules)) {
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString("Komi", r.x + 14, y, 1);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString("-", colB, y, 2);
        snprintf(n, sizeof(n), "%d.%d", Go::KOMI_HALF / 2, (Go::KOMI_HALF % 2) * 5);
        tft.drawString(n, colW, y, 2);
        y = static_cast<int16_t>(y + 20);
        tft.drawFastHLine(r.x + 14, y - 4, r.w - 28, Ui::outline());
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString("Total", r.x + 14, y + 6, 2);
        tft.setTextDatum(MC_DATUM);
        snprintf(n, sizeof(n), "%d%s", score_.half[Go::BLACK] / 2,
                 score_.half[Go::BLACK] % 2 ? ".5" : "");
        tft.setTextColor(w == Go::BLACK ? Ui::success() : Ui::text(), Ui::surface());
        tft.drawString(n, colB, y + 6, 4);
        snprintf(n, sizeof(n), "%d%s", score_.half[Go::WHITE] / 2,
                 score_.half[Go::WHITE] % 2 ? ".5" : "");
        tft.setTextColor(w == Go::WHITE ? Ui::success() : Ui::text(), Ui::surface());
        tft.drawString(n, colW, y + 6, 4);
    }
    tft.setTextDatum(TL_DATUM);
    Ui::drawButton(tft, resultButtonRect(0), mode_ == Mode::Remote ? "Lobby" : "Play again",
                   Ui::panel(), Ui::outline(), Ui::text());
    Ui::drawButton(tft, resultButtonRect(1), "Done", Ui::panel(), Ui::outline(), Ui::text());
}

// ---- render -------------------------------------------------------------------------------

void GoGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    if (mode_ == Mode::Lobby) {
        lobbyStale_ = true;
        return;
    }
    drawBoard(tft);
    memset(dirty_, 0, sizeof(dirty_));
    anyDirty_ = false;
    turnStale_ = capturesStale_ = infoStale_ = buttonsStale_ = true;
    drawPanel(tft);
    if (phase_ == Phase::Over) {
        drawResult(tft);
        resultStale_ = false;
    }
    pausePainted_ = false;
    drawPause(tft);
}

void GoGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    if (mode_ == Mode::Lobby) {
        if (lobbyStale_) {
            renderLobby(host);
            lobbyStale_ = false;
        }
        return;
    }
    if (anyDirty_) {
        pausePainted_ = false;   // a point under the card
        const uint16_t total = Go::points(state_.n);
        for (uint16_t p = 0; p < total; ++p) {
            if (dirty_[p >> 3] & (1U << (p & 7))) repaintPoint(tft, p);
        }
        memset(dirty_, 0, sizeof(dirty_));
        anyDirty_ = false;
        if (big()) infoStale_ = true;   // the magnifier shows the board
    }
    drawPanel(tft);
    if (phase_ == Phase::Over && resultStale_) {
        drawResult(tft);
        resultStale_ = false;
    }
    drawPause(tft);
}

#if GUME_HAS_NAV_KEYS
/* The ring crosses the grid lines at the point's edge, so painting the wood
 * back would cut them: the point is drawn again whole. */
bool GoGame::repaintUnderRing(AppContext& host, const Rect& box) {
    if (mode_ == Mode::Lobby || big()) return false;
    const Rect b = boardRect();
    if (box.x < b.x || box.y < b.y || box.x + box.w > b.x + b.w || box.y + box.h > b.y + b.h) {
        return false;
    }
    repaintPoint(host.display(), pointAt(static_cast<int16_t>(box.x + box.w / 2),
                                         static_cast<int16_t>(box.y + box.h / 2)));
    return true;
}
#endif
