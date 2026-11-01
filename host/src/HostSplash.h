// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <cstdint>

/* The boot splash, drawn at the panel's own resolution. See HostSplash.cpp. */
namespace HostSplash {

/* Draw the splash into `rgb` (w x h, 3 bytes a pixel, R G B). `progress` is
 * 0..1 for the loading bar, or negative to leave the bar off; `glow` scales
 * the halo, 1.0 being the static boot logo's. The mark and its glow are
 * computed once per size and reused, so an animation frame costs a copy, a
 * multiply-add per pixel and a bar. */
void render(uint8_t* rgb, int w, int h, float progress, float glow);

/* The same picture as a 24-bit BMP. */
bool writeBmp(const char* path, int w, int h, float progress, float glow = 1.0f);

/* On a handheld that boots into Braino (BRAINO_BOOT_SPLASH=1, set by its
 * launcher): show the splash across the window and fill the loading bar to
 * 100% before the first screen. A no-op otherwise. */
void runBootPhase();

}   // namespace HostSplash
