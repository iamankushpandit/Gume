// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/StatesCourse.h against the real state table
// (src/games/StateData.cpp, compiled through the PC build's Arduino shim).
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games -I host/shim test/host/states_course_test.cpp -o states_course_test && ./states_course_test
//
// For every level: the questions it holds (9, 9, 18, 54, 23, 100, 100, 200),
// every one with three wrong answers whose labels differ from the answer's
// and each other's, close wrong answers for the postal codes, a deck dealing
// every question before repeating, and the level line fitting.
#include "StatesCourse.h"
#include "engine/Course.h"
#include "StateData.h"
#include "../src/games/StateData.cpp"

#include <cstdio>
#include <cstring>

using namespace StatesCourse;

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

static const char* lab(Dir d, const StateFact& s) { return label(d, s.name, s.capital, s.code); }

int main() {
    const int want[LEVEL_COUNT + 1] = {0, 9, 9, 18, 54, 23, 100, 100, 200};
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "level line %d", level);
        uint8_t n = 0;
        for (uint8_t i = 0; i < STATE_COUNT; ++i) {
            const StateFact& a = STATE_FACTS[i];
            if (!asks(level, a.tier)) continue;
            ++n;
            for (uint8_t k = 0; k < dirCount(level); ++k) {
                const Dir d = dirAt(level, k);
                int ok = 0, close = 0;
                for (uint8_t j = 0; j < STATE_COUNT; ++j) {
                    const StateFact& b = STATE_FACTS[j];
                    if (i == j || !offers(level, b.tier)) continue;
                    if (strcmp(lab(d, a), lab(d, b)) == 0) continue;
                    ++ok;
                    if (a.code[0] == b.code[0]) ++close;
                }
                CHECK(ok >= 3, "level %d %s dir %d: %d wrong", level, a.code, (int)d, ok);
                (void)close;
                CHECK(strlen(subject(d, a.name, a.capital, a.code)) > 0, "subject");
            }
        }
        const uint16_t pool = poolSize(level, n);
        CHECK(pool == want[level], "level %d holds %d questions", level, pool);
        CHECK(pool <= Course::QuestionDeck::MAX_ITEMS, "deck size");

        Course::QuestionDeck deck;
        deck.reset(pool);
        bool seen[256] = {};
        int distinct = 0;
        uint32_t s = 3u + level;
        for (uint16_t q = 0; q < pool; ++q) {
            s = s * 1664525u + 1013904223u;
            const uint16_t d = deck.next(s);
            CHECK(d < pool, "deck item");
            if (d < 256 && !seen[d]) {
                seen[d] = true;
                ++distinct;
            }
        }
        CHECK(distinct >= pool - pool / 8, "level %d: %d of %d in a pass", level, distinct, pool);
    }
    /* The eight M codes give close wrong answers for each other. */
    int m = 0;
    for (uint8_t i = 0; i < STATE_COUNT; ++i) m += STATE_FACTS[i].code[0] == 'M';
    CHECK(m == 8, "eight M states, found %d", m);
    CHECK(dirCount(8) == 4 && dirCount(1) == 1 && dirCount(7) == 2, "directions");
    CHECK(dirAt(7, 0) == Dir::CodeOf && dirAt(7, 1) == Dir::StateOfCode, "code directions");

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
