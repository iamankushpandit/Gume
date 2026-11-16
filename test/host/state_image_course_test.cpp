// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/StateImageCourse.h, the levels State Flags and
// State Maps share, against the real state table (src/games/StateData.cpp,
// compiled here through the PC build's Arduino shim).
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games -I host/shim test/host/state_image_course_test.cpp -o state_image_course_test && ./state_image_course_test
//
// For every level: how many states it asks (9, 18, 23, 50, 50), three wrong
// answers available for every one of them with distinct capitals and no
// look-alike outline, a deck that deals the level before repeating, and the
// level line fitting.
#include "StateImageCourse.h"
#include "engine/Course.h"
#include "StateData.h"
#include "../src/games/StateData.cpp"

#include <cstdio>
#include <cstring>

using namespace StateImageCourse;

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            if (failures <= 20) {                                             \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                   \
                printf(__VA_ARGS__);                                          \
                printf("\n");                                                 \
            }                                                                 \
        }                                                                     \
    } while (0)

int main() {
    CHECK(STATE_COUNT == 50, "fifty states");
    const int want[LEVEL_COUNT + 1] = {0, 9, 18, 23, 50, 50};
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "level line %d", level);
        int n = 0;
        for (uint8_t i = 0; i < STATE_COUNT; ++i) {
            const StateFact& a = STATE_FACTS[i];
            if (!asks(level, a.tier)) continue;
            ++n;
            int ok = 0;
            for (uint8_t j = 0; j < STATE_COUNT; ++j) {
                const StateFact& b = STATE_FACTS[j];
                if (i == j || !offers(level, b.tier)) continue;
                if (strcmp(a.capital, b.capital) == 0) continue;
                if (outlineLookalike(a.code, b.code)) continue;
                ++ok;
            }
            CHECK(ok >= 3, "level %d %s: %d wrong answers", level, a.code, ok);
        }
        CHECK(n == want[level], "level %d asks %d states", level, n);

        Course::QuestionDeck deck;
        deck.reset(static_cast<uint16_t>(n));
        bool seen[64] = {};
        int distinct = 0;
        uint32_t s = 7u + level;
        for (int q = 0; q < n; ++q) {
            s = s * 1664525u + 1013904223u;
            const uint16_t d = deck.next(s);
            CHECK(d < n, "deck item");
            if (d < 64 && !seen[d]) {
                seen[d] = true;
                ++distinct;
            }
        }
        CHECK(distinct >= n - n / 8, "level %d: %d of %d in a pass", level, distinct, n);
    }
    CHECK(outlineLookalike("WY", "CO") && !outlineLookalike("TX", "CA"), "look-alikes");
    CHECK(capitalCounts(5) && !capitalCounts(4), "capital counts on 5");

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
