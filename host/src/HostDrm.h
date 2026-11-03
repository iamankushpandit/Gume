// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The panel without a window system: two dumb buffers on the kernel's own
 * display driver, flipped on vertical blank. Built only with BRAINO_HOST_DRM,
 * for a whole-device image that has no GPU driver, no Mesa and no X or
 * Wayland -- the RG40XX V's Buildroot image (host/rg40xxv). Braino draws a
 * 320x240 picture; scaling it to the panel is a few hundred thousand pixel
 * writes a frame, which a Cortex-A53 does without noticing.
 *
 * Only the kernel's DRM ioctls are used, not libdrm, so the image carries no
 * library for it. */

#include <cstdint>

namespace HostDrm {

/* Opens the first DRM card with a connected output and shows a black frame on
 * it: an HDMI screen if one is plugged in, otherwise the built-in panel. False
 * if there is none; the caller then has no screen at all. */
bool begin();
void shutdown();

/* The current output's mode, in pixels. */
int width();
int height();

/* True when a screen was plugged in or unplugged since the last call and the
 * picture has moved: width() and height() may differ, and both buffers are
 * black. The caller redraws everything. Cheap enough to ask every frame. */
bool outputChanged();

/* The buffer to draw the next frame into, XRGB8888, `stride` pixels a row.
 * It is the one not on the screen, and the previous flip has completed. */
uint32_t* backBuffer(int& stride);

/* Puts the back buffer on the screen at the next vertical blank. */
void flip();

}   // namespace HostDrm
