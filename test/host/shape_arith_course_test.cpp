// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/ShapeArithCourse.h: the levels of Shape Arith
// and the questions each one asks. The header is pure C++, and this file is
// what proves it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/shape_arith_course_test.cpp -o shape_arith_course_test && ./shape_arith_course_test
//
// For every level and every item of its pool, under many random streams: the
// item makes a question; the question is the kind the level says it asks,
// inside the level's range; the panels can draw it; the four options are four
// different numbers, exactly one right, the likeliest mistake among them; and
// the right answer does not sit in the same slot, or at the same rank, every
// time. Then that no two items of a level are the same question, and that a
// player who gets everything right climbs to the top.
#include "ShapeArithCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <set>
#include <tuple>

using namespace ShapeArithCourse;

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        ++checks;                                                         \
        if (!(cond)) {                                                    \
            ++failures;                                                   \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                     \
            std::printf("\n");                                            \
        }                                                                 \
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

/* What each level promises, checked against the question it made. */
static bool inRange(uint8_t level, const Question& q) {
    switch (level) {
        case 1: return q.op == Op::Add && q.a >= 1 && q.b >= 1 && q.a + q.b <= 5;
        case 2: return q.op == Op::TakeAway && q.a >= 2 && q.a <= 5 && q.b >= 1 && q.b < q.a;
        case 3: return q.op == Op::Add && q.a >= 1 && q.b >= 1 && q.a + q.b >= 6 && q.a + q.b <= 10;
        case 4: return q.op == Op::TakeAway && q.a >= 6 && q.a <= 10 && q.b >= 1 && q.b < q.a;
        case 5:
            return (q.op == Op::Add && (q.a == 0) != (q.b == 0) && q.a + q.b <= 10) ||
                   (q.op == Op::TakeAway && q.a >= 1 && q.a <= 10 && (q.b == 0 || q.b == q.a));
        case 6:
            return q.op == Op::MakeUp && q.whole >= 5 && q.whole <= 10 && q.a >= 1 &&
                   q.a + q.b == q.whole && q.b >= 1;
        case 7: return q.op == Op::Add && q.a >= 2 && q.a <= 9 && q.b >= 2 && q.b <= 9 && q.a + q.b >= 11;
        default:
            return q.op == Op::TakeAway && q.a >= 11 && q.a <= 18 && q.b >= 2 && q.b <= 9 &&
                   q.a - q.b >= 2 && q.a - q.b <= 9;
    }
}

static int rightAnswer(const Question& q) {
    switch (q.op) {
        case Op::Add: return q.a + q.b;
        case Op::TakeAway: return q.a - q.b;
        case Op::MakeUp: break;
    }
    return q.whole - q.a;
}

int main() {
    CHECK(clampLevel(0) == 1 && clampLevel(99) == LEVEL_COUNT, "clamp");
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        const uint16_t pool = poolSize(level);
        CHECK(pool > 0 && pool <= Course::QuestionDeck::MAX_ITEMS, "level %u pool %u", level, pool);
        char line[LEVEL_LINE_MAX + 1];
        const int n = levelLine(level, line, sizeof(line));
        CHECK(n > 0 && n <= LEVEL_LINE_MAX, "level %u line fits", level);

        std::set<std::tuple<int, int, int, int>> seen;
        int slot[4] = {};
        int rank[4] = {};
        int total = 0;
        int boundary = 0;   // answers that are the smallest the level can ask
        Rng rng(level * 7919U);
        for (uint16_t item = 0; item < pool; ++item) {
            Question first;
            CHECK(make(level, item, rng.next(), first), "level %u item %u makes", level, item);
            seen.insert(std::make_tuple(static_cast<int>(first.op), first.a, first.b, first.whole));
            for (int k = 0; k < 64; ++k) {
                Question q;
                make(level, item, rng.next(), q);
                CHECK(q.op == first.op && q.a == first.a && q.b == first.b && q.whole == first.whole,
                      "level %u item %u depends only on the item", level, item);
                CHECK(inRange(level, q), "level %u item %u op %d %u,%u,%u", level, item,
                      static_cast<int>(q.op), q.a, q.b, q.whole);
                CHECK(q.leftCount() <= PANEL_MAX && q.rightCount() <= SMALL_GRID,
                      "level %u item %u fits the panels", level, item);
                CHECK(q.answer == rightAnswer(q), "level %u item %u answer", level, item);
                int right = 0;
                for (int i = 0; i < 4; ++i) {
                    CHECK(q.options[i] <= OPTION_MAX, "option range");
                    for (int j = i + 1; j < 4; ++j) {
                        CHECK(q.options[i] != q.options[j], "level %u item %u distinct", level, item);
                    }
                    if (q.options[i] == q.answer) ++right;
                }
                CHECK(right == 1 && q.options[q.correct] == q.answer,
                      "level %u item %u exactly one right answer", level, item);
                /* The likeliest mistake is offered whenever it is a different
                 * number from the answer. */
                if (q.op == Op::TakeAway) {
                    CHECK((q.b == q.answer || has(q, q.b)) || (q.a == q.answer || has(q, q.a)),
                          "level %u item %u offers a take-away mistake", level, item);
                } else if (q.op == Op::MakeUp) {
                    CHECK(has(q, q.a) || has(q, q.whole), "level %u item %u offers a make-up mistake",
                          level, item);
                } else {
                    const int bigger = q.a > q.b ? q.a : q.b;
                    CHECK(has(q, bigger) || has(q, q.answer + 1) || bigger == q.answer,
                          "level %u item %u offers an adding mistake", level, item);
                }
                ++slot[q.correct];
                int below = 0;
                for (int i = 0; i < 4; ++i) {
                    if (q.options[i] < q.answer) ++below;
                }
                ++rank[below];
                if (q.answer == 0) ++boundary;
                ++total;
            }
        }
        CHECK(static_cast<int>(seen.size()) == pool, "level %u: %zu different questions of %u", level,
              seen.size(), pool);
        for (int i = 0; i < 4; ++i) {
            CHECK(slot[i] * 100 > total * 18 && slot[i] * 100 < total * 32,
                  "level %u slot %d holds the answer %d of %d", level, i, slot[i], total);
        }
        /* Not always the smallest and not always the biggest: neither end of
         * the four may hold the answer more than half the time, leaving out
         * the answers that are the smallest number there is. */
        CHECK((rank[0] - boundary) * 100 < (total - boundary) * 50,
              "level %u answer is the smallest %d of %d", level, rank[0], total);
        CHECK(rank[3] * 100 < total * 50, "level %u answer is the biggest %d of %d", level, rank[3], total);
        std::printf("level %u %-18s pool %3u  ranks %d/%d/%d/%d\n", level, levelName(level), pool,
                    rank[0], rank[1], rank[2], rank[3]);
    }

    /* A player who answers everything right climbs to the top. */
    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 200; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
