// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>

class TFT_eSPI;

/* The product mark, drawn by the platform when it can draw it better than
 * the one-bit mask.
 *
 * Ui::drawLogo() paints the mark from LogoMask, a one-bit silhouette made
 * for the 2.8-inch panel, and on a board that is the art: one mask pixel,
 * one panel pixel. A platform that draws the firmware's pixels larger -- the
 * Linux handhelds lay out at 320x240 and draw every pixel as a 2x2 block --
 * would show it as blocks, the one piece of lettering left so once its text
 * is drawn from a real typeface. So the mask is offered here first.
 *
 * One implementation per platform: ui/PanelArtEsp32.cpp declines, and the
 * mask is drawn as it always was; host/src/HostLogoArt.cpp rasterises the
 * same SVG the mask was made from, at the panel's real resolution, into the
 * same box. `which` is a Ui::Logo, and the mask is passed so the platform
 * can place what it draws exactly where the mask's ink is. */
namespace PanelArt {

bool drawLogo(TFT_eSPI& panel, uint8_t which, int16_t x0, int16_t y0,
              const uint8_t* bits, int16_t width, int16_t height, int16_t stride,
              uint16_t colour);

/* An arc one pixel thick, centred on pixel (cx, cy), from `fromDeg` to `toDeg`
 * (0 is to the right, angles grow clockwise, as y does). The few glyphs that
 * plot their arcs pixel by pixel -- the speaker's waves, the Wi-Fi badge's
 * fan -- offer them here first, for the same reason the mark does; false
 * means "plot the pixels", and on a board that is the art. */
bool drawArc(TFT_eSPI& panel, int16_t cx, int16_t cy, int16_t radius, int16_t fromDeg,
             int16_t toDeg, uint16_t colour);

}  // namespace PanelArt
