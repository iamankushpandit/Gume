// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The panel shim's shapes: spans, fills, outlines, circles, rounded
 * rectangles, lines and triangles, as TFT_eSPI draws them. Split from
 * HostTft.cpp, which keeps the frame, the text and the marks the d-pad's
 * selector reads. See host/shim/TFT_eSPI.h. */

#include <TFT_eSPI.h>

#include <algorithm>
#include <cstdlib>

void TFT_eSPI::span(int32_t x0, int32_t x1, int32_t y, uint16_t color) {
    if (x1 < x0) std::swap(x0, x1);
    for (int32_t x = x0; x <= x1; ++x) plot(x, y, color);
}

void TFT_eSPI::fillScreen(uint32_t color) {
    /* Like the driver: the whole panel, ignoring any viewport offset but
     * still clipped to it. */
    const bool datum = vpDatum_;
    vpDatum_ = false;
    forgetMarks();
    for (int32_t y = vpY_; y < vpY_ + vpH_; ++y) {
        for (int32_t x = vpX_; x < vpX_ + vpW_; ++x) plot(x, y, static_cast<uint16_t>(color));
    }
    vpDatum_ = datum;
}

void TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t color) {
    plot(x, y, static_cast<uint16_t>(color));
}

void TFT_eSPI::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) {
    if (w <= 0) return;
    span(x, x + w - 1, y, static_cast<uint16_t>(color));
}

void TFT_eSPI::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
    for (int32_t i = 0; i < h; ++i) plot(x, y + i, static_cast<uint16_t>(color));
}

/* A square-cornered fill and outline of one rectangle is a control too, as it
 * is to the firmware's own recorder (ui/FocusRecorder.cpp, which notes
 * fillRect() and drawRect() at radius 0): a board's squares, a piano's keys
 * and a puzzle's cells are drawn that way. */
void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    fillRectQuiet(x, y, w, h, color);
    noteFocusFill(x, y, w, h);
}

void TFT_eSPI::fillRectQuiet(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    forgetMarksUnder(x, y, w, h);
    noteTabSquare(x, y, w, h);
    for (int32_t j = 0; j < h; ++j) span(x, x + w - 1, y + j, static_cast<uint16_t>(color));
}

void TFT_eSPI::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    noteFocusOutline(x, y, w, h);
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

/* Midpoint circle, one quadrant arc per bit of `corners`:
 * 1 top-left, 2 top-right, 4 bottom-right, 8 bottom-left. */
void TFT_eSPI::circleQuarters(int32_t x0, int32_t y0, int32_t r, uint8_t corners,
                              uint16_t color) {
    int32_t f = 1 - r;
    int32_t ddx = 1;
    int32_t ddy = -2 * r;
    int32_t x = 0;
    int32_t y = r;
    while (x < y) {
        if (f >= 0) {
            --y;
            ddy += 2;
            f += ddy;
        }
        ++x;
        ddx += 2;
        f += ddx;
        if (corners & 4) { plot(x0 + x, y0 + y, color); plot(x0 + y, y0 + x, color); }
        if (corners & 2) { plot(x0 + x, y0 - y, color); plot(x0 + y, y0 - x, color); }
        if (corners & 8) { plot(x0 - y, y0 + x, color); plot(x0 - x, y0 + y, color); }
        if (corners & 1) { plot(x0 - y, y0 - x, color); plot(x0 - x, y0 - y, color); }
    }
}

/* Filled halves: bit 1 the right half, bit 2 the left, each stretched
 * vertically by `stretch` -- which is what a rounded rectangle's sides are. */
void TFT_eSPI::fillCircleHalves(int32_t x0, int32_t y0, int32_t r, uint8_t sides,
                                int32_t stretch, uint16_t color) {
    int32_t f = 1 - r;
    int32_t ddx = 1;
    int32_t ddy = -2 * r;
    int32_t x = 0;
    int32_t y = r;
    while (x < y) {
        if (f >= 0) {
            --y;
            ddy += 2;
            f += ddy;
        }
        ++x;
        ddx += 2;
        f += ddx;
        if (sides & 1) {
            drawFastVLine(x0 + x, y0 - y, 2 * y + 1 + stretch, color);
            drawFastVLine(x0 + y, y0 - x, 2 * x + 1 + stretch, color);
        }
        if (sides & 2) {
            drawFastVLine(x0 - x, y0 - y, 2 * y + 1 + stretch, color);
            drawFastVLine(x0 - y, y0 - x, 2 * x + 1 + stretch, color);
        }
    }
}

void TFT_eSPI::drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    if (r < 0) return;
    noteIconPrim(Prim::CircleOutline, x - r, y - r, x + r, y + r);
    const uint16_t c = static_cast<uint16_t>(color);
    plot(x, y + r, c);
    plot(x, y - r, c);
    plot(x + r, y, c);
    plot(x - r, y, c);
    circleQuarters(x, y, r, 0x0F, c);
}

void TFT_eSPI::fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    if (r < 0) return;
    noteIconPrim(Prim::CircleFill, x - r, y - r, x + r, y + r);
    drawFastVLine(x, y - r, 2 * r + 1, color);
    fillCircleHalves(x, y, r, 3, 0, static_cast<uint16_t>(color));
}

void TFT_eSPI::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                             uint32_t color) {
    if (w <= 0 || h <= 0) return;
    noteScrollOutline(x, y, w, h, r);
    noteFocusOutline(x, y, w, h);
    noteIconPrim(Prim::RoundOutline, x, y, x + w - 1, y + h - 1);
    r = std::min(r, std::min(w, h) / 2);
    if (r < 0) r = 0;
    drawFastHLine(x + r, y, w - 2 * r, color);
    drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
    drawFastVLine(x, y + r, h - 2 * r, color);
    drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
    const uint16_t c = static_cast<uint16_t>(color);
    circleQuarters(x + r, y + r, r, 1, c);
    circleQuarters(x + w - r - 1, y + r, r, 2, c);
    circleQuarters(x + w - r - 1, y + h - r - 1, r, 4, c);
    circleQuarters(x + r, y + h - r - 1, r, 8, c);
}

void TFT_eSPI::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                             uint32_t color) {
    if (w <= 0 || h <= 0) return;
    noteScrollFill(x, y, w, h, r);
    noteIconPrim(Prim::RoundFill, x, y, x + w - 1, y + h - 1);
    noteTabFill(x, y, w, h, r);
    noteFocusFill(x, y, w, h);
    r = std::min(r, std::min(w, h) / 2);
    if (r < 0) r = 0;
    fillRectQuiet(x + r, y, w - 2 * r, h, color);
    const uint16_t c = static_cast<uint16_t>(color);
    fillCircleHalves(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, c);
    fillCircleHalves(x + r, y + r, r, 2, h - 2 * r - 1, c);
}

void TFT_eSPI::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    noteIconPrim(Prim::Line, x0, y0, x1, y1);
    const uint16_t c = static_cast<uint16_t>(color);
    const int32_t dx = std::abs(x1 - x0);
    const int32_t dy = -std::abs(y1 - y0);
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        plot(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void TFT_eSPI::drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                            int32_t y2, uint32_t color) {
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

void TFT_eSPI::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                            int32_t y2, uint32_t color) {
    noteIconPrim(Prim::Triangle, std::min({x0, x1, x2}), std::min({y0, y1, y2}),
                 std::max({x0, x1, x2}), std::max({y0, y1, y2}));
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    const uint16_t c = static_cast<uint16_t>(color);
    if (y0 == y2) {
        span(std::min({x0, x1, x2}), std::max({x0, x1, x2}), y0, c);
        return;
    }
    /* Scanline fill with edges interpolated in integers, so the fill meets
     * the outline drawTriangle() would draw. */
    for (int32_t y = y0; y <= y2; ++y) {
        const int32_t xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int32_t xb;
        if (y < y1 || y1 == y2) {
            xb = (y1 == y0) ? x1 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        } else {
            xb = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        }
        span(xa, xb, y, c);
    }
}
