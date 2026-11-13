// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ui/LauncherIconArt.h"

#include <math.h>   // cosf()/sinf(), for ellipseRing()

namespace LauncherIconArt {

/* Through Ui::rgb rather than hand-written RGB565 literals: an earlier draft
 * carried 0x2418 in a comment claiming to be rgb(36, 132, 204), and it was not
 * -- the real value is 0x2439. Let the packer do it. */
uint16_t snow()  { return Ui::rgb(247, 250, 253); }   // the silhouette
uint16_t ink()   { return Ui::rgb(26, 34, 48); }      // detail on top of snow
uint16_t steel() { return Ui::rgb(158, 172, 188); }   // a second, quieter solid
uint16_t amber() { return Ui::rgb(255, 196, 64); }
uint16_t coral() { return Ui::rgb(255, 110, 100); }
uint16_t mint()  { return Ui::rgb(88, 220, 158); }
uint16_t sky()   { return Ui::rgb(96, 190, 255); }

// ---- stroke helpers ----

void strokeH(Ui::Renderer& tft, int16_t x, int16_t y, int16_t w, uint16_t color) {
    tft.fillRect(x, y, w, 2, color);
}

void strokeV(Ui::Renderer& tft, int16_t x, int16_t y, int16_t h, uint16_t color) {
    tft.fillRect(x, y, 2, h, color);
}

/* A diagonal with body. drawLine is one pixel wide, and a one-pixel diagonal is
 * the worst case on this panel, so every diagonal here is drawn as a small
 * bundle of offset lines. */
void strokeDiag(Ui::Renderer& tft, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                uint16_t color) {
    for (int16_t t = -1; t <= 1; ++t) {
        tft.drawLine(x0, static_cast<int16_t>(y0 + t), x1, static_cast<int16_t>(y1 + t), color);
        tft.drawLine(static_cast<int16_t>(x0 + t), y0, static_cast<int16_t>(x1 + t), y1, color);
    }
}

/** The workhorse silhouette: a rounded slab centred on (cx, cy). */
void plate(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t w, int16_t h,
           uint16_t color) {
    tft.fillRoundRect(static_cast<int16_t>(cx - w / 2), static_cast<int16_t>(cy - h / 2),
                      w, h, RADIUS, color);
}

/** A two-pixel ring, so circles match the stroke weight of everything else. */
void ring(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
    tft.drawCircle(cx, cy, radius, color);
    tft.drawCircle(cx, cy, static_cast<int16_t>(radius - 1), color);
}

/* Unit five-point star, x100, interleaved outer/inner from twelve o'clock.
 * A table, so the icons cost no runtime trigonometry. */
constexpr int16_t STAR_XY[10][2] = {
    {   0, -100}, {  22,  -31}, {  95,  -31}, {  36,   12}, {  59,   81},
    {   0,   38}, { -59,   81}, { -36,   12}, { -95,  -31}, { -22,  -31},
};

void fillStar(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
    for (uint8_t i = 0; i < 10; ++i) {
        const uint8_t j = static_cast<uint8_t>((i + 1) % 10);
        tft.fillTriangle(cx, cy,
                         static_cast<int16_t>(cx + STAR_XY[i][0] * radius / 100),
                         static_cast<int16_t>(cy + STAR_XY[i][1] * radius / 100),
                         static_cast<int16_t>(cx + STAR_XY[j][0] * radius / 100),
                         static_cast<int16_t>(cy + STAR_XY[j][1] * radius / 100),
                         color);
    }
}

/* Upper half of a circle, from the circle equation.
 *
 * The Wi-Fi tile used to draw whole circles and paint their lower halves out
 * with a rectangle in the tile colour. The mask was three pixels short, so the
 * base of the outer ring survived at cy + 24 -- outside a 46px landscape tile.
 * Explicit geometry cannot spill the way a mask can miss. Integer square root,
 * because there is no reason to pull floating point in for a 16px radius. */
void arcTop(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
    for (int16_t dx = -radius; dx <= radius; ++dx) {
        const int32_t inside = static_cast<int32_t>(radius) * radius -
                               static_cast<int32_t>(dx) * dx;
        int32_t dy = 0;
        while ((dy + 1) * (dy + 1) <= inside) {
            ++dy;
        }
        tft.drawPixel(cx + dx, static_cast<int16_t>(cy - dy), color);
        tft.drawPixel(cx + dx, static_cast<int16_t>(cy - dy + 1), color);
    }
}

/** A die face on the usual 3x3 grid, bit 8 = top-left. */
void dieFace(Ui::Renderer& tft, int16_t x, int16_t y, int16_t size, uint16_t face,
             uint16_t body) {
    tft.fillRoundRect(x, y, size, size, RADIUS, body);
    const int16_t step = static_cast<int16_t>(size / 4);
    for (uint8_t cell = 0; cell < 9; ++cell) {
        if ((face & (1u << (8 - cell))) == 0) continue;
        tft.fillCircle(static_cast<int16_t>(x + step + (cell % 3) * step),
                       static_cast<int16_t>(y + step + (cell / 3) * step),
                       2, ink());
    }
}

/* A tilted elliptical band, two pixels thick, for the one icon that needs a
 * ring rather than a circle. Plotted rather than masked: the Wi-Fi tile once
 * drew whole circles and painted the unwanted half out with a rectangle, and
 * the mask was three pixels short. Explicit geometry cannot spill. */
void ellipseRing(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t rx, int16_t ry,
                 int16_t tiltDegrees, uint16_t color) {
    const float tilt = tiltDegrees * PI / 180.0f;
    const float ct = cosf(tilt);
    const float st = sinf(tilt);
    int16_t px = 0;
    int16_t py = 0;
    for (uint8_t i = 0; i <= 24; ++i) {
        const float a = i * 2.0f * PI / 24;
        const float ex = cosf(a) * rx;
        const float ey = sinf(a) * ry;
        const int16_t x = static_cast<int16_t>(cx + ex * ct - ey * st);
        const int16_t y = static_cast<int16_t>(cy + ex * st + ey * ct);
        if (i > 0) {
            tft.drawLine(px, py, x, y, color);
            tft.drawLine(px, static_cast<int16_t>(py + 1), x, static_cast<int16_t>(y + 1), color);
        }
        px = x;
        py = y;
    }
}

/** Pole only; the field is the caller's, and is what tells the two flags apart. */
void flagPole(Ui::Renderer& tft, int16_t cx, int16_t cy) {
    strokeV(tft, static_cast<int16_t>(cx - 15), static_cast<int16_t>(cy - 16), 33, steel());
}

}   // namespace LauncherIconArt
