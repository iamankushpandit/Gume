// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/PercentCourse.h: the levels of Percent and the
// questions each one asks. The header is pure C++, and this file is what
// proves it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/percent_course_test.cpp -o percent_course_test && ./percent_course_test
//
// For every item, under many random streams: the answer is EXACT -- worked
// out here again with no division that rounds -- so 25% of 10 can never be
// "2" again; four different options, exactly one right, the level's
// commonest slip among them, every label and prompt fitting, percents never
// above 100 on a circle; the right one in each slot about a quarter of the
// time; every item of a level a different question; shading in steps the
// level's - and + can reach.
#include "PercentCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <tuple>

using namespace PercentCourse;

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
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "line fits: %s", line);
        const uint16_t pool = poolSize(level);
        CHECK(pool > 0 && pool <= Course::QuestionDeck::MAX_ITEMS, "pool");
        std::set<std::tuple<int, int, int, int, int, int>> seen;
        int slot[4] = {};
        int total = 0;
        Rng rng(level * 7919U);
        for (uint16_t item = 0; item < pool; ++item) {
            Question first;
            CHECK(make(level, item, rng.next(), first), "level %u item %u", level, item);
            seen.insert(std::make_tuple(static_cast<int>(first.mode), first.percent, first.base,
                                        first.part, first.top * 100 + first.bottom, first.more ? 1 : 0));
            for (int k = 0; k < 60; ++k) {
                Question q;
                make(level, item, rng.next(), q);
                CHECK(q.answer == first.answer, "stable");
                char p[48];
                const int pn = prompt(q, p, sizeof(p));
                CHECK(pn > 0 && pn <= 22, "prompt fits: %s", p);
                /* Exact, in whole numbers: product divisible by 100. */
                switch (q.mode) {
                    case Mode::Read:
                    case Mode::Shade:
                        CHECK(q.answer == q.percent && q.percent >= 5 && q.percent <= 100, "circle");
                        CHECK(q.percent % shadeStep(level) == 0, "level %u reachable by the step", level);
                        break;
                    case Mode::Fraction:
                        CHECK((100 * q.top) % q.bottom == 0 && q.answer * q.bottom == 100 * q.top, "fraction");
                        CHECK(has(q, q.top), "the top as the percent");
                        break;
                    case Mode::Of:
                        CHECK((q.base * q.percent) % 100 == 0 && q.answer * 100 == q.base * q.percent,
                              "level %u %u%% of %u exact", level, q.percent, q.base);
                        if (q.answer != q.base - q.answer) CHECK(has(q, q.base - q.answer), "the rest");
                        break;
                    case Mode::WhatPercent:
                        CHECK(q.answer * q.base == 100 * q.part && q.answer % 5 == 0, "what percent");
                        CHECK(has(q, q.part) || q.part == q.answer, "the part as the percent");
                        break;
                    case Mode::MoreLess: {
                        const int delta = q.base * q.percent / 100;
                        CHECK((q.base * q.percent) % 100 == 0, "exact delta");
                        CHECK(q.answer == (q.more ? q.base + delta : q.base - delta), "more and less");
                        CHECK(has(q, q.more ? q.base - delta : q.base + delta), "the other way");
                        break;
                    }
                }
                if (level <= 3) CHECK(q.mode == Mode::Read || q.mode == Mode::Shade, "circle levels");
                if (q.mode == Mode::Shade) {
                    ++total;
                    ++slot[k % 4];
                    continue;
                }
                int right = 0;
                for (int i = 0; i < 4; ++i) {
                    char label[16];
                    CHECK(optionText(q, q.options[i], label, sizeof(label)) <= 5, "label fits: %s", label);
                    CHECK(q.options[i] >= 1, "positive");
                    if (q.percents) CHECK(q.options[i] <= 100, "percent at most 100");
                    for (int j = i + 1; j < 4; ++j) CHECK(q.options[i] != q.options[j], "distinct");
                    if (q.options[i] == q.answer) ++right;
                }
                CHECK(right == 1 && q.options[q.correct] == q.answer, "level %u one right", level);
                ++slot[q.correct];
                ++total;
            }
        }
        CHECK(static_cast<int>(seen.size()) == pool, "level %u: %zu different of %u", level, seen.size(), pool);
        Question none;
        CHECK(!make(level, pool, 1, none), "past the pool");
        for (int i = 0; i < 4; ++i) {
            CHECK(slot[i] * 100 > total * 18 && slot[i] * 100 < total * 32, "level %u slot %d %d of %d",
                  level, i, slot[i], total);
        }
        std::printf("level %2u %-22s pool %3u\n", level, levelName(level), pool);
    }
    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 300; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
