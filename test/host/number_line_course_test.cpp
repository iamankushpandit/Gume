// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/NumberLineCourse.h: the levels of Number Line
// and the questions each one asks. The header is pure C++, and this file is
// what proves it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/number_line_course_test.cpp -o number_line_course_test && ./number_line_course_test
//
// For every level and item, under many random streams: the frog starts and
// lands on the line, every hop is a tick, the answer is what the question
// asks; four different options that fit on a button, exactly one right, the
// level's commonest mistake among them, and the right one neither in one slot
// nor the smallest or biggest most of the time. Then that no two items of a
// level are the same question, and that the ladder climbs to the top.
#include "NumberLineCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <set>
#include <tuple>

using namespace NumberLineCourse;

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            if (failures < 40) {                                              \
                std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
                std::printf(__VA_ARGS__);                                     \
                std::printf("\n");                                            \
            }                                                                 \
        }                                                                     \
    } while (0)

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

static bool has(const Question& q, int v) {
    for (int i = 0; i < 4; ++i) {
        if (q.options[i] == v) return true;
    }
    return false;
}

int main() {
    CHECK(clampLevel(0) == 1 && clampLevel(99) == LEVEL_COUNT, "clamp");
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        const uint16_t pool = poolSize(level);
        CHECK(pool > 0 && pool <= Course::QuestionDeck::MAX_ITEMS, "level %u pool", level);
        char line[64];
        const int ln = levelLine(level, line, sizeof(line));
        CHECK(ln > 0 && ln <= LEVEL_LINE_MAX, "level %u line fits", level);

        std::set<std::tuple<int, int, int, int, int>> seen;
        int slot[4] = {};
        int rank[4] = {};
        int total = 0;
        Rng rng(level * 7919U);
        for (uint16_t item = 0; item < pool; ++item) {
            Question first;
            CHECK(make(level, item, rng.next(), first), "level %u item %u makes", level, item);
            seen.insert(std::make_tuple(static_cast<int>(first.ask), first.start, first.hop,
                                        first.hops, first.add ? 1 : 0));
            for (int k = 0; k < 64; ++k) {
                Question q;
                make(level, item, rng.next(), q);
                CHECK(q.start == first.start && q.answer == first.answer, "level %u stable", level);
                CHECK(q.lo < q.hi && (q.hi - q.lo) % q.tick == 0, "level %u line", level);
                CHECK((q.hi - q.lo) / q.tick <= 20, "level %u ticks fit", level);
                CHECK(q.start >= q.lo && q.start <= q.hi, "level %u start on the line", level);
                CHECK(q.start % q.tick == 0 || q.ask == Ask::Nearest, "level %u start on a tick", level);
                CHECK(q.watchFirst == (level == 1), "level %u watch first", level);
                CHECK(q.viaTen == (level == 5), "level %u via ten", level);
                if (q.ask != Ask::Nearest) {
                    CHECK(q.hops >= 1 && q.hop % q.tick == 0, "level %u hops are ticks", level);
                    CHECK(q.landing() >= q.lo && q.landing() <= q.hi, "level %u lands on the line",
                          level);
                }
                switch (level) {
                    case 1: CHECK(q.add && q.landing() <= 5, "level 1 within five"); break;
                    case 2: CHECK(q.add && q.hops <= 5 && q.landing() <= 10, "level 2"); break;
                    case 3: CHECK(!q.add && q.hops <= 5 && q.landing() >= 0, "level 3"); break;
                    case 4: CHECK(q.ask == Ask::Hops && q.answer == q.hops, "level 4"); break;
                    case 5: {
                        const int small = q.add ? q.start : q.landing();
                        const int big = q.add ? q.landing() : q.start;
                        CHECK(small <= 9 && q.hops <= 9 && big > 10 && big <= 18, "level 5 through ten");
                        break;
                    }
                    case 6: CHECK(q.hop == 10 && q.start % 10 == 0 && q.hi == 100, "level 6 tens"); break;
                    case 7:
                        CHECK(q.ask == Ask::Nearest && q.start % 5 != 0 &&
                                  q.answer == ((q.start + 5) / 10) * 10, "level 7 nearest");
                        break;
                    case 8:
                        CHECK(q.ask == Ask::Times && q.start == 0 && q.answer == q.hop * q.hops,
                              "level 8 times");
                        break;
                    case 9: CHECK(q.lo == -5 && q.hi == 5, "level 9 below zero"); break;
                }
                if (q.ask == Ask::Land) CHECK(q.answer == q.landing(), "level %u answer", level);

                char text[64];
                const int tn = q.text(text, sizeof(text));
                CHECK(tn > 0 && tn <= QUESTION_MAX, "level %u question fits: %s", level, text);
                const int sn = q.solved(text, sizeof(text));
                CHECK(sn > 0 && sn < 28, "level %u solved fits: %s", level, text);

                int right = 0;
                for (int i = 0; i < 4; ++i) {
                    /* Answer buttons are 64px at font 4: three characters. */
                    CHECK(q.options[i] >= -99 && q.options[i] <= 999, "level %u option fits", level);
                    for (int j = i + 1; j < 4; ++j) {
                        CHECK(q.options[i] != q.options[j], "level %u distinct", level);
                    }
                    if (q.options[i] == q.answer) ++right;
                }
                CHECK(right == 1 && q.options[q.correct] == q.answer, "level %u one right", level);

                /* The level's own mistake. */
                const int sign = q.add ? 1 : -1;
                switch (level) {
                    case 1:
                    case 2:
                    case 3:
                        if (q.answer - sign >= 0) CHECK(has(q, q.answer - sign), "level %u start counted", level);
                        break;
                    case 4: CHECK(has(q, q.answer + 1), "level 4 start counted"); break;
                    case 5: CHECK(has(q, q.answer - 10 * sign), "level 5 a ten lost"); break;
                    case 6:
                        if (q.answer - 10 * sign >= 0) CHECK(has(q, q.answer - 10 * sign), "level 6 one hop");
                        break;
                    case 7: {
                        const int lower = (q.start / 10) * 10;
                        CHECK(has(q, q.answer == lower ? lower + 10 : lower), "level 7 the other ten");
                        break;
                    }
                    case 8:
                        CHECK(has(q, q.answer + q.hop) || has(q, q.answer - q.hop), "level 8 one hop out");
                        CHECK(!(has(q, q.answer + q.hop) && has(q, q.answer - q.hop)), "level 8 not between");
                        break;
                    case 9:
                        if (q.answer != 0) CHECK(has(q, -q.answer), "level 9 sign lost");
                        break;
                }

                ++slot[q.correct];
                int below = 0;
                for (int i = 0; i < 4; ++i) {
                    if (q.options[i] < q.answer) ++below;
                }
                ++rank[below];
                ++total;
            }
        }
        CHECK(static_cast<int>(seen.size()) == pool, "level %u: %zu different of %u", level,
              seen.size(), pool);
        Question none;
        CHECK(!make(level, pool, 1, none), "level %u item past the pool", level);
        for (int i = 0; i < 4; ++i) {
            CHECK(slot[i] * 100 > total * 18 && slot[i] * 100 < total * 32,
                  "level %u slot %d holds the answer %d of %d", level, i, slot[i], total);
        }
        CHECK(rank[0] * 100 < total * 50, "level %u answer smallest %d of %d", level, rank[0], total);
        CHECK(rank[3] * 100 < total * 50, "level %u answer biggest %d of %d", level, rank[3], total);
        std::printf("level %u %-14s pool %3u  ranks %d/%d/%d/%d\n", level, levelName(level), pool,
                    rank[0], rank[1], rank[2], rank[3]);
    }

    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 200; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
