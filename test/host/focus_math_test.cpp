// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of where the d-pad's selector goes next (src/ui/FocusMath.h),
// the arithmetic both selectors share: the Linux host's and the firmware's on
// a board with keys.
//
// Not run by CI and not picked up by pio test. From the repository root:
//   g++ -std=c++17 -Wall -Wextra -Isrc test/host/focus_math_test.cpp -o /tmp/focus_math && /tmp/focus_math

#include <cstdio>
#include <cstdlib>

#include "ui/FocusMath.h"

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond);       \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

using FocusMath::Box;

static Box box(int x0, int y0, int x1, int y1, bool header = false) {
    Box b;
    b.x0 = static_cast<int16_t>(x0);
    b.y0 = static_cast<int16_t>(y0);
    b.x1 = static_cast<int16_t>(x1);
    b.y1 = static_cast<int16_t>(y1);
    b.header = header;
    return b;
}

int main() {
    /* The Wi-Fi password screen as the PC build records it: Show at the right
     * end of the field, a row of ten keys below it, and the header's padlock,
     * speaker and gear above. */
    Box boxes[16];
    int n = 0;
    const int SHOW = n;
    boxes[n++] = box(234, 59, 307, 82);
    const int FIRST_KEY = n;
    for (int k = 0; k < 10; ++k) boxes[n++] = box(2 + 32 * k, 96, 29 + 32 * k, 119);
    const int LOCK = n;
    boxes[n++] = box(40, 6, 57, 23, true);
    boxes[n++] = box(64, 6, 81, 23, true);
    boxes[n++] = box(294, 6, 311, 23, true);

    /* step() alone: only the keys under Show reach it, which was the defect.
     * From the sixth key up finds nothing at all. */
    CHECK(FocusMath::step(boxes, n, boxes[FIRST_KEY + 5], 0, -1) == -1);
    CHECK(FocusMath::step(boxes, n, boxes[FIRST_KEY + 8], 0, -1) == SHOW);

    /* Every key now goes somewhere going up: step() where its cone holds
     * something (Show, or the padlock above the first keys), and otherwise
     * stepBeyond(), which finds Show -- nearer than the header. */
    int fellBack = 0;
    for (int k = 0; k < 10; ++k) {
        const Box& from = boxes[FIRST_KEY + k];
        if (FocusMath::step(boxes, n, from, 0, -1) >= 0) continue;
        CHECK(FocusMath::stepBeyond(boxes, n, from, -1) == SHOW);
        ++fellBack;
    }
    /* The sixth and seventh keys: the middle of the row, where the selector
     * arrives, and the one beside it. */
    CHECK(fellBack == 2);
    /* Down from Show is a key, and from a key there is nothing below. */
    CHECK(FocusMath::stepBeyond(boxes, n, boxes[SHOW], 1) >= FIRST_KEY);
    CHECK(FocusMath::stepBeyond(boxes, n, boxes[FIRST_KEY], 1) == -1);
    /* Never sideways: a press left or right with nothing in the cone is the
     * next page's business. */
    CHECK(FocusMath::stepBeyond(boxes, n, boxes[FIRST_KEY + 9], 0) == -1);
    /* A neighbour on the same row is not "above", however it is staggered. */
    Box row[2] = {box(0, 100, 30, 120), box(40, 95, 70, 115)};
    CHECK(FocusMath::stepBeyond(row, 2, row[0], -1) == -1);
    /* Up from Show goes to the header, the nearest thing above it. */
    const int up = FocusMath::stepBeyond(boxes, n, boxes[SHOW], -1);
    CHECK(up >= LOCK && boxes[up].header);

    std::printf("focus_math_test: all checks passed\n");
    return 0;
}
