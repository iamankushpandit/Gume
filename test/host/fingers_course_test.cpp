// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/FingersCourse.h: the levels of Fingers and the
// questions each one asks. The header is pure C++, and this file is what
// proves it, since it will not compile otherwise.
//
// Run by CI (tools/run_host_tests.py), not by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/fingers_course_test.cpp -o fingers_course_test && ./fingers_course_test
//
// For every level and every item, under many random streams: the item makes
// a question of the kind the level says, with the hands in the state the
// level promises; a question answered with the hands asks for a number that
// is not already showing; a number question has four different options,
// exactly one right, the level's likeliest mistake among them, and the right
// one neither in the same slot nor at the same rank every time. Then that no
// two items of a level are the same question, and that a player who gets
// everything right climbs to the top.
#include "FingersCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <set>
#include <tuple>

using namespace FingersCourse;

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

int main() {
    CHECK(clampLevel(0) == 1 && clampLevel(99) == LEVEL_COUNT, "clamp");
    CHECK(inOrder(6) == 0x3F && countUp(inOrder(7)) == 7 && countUp(ALL_UP) == 10, "in order");
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        const uint16_t pool = poolSize(level);
        CHECK(pool > 0 && pool <= Course::QuestionDeck::MAX_ITEMS, "level %u pool", level);
        char line[LEVEL_LINE_MAX + 1];
        const int n = levelLine(level, line, sizeof(line));
        CHECK(n > 0 && n <= LEVEL_LINE_MAX, "level %u line fits", level);

        std::set<std::tuple<int, int, int>> seen;
        int slot[4] = {};
        int rank[4] = {};
        int total = 0;
        int atBottom = 0;
        int atTop = 0;
        const int lowest = (level == 4 || level == 5) ? 0 : 1;
        Rng rng(level * 104729U);
        for (uint16_t item = 0; item < pool; ++item) {
            Question first;
            CHECK(make(level, item, rng.next(), first), "level %u item %u makes", level, item);
            const bool scattered = level == 3 || level == 4;
            seen.insert(std::make_tuple(static_cast<int>(first.ask), first.answer,
                                        scattered ? countUp(first.raised) : first.raised));
            for (int k = 0; k < 64; ++k) {
                Question q;
                make(level, item, rng.next(), q);
                CHECK(q.ask == first.ask && q.answer == first.answer, "level %u item %u stable",
                      level, item);
                CHECK((q.raised & ~ALL_UP) == 0, "level %u item %u ten fingers", level, item);
                const uint8_t up = countUp(q.raised);
                switch (q.ask) {
                    case Ask::HowMany:
                        CHECK(q.answer == up && up >= 1 && up <= 10, "level %u how many", level);
                        if (level <= 2) CHECK(q.raised == inOrder(up), "level %u in order", level);
                        if (level == 1) CHECK(up <= 5, "level 1 is one hand");
                        if (level == 2) CHECK(up >= 6, "level 2 is a hand and more");
                        break;
                    case Ask::ShowMe:
                        CHECK(q.raised == 0 && q.answer >= 1 && q.answer <= 10, "level %u show", level);
                        CHECK(q.countsAloud == (level <= 2), "level %u counts aloud", level);
                        break;
                    case Ask::HowManyDown:
                        CHECK(q.answer == 10 - up, "level %u down", level);
                        break;
                    case Ask::InAll:
                        CHECK(q.answer == q.left + q.right && q.left <= 5 && q.right <= 5 &&
                                  up == q.answer && (q.raised & 0x1F) == inOrder(q.left),
                              "level %u in all", level);
                        break;
                    case Ask::MakeIt:
                        CHECK(q.answer <= 10 && up != q.answer && q.raised == inOrder(up),
                              "level %u make it", level);
                        break;
                }
                if (showsWithHands(q.ask)) continue;
                int right = 0;
                for (int i = 0; i < 4; ++i) {
                    CHECK(q.options[i] >= lowest && q.options[i] <= 10, "level %u option range", level);
                    for (int j = i + 1; j < 4; ++j) {
                        CHECK(q.options[i] != q.options[j], "level %u distinct", level);
                    }
                    if (q.options[i] == q.answer) ++right;
                }
                CHECK(right == 1 && q.options[q.correct] == q.answer, "level %u one right", level);
                if (q.ask == Ask::HowManyDown && up != q.answer) {
                    CHECK(has(q, up), "level 4 offers the ones up");
                }
                if (q.ask == Ask::InAll) {
                    const int bigger = q.left > q.right ? q.left : q.right;
                    CHECK(bigger == q.answer || has(q, bigger), "level 5 offers one hand");
                }
                if (level == 2) CHECK(has(q, q.answer - 5), "level 2 offers the second hand alone");
                ++slot[q.correct];
                int below = 0;
                for (int i = 0; i < 4; ++i) {
                    if (q.options[i] < q.answer) ++below;
                }
                ++rank[below];
                if (q.answer == lowest) ++atBottom;
                if (q.answer == 10) ++atTop;
                ++total;
            }
        }
        CHECK(static_cast<int>(seen.size()) == pool, "level %u: %zu different questions of %u", level,
              seen.size(), pool);
        if (total > 0) {
            for (int i = 0; i < 4; ++i) {
                CHECK(slot[i] * 100 > total * 18 && slot[i] * 100 < total * 32,
                      "level %u slot %d holds the answer %d of %d", level, i, slot[i], total);
            }
            CHECK((rank[0] - atBottom) * 100 < (total - atBottom) * 50,
                  "level %u answer is the smallest %d of %d", level, rank[0], total);
            CHECK((rank[3] - atTop) * 100 < (total - atTop) * 50,
                  "level %u answer is the biggest %d of %d", level, rank[3], total);
        }
        std::printf("level %u %-18s pool %3u  ranks %d/%d/%d/%d\n", level, levelName(level), pool,
                    rank[0], rank[1], rank[2], rank[3]);
    }

    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 200; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
