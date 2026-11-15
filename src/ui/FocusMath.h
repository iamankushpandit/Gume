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
#include <cstdlib>

/* Where the d-pad's selector goes next: pure arithmetic on boxes, no device.
 * The boxes are the controls on the screen in the frame the d-pad is turned
 * into, so up on the d-pad is up as the screen is seen in every rotation.
 *
 * Shared by both selectors: the Linux host's, which finds the boxes in its
 * panel shim (host/src/HostTftFocus.cpp), and the firmware's on a board with
 * keys (engine/AppRuntimeSelector.cpp, ui/FocusRecorder.cpp). One copy, so
 * the two consoles cannot come to disagree about where right goes. */
namespace FocusMath {

struct Box {
    int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // inclusive
    bool header = false;                      // a header icon rather than part of the screen
    int cx() const { return (x0 + x1) / 2; }
    int cy() const { return (y0 + y1) / 2; }
    bool same(const Box& o) const { return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1; }
};

/* The box nearest to the point, or -1 when there are none. */
inline int nearest(const Box* boxes, int count, int x, int y) {
    int best = -1;
    int32_t bestScore = 0;
    for (int i = 0; i < count; ++i) {
        const int32_t dx = boxes[i].cx() - x;
        const int32_t dy = boxes[i].cy() - y;
        const int32_t score = dx * dx + dy * dy;
        if (best < 0 || score < bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

/* The box the selector moves to from `from`, for one of (dx, dy) being +1 or
 * -1, or -1 when nothing lies that way.
 *
 * A box must be further along the direction, and either share the row or
 * column with `from` or sit within a cone about it: without the cone, right
 * from the last tile on a page lands on a Next button far below it, where the
 * player meant to turn the page. Anything on the line beats anything off it,
 * whatever the distance, so right from a wide space bar is the key beside it
 * and not a nearer one on the row above. Sideways distance counts double, so a
 * staggered keyboard row still goes to the key most nearly underneath. */
constexpr int32_t OFF_THE_LINE = 100000;

inline int step(const Box* boxes, int count, const Box& from, int dx, int dy) {
    int best = -1;
    int32_t bestScore = 0;
    for (int i = 0; i < count; ++i) {
        const Box& b = boxes[i];
        if (b.same(from)) continue;
        const int32_t along = (b.cx() - from.cx()) * dx + (b.cy() - from.cy()) * dy;
        if (along <= 0) continue;
        const int32_t across = dx != 0 ? std::abs(b.cy() - from.cy()) : std::abs(b.cx() - from.cx());
        const bool shared = dx != 0 ? (b.y0 <= from.y1 && b.y1 >= from.y0)
                                    : (b.x0 <= from.x1 && b.x1 >= from.x0);
        if (!shared && across > along) continue;
        const int32_t score = along + 2 * across + (shared ? 0 : OFF_THE_LINE);
        if (best < 0 || score < bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

/* Up or down when step() found nothing: the nearest box lying wholly above
 * or below `from`, however far to the side, or -1 when there is none. Never
 * for left and right, where nothing in the cone means the next page.
 *
 * The cone keeps a press from sliding sideways, but a lone control set off to
 * one side of a wide row -- the Wi-Fi password's Show checkbox, at the right
 * end of the field above a keyboard -- is then reachable only from the two or
 * three keys under it, and up from any other key does nothing at all. That
 * was a control with no way in from most of the screen. Callers try this
 * only once a list that scrolls that way has had its turn, so the rest of a
 * list still comes before a control beyond it. Sideways distance counts
 * double, as in step(). */
inline int stepBeyond(const Box* boxes, int count, const Box& from, int dy) {
    if (dy == 0) return -1;
    int best = -1;
    int32_t bestScore = 0;
    for (int i = 0; i < count; ++i) {
        const Box& b = boxes[i];
        const bool beyond = dy < 0 ? b.y1 < from.y0 : b.y0 > from.y1;
        if (!beyond) continue;
        const int32_t along = (b.cy() - from.cy()) * dy;
        const int32_t across = std::abs(b.cx() - from.cx());
        const int32_t score = along + 2 * across;
        if (best < 0 || score < bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

/* After a page turn: the box at the far end of `from`'s row, where the eye
 * expects to arrive, or -1 when nothing else shares the row. */
inline int rowEnd(const Box* boxes, int count, const Box& from, bool leftmost) {
    int best = -1;
    for (int i = 0; i < count; ++i) {
        const Box& b = boxes[i];
        if (b.y0 > from.y1 || b.y1 < from.y0) continue;
        if (best < 0 || (leftmost ? b.cx() < boxes[best].cx() : b.cx() > boxes[best].cx())) best = i;
    }
    return best;
}

}   // namespace FocusMath
