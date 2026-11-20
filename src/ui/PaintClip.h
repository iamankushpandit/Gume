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

/* The arithmetic of a paint clip, apart from any panel, so that it can be
 * checked on a desk (test/host/paint_clip_test.cpp). Ui::TftRenderer is its
 * one user; setPaintClip() there says what a paint clip is for. */
namespace Ui {
namespace PaintClip {

struct Box {
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
};

/* What two boxes have in common. Empty, with no width and no height, when
 * they have nothing: the panel driver reads a viewport like that as "draw
 * nothing", which is what is wanted. */
inline Box common(const Box& a, const Box& b) {
    const int32_t x0 = a.x > b.x ? a.x : b.x;
    const int32_t y0 = a.y > b.y ? a.y : b.y;
    const int32_t x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    const int32_t y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    if (a.empty() || b.empty() || x1 <= x0 || y1 <= y0) return Box{x0, y0, 0, 0};
    return Box{x0, y0, x1 - x0, y1 - y0};
}

/* The one viewport a panel driver is given. `set` false is no viewport at
 * all: the whole panel. */
struct Viewport {
    bool set = false;
    bool datum = false;
    Box box;
};

/* A screen's own viewport (`own`, when `ownSet`) and a paint clip (`clip`,
 * when `clipSet`), resolved into the one viewport the driver has.
 *
 * A viewport set with vpDatum false is a clip and nothing more, so the two are
 * simply what they have in common. Every viewport in this codebase is set
 * that way. One set with vpDatum true also moves the origin to its corner,
 * and that corner cannot move: it may lose its bottom and its right-hand
 * side to the clip, and if the clip would take its top or its left it is not
 * drawn at all, because drawn from a moved origin it would be drawn in the
 * wrong place. */
inline Viewport resolve(bool ownSet, const Box& own, bool ownDatum, bool clipSet,
                        const Box& clip) {
    Viewport v;
    if (!clipSet) {
        v.set = ownSet;
        v.datum = ownSet && ownDatum;
        v.box = own;
        return v;
    }
    v.set = true;
    if (!ownSet) {
        v.box = clip;
        return v;
    }
    v.box = common(own, clip);
    if (ownDatum) {
        v.datum = true;
        if (v.box.empty() || v.box.x != own.x || v.box.y != own.y) {
            v.box = Box{own.x, own.y, 0, 0};
        }
    }
    return v;
}

/* One row of an image, `w` pixels wide with its first at (x, y), against a
 * clip: how many of its pixels to skip, and how many to send after them.
 * False when none of the row shows. */
inline bool clipRow(const Box& clip, int32_t x, int32_t y, int32_t w, int32_t& skip,
                    int32_t& count) {
    if (clip.empty() || w <= 0 || y < clip.y || y >= clip.y + clip.h) return false;
    const int32_t x0 = x > clip.x ? x : clip.x;
    const int32_t x1 = (x + w) < (clip.x + clip.w) ? (x + w) : (clip.x + clip.w);
    if (x1 <= x0) return false;
    skip = x0 - x;
    count = x1 - x0;
    return true;
}

}  // namespace PaintClip
}  // namespace Ui
