// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/ColorMixCourse.h: the levels of Color Mix and
// the questions each one asks. The header is pure C++, and this file is what
// proves it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/color_mix_course_test.cpp -o color_mix_course_test && ./color_mix_course_test
//
// For every level and item, under many random streams: the answer is what the
// mix table says; four different buttons, exactly one of them a right answer
// -- no other button would also be true -- no two from one look-alike group,
// every colour name fits its button, every colour that is offered is a right
// answer somewhere in the course (so no button is a colour that is always
// wrong), and the right one in each slot about a quarter of the time. The
// level's questions are all different and the ladder climbs to the top.
#include "ColorMixCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <tuple>

using namespace ColorMixCourse;

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

static bool rightAnswer(const Question& q, uint8_t c) {
    if (q.backwards) return mixOf(q.left, c, q.light ? 1 : 0) == q.right;
    return mixOf(q.left, q.right, q.light ? 1 : 0) == c;
}

int main() {
    CHECK(clampLevel(0) == 1 && clampLevel(99) == LEVEL_COUNT, "clamp");
    /* Every colour that is the answer to some question in the course. */
    std::set<int> answers;
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        for (uint16_t item = 0; item < poolSize(level); ++item) {
            Question q;
            make(level, item, 1, q);
            answers.insert(q.answer);
        }
    }
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        const uint16_t pool = poolSize(level);
        CHECK(pool > 0 && pool <= Course::QuestionDeck::MAX_ITEMS, "level %u pool", level);
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "line fits: %s", line);
        std::set<std::tuple<int, int, int, int>> seen;
        int slot[4] = {};
        int total = 0;
        Rng rng(level * 104729U);
        for (uint16_t item = 0; item < pool; ++item) {
            Question first;
            CHECK(make(level, item, rng.next(), first), "level %u item %u", level, item);
            seen.insert(std::make_tuple(first.left, first.right, first.answer, first.backwards ? 1 : 0));
            for (int k = 0; k < 200; ++k) {
                Question q;
                make(level, item, rng.next(), q);
                CHECK(q.answer == first.answer && q.left == first.left, "stable");
                CHECK(rightAnswer(q, q.answer), "level %u answer is the mix", level);
                CHECK(q.light == (level == 6), "level %u light", level);
                if (level <= 2) CHECK(!q.backwards, "level %u forwards", level);
                /* The first two levels offer only colours of the first two:
                 * the six first mixes, their ingredients and the tints. */
                if (level <= 2) {
                    for (int i = 0; i < 4; ++i) {
                        bool met = false;
                        for (int r = 0; r < 16; ++r) met = met || MIXES[r].result == q.options[i];
                        CHECK(met, "level %u offers %s, not met yet", level, SWATCHES[q.options[i]].name);
                    }
                }
                if (level == 3 || level == 5) CHECK(q.backwards, "level %u backwards", level);
                if (level == 5) CHECK(q.right == BROWN, "level 5 makes brown");
                CHECK(std::strlen(q.prompt()) <= 32, "prompt fits");
                int right = 0;
                for (int i = 0; i < 4; ++i) {
                    const uint8_t c = q.options[i];
                    CHECK(c < COLOUR_COUNT, "a colour");
                    /* Buttons are 120px at font 2: thirteen characters. */
                    CHECK(std::strlen(SWATCHES[c].name) <= 13, "name fits: %s", SWATCHES[c].name);
                    CHECK(answers.count(c) == 1, "level %u offers %s, never right anywhere", level,
                          SWATCHES[c].name);
                    for (int j = i + 1; j < 4; ++j) {
                        CHECK(c != q.options[j], "level %u distinct", level);
                        const uint8_t g = SWATCHES[c].group;
                        CHECK(g == 0 || g != SWATCHES[q.options[j]].group, "look-alikes %s %s",
                              SWATCHES[c].name, SWATCHES[q.options[j]].name);
                    }
                    if (rightAnswer(q, c)) ++right;
                }
                CHECK(right == 1 && q.options[q.correct] == q.answer, "level %u exactly one true", level);
                if (level == 6 && !q.backwards) {
                    const uint8_t paint = mixOf(q.left, q.right, 0);
                    if (paint < COLOUR_COUNT) {
                        bool has = false;
                        for (int i = 0; i < 4; ++i) has = has || q.options[i] == paint;
                        CHECK(has, "light offers the paint mix");
                    }
                }
                ++slot[q.correct];
                ++total;
            }
        }
        CHECK(static_cast<int>(seen.size()) == pool, "level %u: %zu different of %u", level,
              seen.size(), pool);
        Question none;
        CHECK(!make(level, pool, 1, none), "past the pool");
        for (int i = 0; i < 4; ++i) {
            CHECK(slot[i] * 100 > total * 18 && slot[i] * 100 < total * 32,
                  "level %u slot %d holds %d of %d", level, i, slot[i], total);
        }
        std::printf("level %u %-22s pool %3u\n", level, levelName(level), pool);
    }

    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 200; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
