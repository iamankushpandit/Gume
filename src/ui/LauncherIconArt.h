// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "ui/Ui.h"

/* The shared vocabulary every launcher icon is drawn in: the palette, the
 * box, and the stroke and shape helpers. The rules they serve are stated at
 * the top of LauncherIcons.cpp; these are split out only so that file holds
 * the drawings and nothing else. */
namespace LauncherIconArt {

/* Half-width of the box every icon must fit inside. See rule 3. */
constexpr int16_t ICON_HALF = 18;
/* The landscape tile is 46 tall and the icon is centred 22 down from its top,
 * so anything past 22 is off the tile outright; 18 keeps a margin inside the
 * rounded border. Stated as an assertion so the relationship is checked rather
 * than remembered. */
static_assert(ICON_HALF <= 22, "icon box would overrun the landscape tile");
/* Every rounded corner in the icon set. See rule 4. */
constexpr int16_t RADIUS = 4;

uint16_t snow();    // the silhouette
uint16_t ink();     // detail on top of snow
uint16_t steel();   // a second, quieter solid
uint16_t amber();
uint16_t coral();
uint16_t mint();
uint16_t sky();

// ---- stroke helpers: nothing in the icon set draws a one-pixel line ----
void strokeH(Ui::Renderer& tft, int16_t x, int16_t y, int16_t w, uint16_t color);
void strokeV(Ui::Renderer& tft, int16_t x, int16_t y, int16_t h, uint16_t color);
void strokeDiag(Ui::Renderer& tft, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                uint16_t color);
/** The workhorse silhouette: a rounded slab centred on (cx, cy). */
void plate(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t w, int16_t h,
           uint16_t color);
/** A two-pixel ring, so circles match the stroke weight of everything else. */
void ring(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color);
void fillStar(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color);
void arcTop(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color);
/** A die face on the usual 3x3 grid, bit 8 = top-left. */
void dieFace(Ui::Renderer& tft, int16_t x, int16_t y, int16_t size, uint16_t face,
             uint16_t body);
void ellipseRing(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t rx, int16_t ry,
                 int16_t tiltDegrees, uint16_t color);
/** Pole only; the field is the caller's, and is what tells the two flags apart. */
void flagPole(Ui::Renderer& tft, int16_t cx, int16_t cy);

}   // namespace LauncherIconArt
