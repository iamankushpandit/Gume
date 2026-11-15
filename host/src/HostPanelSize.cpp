// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The host's answer to PanelSize (src/hal/PanelSize.h): the panel
 * HostPlatform::begin() found before the firmware started -- the handheld's
 * display mode, a fullscreen desktop, or the window it was asked for -- turned
 * back to the host board's native portrait (host/boards/host.h mounts the
 * panel at landscape rotation 1, so the landscape frame's height is the
 * native width). */

#include "HostPlatform.h"
#include "hal/PanelSize.h"

static_assert(BOARD.panel.landscapeRotation & 1,
              "the host frame is landscape at an odd rotation; see host/boards/host.h");

int16_t PanelSize::nativeWidth() { return static_cast<int16_t>(HostPlatform::panelHeight()); }
int16_t PanelSize::nativeHeight() { return static_cast<int16_t>(HostPlatform::panelWidth()); }
