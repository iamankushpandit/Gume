// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The ESP32 boards' answer to PanelArt: the one-bit mask is the art here,
 * drawn one mask pixel to one panel pixel, so there is nothing better to
 * draw. */

#include "ui/PanelArt.h"

bool PanelArt::drawLogo(TFT_eSPI&, uint8_t, int16_t, int16_t, const uint8_t*, int16_t,
                        int16_t, int16_t, uint16_t) {
    return false;
}
