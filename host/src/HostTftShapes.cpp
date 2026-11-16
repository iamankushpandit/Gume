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
 * selector reads. See host/shim/TFT_eSPI.h.
 *
 * AT A UI SCALE OF 2 OR MORE, THE ROUND AND SLANTED ONES ARE DRAWN AT THE
 * PANEL'S REAL RESOLUTION. The firmware lays out in logical pixels, and a
 * circle rasterised at 320x240 and then doubled is a staircase of 2x2
 * blocks -- every icon on the RG40XX V was, once text and the mark were
 * smooth. So a circle, a ring, a rounded corner, a slanted line and a
 * triangle are drawn here from their logical geometry times the scale --
 * the radius, the thickness and the corners all scaled -- with
 * anti-aliased edges, through blendReal(), which keeps a redraw a no-op.
 * Rectangles, straight lines and single pixels stay whole blocks, which is
 * already exactly their shape. Two rules keep it the same picture the
 * firmware means:
 *  - a shape covers what TFT_eSPI's covers: logical pixels are filled to
 *    their edges, so a circle of radius r is r + 0.5 pixels to its rim, a
 *    line is one logical pixel thick and a triangle reaches half a pixel
 *    past its vertices' centres. A fill and the outline drawn on it share
 *    their rim, and neighbouring wedges overlap rather than leave a seam;
 *  - at a UI scale of 1 nothing here runs, so a 320x240 window is still
 *    the board's picture, pixel for pixel. */

#include <TFT_eSPI.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

/* Coverage 0..255 of a pixel whose centre is `inside` real pixels inside an
 * edge (negative: outside), with a one-pixel ramp. */
inline int cover(double inside) {
    const double a = inside + 0.5;
    if (a <= 0.0) return 0;
    if (a >= 1.0) return 255;
    return static_cast<int>(a * 255.0 + 0.5);
}

}   // namespace

bool TFT_eSPI::realShapes() const {
    return scale_ >= 2;
}

/* A circle centred on logical pixel (ax, ay), absolute: filled out to r + 0.5
 * logical pixels, or the ring between r - 0.5 and r + 0.5. `quarters` keeps
 * only some quadrants (1 top-left, 2 top-right, 4 bottom-right, 8 bottom-left)
 * -- a rounded rectangle's corners. */
void TFT_eSPI::realCircle(int32_t ax, int32_t ay, int32_t r, bool filled, uint8_t quarters,
                          uint16_t color) {
    beginShape('C', ax, ay, r, filled, quarters);
    const double d = scale_;
    const double cx = (ax + 0.5) * d, cy = (ay + 0.5) * d;
    const double outer = (r + 0.5) * d;
    const double inner = (r - 0.5) * d;
    const int32_t x0 = static_cast<int32_t>(std::floor(cx - outer - 1));
    const int32_t x1 = static_cast<int32_t>(std::ceil(cx + outer + 1));
    const int32_t y0 = static_cast<int32_t>(std::floor(cy - outer - 1));
    const int32_t y1 = static_cast<int32_t>(std::ceil(cy + outer + 1));
    for (int32_t py = y0; py <= y1; ++py) {
        const double qy = py + 0.5 - cy;
        for (int32_t px = x0; px <= x1; ++px) {
            const double qx = px + 0.5 - cx;
            if (quarters != 0x0F) {
                const uint8_t q = qy <= 0 ? (qx <= 0 ? 1 : 2) : (qx <= 0 ? 8 : 4);
                if (!(quarters & q)) continue;
            }
            const double dist = std::sqrt(qx * qx + qy * qy);
            int a = cover(outer - dist);
            if (!filled && r > 0) a -= cover(inner - dist);
            if (a > 0) blendReal(px, py, color, static_cast<uint8_t>(a));
        }
    }
}

/* A line one logical pixel thick between two logical pixels' centres,
 * absolute, with square ends as TFT_eSPI's covers its end pixels whole. */
void TFT_eSPI::realLine(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1, uint16_t color) {
    beginShape('L', ax0, ay0, ax1, ay1);
    const double d = scale_, half = d / 2.0;
    const double x0 = (ax0 + 0.5) * d, y0 = (ay0 + 0.5) * d;
    const double vx = (ax1 - ax0) * d, vy = (ay1 - ay0) * d;
    const double len = std::sqrt(vx * vx + vy * vy);
    if (len < 1e-9) {
        realCircle(ax0, ay0, 0, true, 0x0F, color);
        return;
    }
    const double ux = vx / len, uy = vy / len;
    const int32_t bx0 = static_cast<int32_t>(std::floor(std::min(x0, x0 + vx) - half - 1));
    const int32_t bx1 = static_cast<int32_t>(std::ceil(std::max(x0, x0 + vx) + half + 1));
    const int32_t by0 = static_cast<int32_t>(std::floor(std::min(y0, y0 + vy) - half - 1));
    const int32_t by1 = static_cast<int32_t>(std::ceil(std::max(y0, y0 + vy) + half + 1));
    for (int32_t py = by0; py <= by1; ++py) {
        for (int32_t px = bx0; px <= bx1; ++px) {
            const double qx = px + 0.5 - x0, qy = py + 0.5 - y0;
            const double along = qx * ux + qy * uy;
            const double across = std::fabs(qx * uy - qy * ux);
            const int a = cover(half - across) * cover(std::min(along + half, len + half - along)) / 255;
            if (a > 0) blendReal(px, py, color, static_cast<uint8_t>(a));
        }
    }
}

/* A filled triangle on three logical pixels' centres, absolute, grown by
 * half a logical pixel all round -- what TFT_eSPI's inclusive fill covers --
 * and kept inside its vertices' box grown the same, so a sharp corner does
 * not run on past its point. */
void TFT_eSPI::realTriangle(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1, int32_t ax2,
                            int32_t ay2, uint16_t color) {
    const double d = scale_, half = d / 2.0;
    const double px_[3] = {(ax0 + 0.5) * d, (ax1 + 0.5) * d, (ax2 + 0.5) * d};
    const double py_[3] = {(ay0 + 0.5) * d, (ay1 + 0.5) * d, (ay2 + 0.5) * d};
    beginShape('T', ax0, ay0, ax1, ay1, ax2, ay2);
    const double area2 = (px_[1] - px_[0]) * (py_[2] - py_[0]) - (py_[1] - py_[0]) * (px_[2] - px_[0]);
    if (std::fabs(area2) < 1e-6) {   // a line, as TFT_eSPI draws a flat triangle
        realLine(ax0, ay0, ax1, ay1, color);
        realLine(ax1, ay1, ax2, ay2, color);
        realLine(ax2, ay2, ax0, ay0, color);
        return;
    }
    double nx[3], ny[3], c[3];   // inward unit normals, and each edge's offset
    for (int e = 0; e < 3; ++e) {
        const int a = e, b = (e + 1) % 3, o = (e + 2) % 3;
        double ex = px_[b] - px_[a], ey = py_[b] - py_[a];
        const double el = std::sqrt(ex * ex + ey * ey);
        nx[e] = -ey / el;
        ny[e] = ex / el;
        if ((px_[o] - px_[a]) * nx[e] + (py_[o] - py_[a]) * ny[e] < 0) {
            nx[e] = -nx[e];
            ny[e] = -ny[e];
        }
        c[e] = px_[a] * nx[e] + py_[a] * ny[e];
    }
    const double minX = std::min({px_[0], px_[1], px_[2]}) - half;
    const double maxX = std::max({px_[0], px_[1], px_[2]}) + half;
    const double minY = std::min({py_[0], py_[1], py_[2]}) - half;
    const double maxY = std::max({py_[0], py_[1], py_[2]}) + half;
    for (int32_t py = static_cast<int32_t>(std::floor(minY - 1));
         py <= static_cast<int32_t>(std::ceil(maxY + 1)); ++py) {
        const double qy = py + 0.5;
        for (int32_t px = static_cast<int32_t>(std::floor(minX - 1));
             px <= static_cast<int32_t>(std::ceil(maxX + 1)); ++px) {
            const double qx = px + 0.5;
            double inside = std::min({qx - minX, maxX - qx, qy - minY, maxY - qy});
            for (int e = 0; e < 3; ++e) {
                inside = std::min(inside, qx * nx[e] + qy * ny[e] - c[e] + half);
            }
            const int a = cover(inside);
            if (a > 0) blendReal(px, py, color, static_cast<uint8_t>(a));
        }
    }
}

void TFT_eSPI::drawArcReal(int32_t cx, int32_t cy, int32_t radius, int32_t fromDeg,
                           int32_t toDeg, uint16_t color) {
    const int32_t ax = vpDatum_ ? cx + vpX_ : cx, ay = vpDatum_ ? cy + vpY_ : cy;
    beginShape('A', ax, ay, radius, fromDeg, toDeg);
    const double d = scale_, pi = 3.14159265358979323846;
    const double ox = (ax + 0.5) * d, oy = (ay + 0.5) * d;
    const double outer = (radius + 0.5) * d, inner = (radius - 0.5) * d;
    const double from = fromDeg * pi / 180.0, to = toDeg * pi / 180.0;
    const int32_t x0 = static_cast<int32_t>(std::floor(ox - outer - 1));
    const int32_t x1 = static_cast<int32_t>(std::ceil(ox + outer + 1));
    const int32_t y0 = static_cast<int32_t>(std::floor(oy - outer - 1));
    const int32_t y1 = static_cast<int32_t>(std::ceil(oy + outer + 1));
    for (int32_t py = y0; py <= y1; ++py) {
        for (int32_t px = x0; px <= x1; ++px) {
            const double qx = px + 0.5 - ox, qy = py + 0.5 - oy;
            double angle = std::atan2(qy, qx);   // y grows downwards: clockwise
            while (angle < from) angle += 2 * pi;
            while (angle > from + 2 * pi) angle -= 2 * pi;
            if (angle > to) continue;
            const double dist = std::sqrt(qx * qx + qy * qy);
            const int a = cover(outer - dist) - cover(inner - dist);
            if (a > 0) blendReal(px, py, color, static_cast<uint8_t>(a));
        }
    }
}

/* Whole logical pixels, with no mark noted: a rounded rectangle's sides. */
void TFT_eSPI::blockRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    for (int32_t j = 0; j < h; ++j) {
        for (int32_t i = 0; i < w; ++i) plot(x + i, y + j, color);
    }
}

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
    if (realShapes()) {
        realCircle(vpDatum_ ? x + vpX_ : x, vpDatum_ ? y + vpY_ : y, r, false, 0x0F, c);
        return;
    }
    plot(x, y + r, c);
    plot(x, y - r, c);
    plot(x + r, y, c);
    plot(x - r, y, c);
    circleQuarters(x, y, r, 0x0F, c);
}

void TFT_eSPI::fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    if (r < 0) return;
    noteIconPrim(Prim::CircleFill, x - r, y - r, x + r, y + r);
    if (realShapes()) {
        realCircle(vpDatum_ ? x + vpX_ : x, vpDatum_ ? y + vpY_ : y, r, true, 0x0F,
                   static_cast<uint16_t>(color));
        return;
    }
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
    if (realShapes() && r > 0) {
        const int32_t ax = vpDatum_ ? x + vpX_ : x, ay = vpDatum_ ? y + vpY_ : y;
        realCircle(ax + r, ay + r, r, false, 1, c);
        realCircle(ax + w - r - 1, ay + r, r, false, 2, c);
        realCircle(ax + w - r - 1, ay + h - r - 1, r, false, 4, c);
        realCircle(ax + r, ay + h - r - 1, r, false, 8, c);
        return;
    }
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
    if (realShapes() && r > 0) {
        blockRect(x, y + r, r, h - 2 * r, c);
        blockRect(x + w - r, y + r, r, h - 2 * r, c);
        const int32_t ax = vpDatum_ ? x + vpX_ : x, ay = vpDatum_ ? y + vpY_ : y;
        realCircle(ax + r, ay + r, r, true, 1, c);
        realCircle(ax + w - r - 1, ay + r, r, true, 2, c);
        realCircle(ax + w - r - 1, ay + h - r - 1, r, true, 4, c);
        realCircle(ax + r, ay + h - r - 1, r, true, 8, c);
        return;
    }
    fillCircleHalves(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, c);
    fillCircleHalves(x + r, y + r, r, 2, h - 2 * r - 1, c);
}

void TFT_eSPI::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    noteIconPrim(Prim::Line, x0, y0, x1, y1);
    const uint16_t c = static_cast<uint16_t>(color);
    if (realShapes() && x0 != x1 && y0 != y1) {   // straight ones are blocks already
        const int32_t ox = vpDatum_ ? vpX_ : 0, oy = vpDatum_ ? vpY_ : 0;
        realLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, c);
        return;
    }
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
    if (realShapes()) {
        const int32_t ox = vpDatum_ ? vpX_ : 0, oy = vpDatum_ ? vpY_ : 0;
        realTriangle(x0 + ox, y0 + oy, x1 + ox, y1 + oy, x2 + ox, y2 + oy,
                     static_cast<uint16_t>(color));
        return;
    }
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
