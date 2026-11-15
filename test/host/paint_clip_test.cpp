// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of the paint clip's arithmetic (src/ui/PaintClip.h): what one
// viewport the panel driver is given when a screen's own viewport and the
// runtime's paint clip both apply, and how one row of an image is cut to a
// clip. The firmware resolves the two itself because its driver has a single
// viewport; the PC build's panel keeps a paint clip of its own and never runs
// that half, so this is where it is checked.
//
// Run by CI (tools/run_host_tests.py), not by pio test. From the repository root:
//   g++ -std=c++17 -Wall -Wextra -Isrc test/host/paint_clip_test.cpp -o /tmp/paint_clip && /tmp/paint_clip

#include <cstdio>
#include <cstdlib>

#include "ui/PaintClip.h"

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond);       \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

using Ui::PaintClip::Box;
using Ui::PaintClip::Viewport;
using Ui::PaintClip::clipRow;
using Ui::PaintClip::common;
using Ui::PaintClip::resolve;

static bool same(const Box& a, int32_t x, int32_t y, int32_t w, int32_t h) {
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

/* A 320x240 panel with a notice along the bottom 26 rows. */
static const Box ABOVE{0, 0, 320, 214};   // everything but the strip
static const Box STRIP{0, 214, 320, 26};  // the strip alone

static void testCommon() {
    CHECK(same(common(Box{10, 10, 100, 100}, Box{50, 60, 100, 100}), 50, 60, 60, 50));
    CHECK(same(common(Box{0, 0, 320, 240}, STRIP), 0, 214, 320, 26));
    /* Touching edges have nothing in common. */
    CHECK(common(ABOVE, STRIP).empty());
    CHECK(common(Box{0, 0, 10, 10}, Box{10, 0, 10, 10}).empty());
    /* An empty box has nothing in common with anything. */
    CHECK(common(Box{5, 5, 0, 10}, Box{0, 0, 320, 240}).empty());
    CHECK(common(Box{0, 0, 320, 240}, Box{5, 5, 10, -3}).empty());
}

static void testNoClipIsStraightThrough() {
    const Box list{8, 60, 304, 150};
    Viewport v = resolve(false, Box{}, false, false, Box{});
    CHECK(!v.set);
    v = resolve(true, list, false, false, Box{});
    CHECK(v.set && !v.datum && same(v.box, 8, 60, 304, 150));
    v = resolve(true, list, true, false, Box{});
    CHECK(v.set && v.datum && same(v.box, 8, 60, 304, 150));
    /* A clip left in the box while it is off must not leak. */
    v = resolve(false, list, true, false, STRIP);
    CHECK(!v.set && !v.datum);
}

static void testClipAlone() {
    Viewport v = resolve(false, Box{}, false, true, ABOVE);
    CHECK(v.set && !v.datum && same(v.box, 0, 0, 320, 214));
    v = resolve(false, Box{}, false, true, STRIP);
    CHECK(v.set && !v.datum && same(v.box, 0, 214, 320, 26));
    /* A screen's viewport left in the box after resetViewport() is ignored. */
    v = resolve(false, Box{8, 60, 304, 150}, true, true, STRIP);
    CHECK(v.set && !v.datum && same(v.box, 0, 214, 320, 26));
}

static void testScrollingListUnderANotice() {
    /* A list that runs to the bottom of the panel, notice up: the list loses
     * its last rows to the strip and nothing else. */
    const Box list{8, 60, 304, 180};
    Viewport v = resolve(true, list, false, true, ABOVE);
    CHECK(v.set && !v.datum && same(v.box, 8, 60, 304, 154));
    /* The notice going: only the rows it covered are redrawn. */
    v = resolve(true, list, false, true, STRIP);
    CHECK(v.set && !v.datum && same(v.box, 8, 214, 304, 26));
    /* A list that ends above the strip is not drawn at all while the strip
     * is being given back. */
    v = resolve(true, Box{8, 60, 304, 100}, false, true, STRIP);
    CHECK(v.set && v.box.empty());
}

static void testDatumViewportKeepsItsCorner() {
    /* It can lose its bottom and its right-hand side... */
    Viewport v = resolve(true, Box{8, 60, 304, 180}, true, true, ABOVE);
    CHECK(v.set && v.datum && same(v.box, 8, 60, 304, 154));
    v = resolve(true, Box{8, 60, 400, 100}, true, true, Box{0, 0, 200, 240});
    CHECK(v.set && v.datum && same(v.box, 8, 60, 192, 100));
    /* ...but not its top or its left: drawn from a moved origin it would land
     * in the wrong place, so it is not drawn. */
    v = resolve(true, Box{8, 60, 304, 180}, true, true, STRIP);
    CHECK(v.set && v.datum && v.box.empty() && v.box.x == 8 && v.box.y == 60);
    v = resolve(true, Box{8, 60, 304, 100}, true, true, STRIP);
    CHECK(v.set && v.datum && v.box.empty());
}

static void testImageRows() {
    int32_t skip = -1, count = -1;
    /* Wholly inside. */
    CHECK(clipRow(ABOVE, 100, 50, 64, skip, count) && skip == 0 && count == 64);
    /* The last row above the strip, and the first one in it. */
    CHECK(clipRow(ABOVE, 100, 213, 64, skip, count) && skip == 0 && count == 64);
    CHECK(!clipRow(ABOVE, 100, 214, 64, skip, count));
    CHECK(clipRow(STRIP, 100, 214, 64, skip, count) && skip == 0 && count == 64);
    CHECK(!clipRow(STRIP, 100, 213, 64, skip, count));
    CHECK(!clipRow(STRIP, 100, 240, 64, skip, count));
    /* Cut on the left, on the right, and on both. */
    const Box window{50, 0, 100, 240};
    CHECK(clipRow(window, 30, 10, 64, skip, count) && skip == 20 && count == 44);
    CHECK(clipRow(window, 120, 10, 64, skip, count) && skip == 0 && count == 30);
    CHECK(clipRow(window, 0, 10, 320, skip, count) && skip == 50 && count == 100);
    /* Beside it, on either side, and a row with nothing in it. */
    CHECK(!clipRow(window, 0, 10, 50, skip, count));
    CHECK(!clipRow(window, 150, 10, 50, skip, count));
    CHECK(!clipRow(window, 60, 10, 0, skip, count));
    CHECK(!clipRow(Box{0, 0, 0, 0}, 0, 0, 10, skip, count));
}

int main() {
    testCommon();
    testNoClipIsStraightThrough();
    testClipAlone();
    testScrollingListUnderANotice();
    testDatumViewportKeepsItsCorner();
    testImageRows();
    std::printf("paint clip: all passed\n");
    return 0;
}
