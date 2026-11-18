// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The host's answer to PanelArt (src/ui/PanelArt.h): the product mark drawn
 * from its SVG at the panel's real resolution, anti-aliased, instead of the
 * one-bit mask drawn as blocks of the UI scale.
 *
 * It is the same art and the same fitting the mask was made with --
 * HostSplash::markPart() is tools/gen_logo_mask.py's select(), bounds() and
 * rasterise() -- so it lands on the mask's own ink, at the mask's height.
 * The mask still decides two things, because it is what the firmware laid
 * out: where the art stops and the trade mark sign begins (the gap
 * gen_logo_mask.py cut at, found the same way), and the sign's box, which is
 * drawn in FreeSans at that height. At a UI scale of 1 the mask IS the art,
 * one pixel to one, so it declines and the board's picture stays exact. */

#include <TFT_eSPI.h>

#include <map>
#include <vector>

#include "HostSplash.h"
#include "HostText.h"
#include "ui/PanelArt.h"

namespace {

struct Drawn {
    int artW = 0;                 // the art, in real pixels
    std::vector<uint8_t> art;     // artW x height*scale
    int tmX = -1, tmY = 0;        // the sign's box, in the mask's pixels
    int tmW = 0, tmH = 0;
};

bool ink(const uint8_t* bits, int16_t stride, int x, int y) {
    return (bits[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1u;
}

HostSplash::MarkPart partOf(uint8_t which) {
    switch (which) {
        case 1: case 2: return HostSplash::MarkPart::Wordmark;   // Word, WordSmall
        case 4: case 5: return HostSplash::MarkPart::Icon;       // Icon, IconBig
        default: return HostSplash::MarkPart::Badge;             // Badge, BadgeMid
    }
}

/* Where the sign is in the mask: the ink right of the first gap after the
 * art's own width (gen_logo_mask.py's centre cut). A mask no wider than its
 * art has none: the sign is left off the small marks (TM_MIN_PX), and a gap
 * inside the name must not be taken for one. None for the brain. */
void findSign(Drawn& d, const uint8_t* bits, int16_t w, int16_t h, int16_t stride,
              int artLogical) {
    if (w <= artLogical + 2) return;
    std::vector<int> cols;
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            if (ink(bits, stride, x, y)) {
                cols.push_back(x);
                break;
            }
        }
    }
    int cut = -1;
    for (size_t k = 0; k + 1 < cols.size(); ++k) {
        if (cols[k + 1] - cols[k] > 1 && cols[k] >= artLogical - 2) {
            cut = cols[k];
            break;
        }
    }
    if (cut < 0) return;
    int top = h, bottom = -1, right = cut;
    for (int x = cut + 1; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            if (!ink(bits, stride, x, y)) continue;
            top = std::min(top, y);
            bottom = std::max(bottom, y);
            right = std::max(right, x);
        }
    }
    if (bottom < 0) return;
    int left = cut + 1;
    while (left < right) {
        bool any = false;
        for (int y = top; y <= bottom && !any; ++y) any = ink(bits, stride, left, y);
        if (any) break;
        ++left;
    }
    d.tmX = left;
    d.tmY = top;
    d.tmW = right - left + 1;
    d.tmH = bottom - top + 1;
}

}   // namespace

bool PanelArt::drawArc(TFT_eSPI& panel, int16_t cx, int16_t cy, int16_t radius, int16_t fromDeg,
                       int16_t toDeg, uint16_t colour) {
    if (panel.frameScale() < 2) return false;   // at 1:1 the pixels are the art
    panel.drawArcReal(cx, cy, radius, fromDeg, toDeg, colour);
    return true;
}

bool PanelArt::drawLogo(TFT_eSPI& panel, uint8_t which, int16_t x0, int16_t y0,
                        const uint8_t* bits, int16_t width, int16_t height, int16_t stride,
                        uint16_t colour) {
    const int scale = panel.frameScale();
    if (scale < 2 || !bits || height <= 0) return false;

    static std::map<std::pair<int, int>, Drawn> cache;
    const auto key = std::make_pair(static_cast<int>(which), scale);
    auto it = cache.find(key);
    if (it == cache.end()) {
        Drawn d;
        const HostSplash::MarkPart part = partOf(which);
        const std::vector<float> cover = HostSplash::markPart(part, height * scale, d.artW);
        if (d.artW <= 0) return false;
        if (part != HostSplash::MarkPart::Icon) {
            findSign(d, bits, width, height, stride, (d.artW + scale - 1) / scale);
        }
        d.art.resize(cover.size());
        for (size_t k = 0; k < cover.size(); ++k) {
            d.art[k] = static_cast<uint8_t>(cover[k] * 255.0f + 0.5f);
        }
        it = cache.emplace(key, std::move(d)).first;
    }
    const Drawn& d = it->second;
    panel.blendArt(x0, y0, 0, 0, d.art.data(), d.artW, height * scale, colour);

    /* The sign, in FreeSans, its capitals the height of the mask's sign. */
    if (d.tmX >= 0) {
        const int cap = d.tmH * scale;
        int pen = d.tmX * scale;
        const int baseline = (d.tmY + d.tmH) * scale;
        for (const char c : {'T', 'M'}) {
            const HostText::Glyph* g = HostText::glyph(cap, 1000, static_cast<uint32_t>(c));
            if (!g) return true;   // the art is down; a missing sign is no reason to redraw
            if (g->w > 0) {
                panel.blendArt(x0, y0, pen + g->xoff, baseline + g->yoff, g->alpha.data(), g->w,
                               g->h, colour);
            }
            pen += g->advance;
        }
    }
    return true;
}
