// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/CinnamonCourse.h: Cinnamon Says' levels.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/cinnamon_course_test.cpp -o cinnamon_course_test && ./cinnamon_course_test
//
// Every level: a pace under three flashes a second, a start length a player
// can begin from, which pad is owed forwards and backwards, the counting
// rule, the status and level lines fitting the strip, and a simulated player
// with a fixed span settling on the level that span can do.
#include "CinnamonCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>

using namespace CinnamonCourse;

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
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        const Level& l = levelOf(level);
        /* Under three flashes a second, and each flash long enough to see. */
        CHECK(l.onMs + l.offMs > 1000 / 3, "level %d too fast", level);
        CHECK(l.onMs >= 300 && l.offMs >= 200, "level %d flash too short", level);
        CHECK(l.startLength >= 1 && l.startLength <= COUNTS_FROM, "level %d start", level);
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "level line %d", level);
        CHECK(repeatLine(level, MAX_STEPS - 1, MAX_STEPS, line, sizeof(line)) < STATUS_MAX,
              "status line %d", level);
        /* Order owed. */
        uint8_t seq[MAX_STEPS];
        for (uint8_t i = 0; i < MAX_STEPS; ++i) seq[i] = static_cast<uint8_t>((i * 7 + 3) % 4);
        for (uint8_t len = 1; len <= MAX_STEPS; ++len) {
            for (uint8_t i = 0; i < len; ++i) {
                const uint8_t want = l.backwards ? seq[len - 1 - i] : seq[i];
                CHECK(expectedPad(level, seq, len, i) == want, "level %d order", level);
            }
        }
    }
    CHECK(clampLevel(0) == 1 && clampLevel(99) == LEVEL_COUNT, "clamp");
    CHECK(!levelOf(1).backwards && levelOf(5).backwards && levelOf(6).backwards, "backwards");
    CHECK(levelOf(3).fresh && levelOf(4).fresh && levelOf(6).fresh && !levelOf(1).fresh,
          "fresh");
    CHECK(keepsBest(1) && keepsBest(4) && !keepsBest(5) && !keepsBest(6), "best");

    for (uint8_t len = 1; len <= MAX_STEPS; ++len) {
        CHECK(judgeRepeated(len) == (len >= COUNTS_FROM ? 1 : 0), "repeated %d", len);
        for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
            const bool wrong = len < COUNTS_FROM || len <= levelOf(level).startLength;
            CHECK(judgeFailed(level, len) == (wrong ? -1 : 0), "failed %d", len);
        }
    }

    /* A player who can hold `span` steps forwards (and span-2 backwards)
     * plays runs as the game does; the ladder must climb past what is easy
     * and not fall off what they can do. */
    for (uint8_t span = 3; span <= 9; ++span) {
        Course::Ladder ladder;
        ladder.start(LEVEL_COUNT, 0);
        uint8_t topSeen = 1;
        for (int run = 0; run < 400; ++run) {
            const uint8_t level = ladder.level();
            const uint8_t can = levelOf(level).backwards ? static_cast<uint8_t>(span - 2) : span;
            uint8_t len = levelOf(level).startLength;
            bool moved = false;
            while (!moved) {
                if (len > can) {
                    const int8_t v = judgeFailed(level, len);
                    if (v != 0) ladder.answer(false);
                    break;
                }
                if (judgeRepeated(len) > 0 &&
                    ladder.answer(true) != Course::Ladder::Step::Stay) {
                    moved = true;   // the game starts a new run at the new level
                }
                if (len == MAX_STEPS) break;
                ++len;
            }
            if (ladder.level() > topSeen) topSeen = ladder.level();
        }
        if (span >= 4) {
            CHECK(topSeen >= 4, "span %d reached only level %d", span, topSeen);
        }
        if (span >= 6) {
            CHECK(topSeen == LEVEL_COUNT, "span %d reached only level %d", span, topSeen);
        }
        if (span == 3) {
            CHECK(ladder.level() <= 3, "span 3 sits at level %d", ladder.level());
        }
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
