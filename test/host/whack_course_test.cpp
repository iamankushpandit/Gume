// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/WhackCourse.h: Whack A Mole's levels.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/whack_course_test.cpp -o whack_course_test && ./whack_course_test
//
// The pace only ever quickens up the ladder and never goes under 480 ms (the
// last-moment tick is 260 ms before a mole goes); decoys from level 6 at about
// one in five and never before; the level line fits the header; and a
// simulated player who reacts in a fixed time settles at the level that time
// can do, neither stuck at the bottom nor at the top.
#include "WhackCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>

using namespace WhackCourse;

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

int main() {
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) < LEVEL_LINE_MAX, "line '%s'", line);
        CHECK(visibleMs(level) >= 480, "level %d too fast", level);
        if (level > 1) CHECK(visibleMs(level) <= visibleMs(level - 1), "level %d slower", level);
        int decoys = 0;
        for (uint32_t r = 0; r < 10000; ++r) decoys += decoy(level, r * 2654435761u >> 7);
        if (level < 6) CHECK(decoys == 0, "decoys at level %d", level);
        else CHECK(decoys > 1500 && decoys < 2500, "level %d decoys %d", level, decoys);
    }
    CHECK(right(Outcome::Hit) && right(Outcome::DecoyLeft), "right");
    CHECK(!right(Outcome::Missed) && !right(Outcome::WrongCell) && !right(Outcome::DecoyPressed),
          "wrong");

    /* A player who reacts in `ms` hits every mole up for longer than that
     * plus a little, and misses the rest. They should settle where their
     * reaction time is. */
    const uint16_t reactions[] = {700, 950, 1250};
    const uint8_t expect[] = {8, 5, 2};   // the level whose pace they just make
    for (int k = 0; k < 3; ++k) {
        Course::Ladder ladder;
        ladder.start(LEVEL_COUNT, 0);
        for (int mole = 0; mole < 2000; ++mole) {
            const bool hit = visibleMs(ladder.level()) > reactions[k] + 50;
            ladder.answer(hit);
        }
        CHECK(ladder.level() >= expect[k] - 1 && ladder.level() <= expect[k] + 1,
              "reaction %d settles at %d, expected about %d", reactions[k], ladder.level(), expect[k]);
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
