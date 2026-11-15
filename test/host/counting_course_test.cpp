// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/CountingCourse.h: the levels of Counting and
// the questions each one asks. Like engine/Course.h, the header is pure C++,
// and this file is what proves it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/counting_course_test.cpp -o counting_course_test && ./counting_course_test
//
// What it checks, for every level and every item of its pool, under many
// random streams: the item makes a question; the question is the one the
// level says it asks; the picture fits what the panel can draw; the four
// options are four different numbers the buttons can show, exactly one of
// them right; the likeliest mistake is among them where the level promises
// it; and the right answer does not sit in the same place, or at the same
// rank, every time. Then that no two items of a level are the same question,
// so a deck that deals every item deals every question, and that a player who
// gets everything right climbs to the top.
#include "CountingCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <tuple>

using namespace CountingCourse;

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

/* What each level should ask for item `item`, worked out again here rather
 * than read back from the header. */
struct Expect {
    Picture picture;
    Ask ask;
    int shown;
    int groupSize;
    int answer;
    int likelyMistake;   // -1: none promised
};

static Expect expected(uint8_t level, uint16_t item) {
    switch (level) {
        case 1: return {Picture::Row, Ask::HowMany, item + 1, 1, item + 1, -1};
        case 2: return {Picture::Fives, Ask::HowMany, item + 1, 1, item + 1, -1};
        case 3: return {Picture::Scatter, Ask::HowMany, item, 1, item, -1};
        case 4: return {Picture::Tens, Ask::HowMany, item + 6, 1, item + 6, -1};
        case 5: {
            const int n = 1 + item / 2;
            const bool more = item % 2 == 0;
            return {Picture::Tens, more ? Ask::OneMore : Ask::OneLess, n, 1,
                    more ? n + 1 : n - 1, n};
        }
        case 6: {
            const int n = item + 10;
            const int reversed = (n % 10) * 10 + n / 10;
            return {Picture::Rods, Ask::HowManyDots, n, 10, n, reversed == n ? -1 : reversed};
        }
        default: {
            int k, step;
            if (item < 10) { k = item + 1; step = 2; }
            else if (item < 20) { k = item - 9; step = 5; }
            else { k = item - 19; step = 10; }
            return {step == 10 ? Picture::Rods : Picture::Groups, Ask::InAll, k * step, step,
                    k * step, k};
        }
    }
}

static int bits(uint32_t v) {
    int n = 0;
    while (v) {
        n += v & 1U;
        v >>= 1;
    }
    return n;
}

/* Everything the panel can show, for one picture. */
static bool fitsPanel(const Question& q) {
    switch (q.picture) {
        case Picture::Row: return q.shown >= 1 && q.shown <= ROW_MAX;
        case Picture::Fives: return q.shown >= 1 && q.shown <= FIVES_PER_ROW * FIVES_ROWS;
        case Picture::Scatter:
            return q.shown <= SCATTER_CELLS && bits(q.places) == q.shown &&
                   (q.places >> SCATTER_CELLS) == 0;
        case Picture::Tens: return q.shown >= 1 && q.shown <= TENS_PER_ROW * TENS_ROWS;
        case Picture::Rods:
            return q.groupSize == 10 && q.shown / 10 >= 1 && q.shown / 10 <= RODS_MAX &&
                   q.shown % 10 <= ONES_MAX;
        case Picture::Groups:
            return (q.groupSize == 2 || q.groupSize == 5) && q.shown % q.groupSize == 0 &&
                   q.shown / q.groupSize >= 1 && q.shown / q.groupSize <= GROUPS_MAX;
    }
    return false;
}

static void testLevels() {
    const uint16_t pools[LEVEL_COUNT] = {5, 10, 11, 15, 38, 90, 29};
    char line[64];
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        CHECK(poolSize(level) == pools[level - 1], "level %u pool %u, expected %u", level,
              poolSize(level), pools[level - 1]);
        CHECK(poolSize(level) <= Course::QuestionDeck::MAX_ITEMS, "level %u too big for a deck",
              level);
        const int len = levelLine(level, line, sizeof(line));
        CHECK(len > 0 && len <= LEVEL_LINE_MAX, "level %u line \"%s\" is %d, max %u", level,
              line, len, LEVEL_LINE_MAX);
        CHECK(std::strncmp(line, "Level ", 6) == 0, "level line \"%s\"", line);
    }
    CHECK(poolSize(0) == poolSize(1) && poolSize(200) == poolSize(LEVEL_COUNT),
          "levels outside the ladder are its ends");
    Question q;
    CHECK(!make(1, poolSize(1), 7, q), "an item past the pool is refused");
}

static void testQuestions() {
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        const uint16_t pool = poolSize(level);
        int slotCount[4] = {};
        int rankCount[4] = {};
        int samples = 0;
        int mistakeOffered = 0;
        int mistakePromised = 0;
        for (uint16_t item = 0; item < pool; ++item) {
            const Expect e = expected(level, item);
            Rng rng(level * 1000003U + item * 7919U + 1);
            /* At least a few thousand questions a level, however small the
             * pool. */
            const int streams = 4000 / pool + 50;
            for (int r = 0; r < streams; ++r) {
                Question q;
                const bool made = make(level, item, rng.next(), q);
                CHECK(made, "level %u item %u not made", level, item);
                if (!made) return;
                ++samples;
                CHECK(q.picture == e.picture && q.ask == e.ask && q.shown == e.shown &&
                          q.groupSize == e.groupSize && q.answer == e.answer,
                      "level %u item %u: shown %u answer %u, expected %d and %d", level, item,
                      q.shown, q.answer, e.shown, e.answer);
                CHECK(fitsPanel(q), "level %u item %u: %u objects do not fit the panel", level,
                      item, q.shown);
                if (q.picture != Picture::Scatter) {
                    CHECK(q.places == 0, "level %u item %u: places on a fixed picture", level,
                          item);
                }
                int right = 0;
                bool mistake = false;
                for (int i = 0; i < 4; ++i) {
                    CHECK(q.options[i] <= OPTION_MAX, "level %u item %u: option %u", level, item,
                          q.options[i]);
                    if (q.options[i] == q.answer) ++right;
                    if (q.options[i] == e.likelyMistake) mistake = true;
                    for (int j = 0; j < i; ++j) {
                        CHECK(q.options[i] != q.options[j],
                              "level %u item %u: %u offered twice", level, item, q.options[i]);
                    }
                }
                CHECK(right == 1, "level %u item %u: %d right options", level, item, right);
                CHECK(q.correct < 4 && q.options[q.correct] == q.answer,
                      "level %u item %u: correct slot %u holds %u", level, item, q.correct,
                      q.options[q.correct]);
                if (e.likelyMistake >= 0) {
                    ++mistakePromised;
                    if (mistake) ++mistakeOffered;
                }
                ++slotCount[q.correct];
                int rank = 0;
                for (int i = 0; i < 4; ++i) {
                    if (q.options[i] < q.answer) ++rank;
                }
                ++rankCount[rank];
            }
        }
        /* The slot is a shuffle: each about a quarter. */
        for (int i = 0; i < 4; ++i) {
            CHECK(slotCount[i] * 5 > samples, "level %u: slot %d right %d of %d", level, i,
                  slotCount[i], samples);
        }
        /* Nor does the answer give itself away by always being the second
         * smallest, say. No rank may hold half the questions: level 7 once
         * put the answer between its two neighbouring multiples 59% of the
         * time, because "both above" quietly turned into "one either side". */
        int ranks = 0;
        for (int i = 0; i < 4; ++i) {
            if (rankCount[i] > 0) ++ranks;
            CHECK(rankCount[i] * 2 < samples, "level %u: rank %d is %d of %d", level, i,
                  rankCount[i], samples);
        }
        CHECK(ranks >= 3, "level %u: the answer sits at only %d ranks", level, ranks);
        CHECK(mistakeOffered == mistakePromised,
              "level %u: the likeliest mistake offered %d of %d times", level, mistakeOffered,
              mistakePromised);
        std::printf("level %u %-20s pool %3u  %6d questions  ranks %d/%d/%d/%d\n", level,
                    levelName(level), pool, samples, rankCount[0], rankCount[1], rankCount[2],
                    rankCount[3]);
    }
}

static void testItemsAreDifferentQuestions() {
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        std::set<std::tuple<int, int, int, int>> seen;
        for (uint16_t item = 0; item < poolSize(level); ++item) {
            Question q;
            make(level, item, 12345, q);
            const auto key = std::make_tuple(static_cast<int>(q.picture), static_cast<int>(q.ask),
                                             static_cast<int>(q.shown),
                                             static_cast<int>(q.groupSize));
            CHECK(seen.insert(key).second, "level %u item %u repeats an earlier question", level,
                  item);
        }
    }
}

static void testScatterVaries() {
    /* Six scattered objects must not settle into one arrangement. */
    std::set<uint32_t> shapes;
    Rng rng(31337);
    for (int i = 0; i < 400; ++i) {
        Question q;
        make(3, 6, rng.next(), q);
        shapes.insert(q.places);
    }
    CHECK(shapes.size() > 300, "six scattered objects came in only %zu arrangements",
          shapes.size());
    /* And the same random number gives the same picture, which is what lets
     * a repaint show what was there. */
    Question a, b;
    make(3, 7, 99, a);
    make(3, 7, 99, b);
    CHECK(a.places == b.places && std::memcmp(a.options, b.options, 4) == 0,
          "one question, two pictures");
}

static void testLadderThroughTheCourse() {
    /* Everything right: one level every six answers, to the top. */
    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    int answers = 0;
    while (!ladder.atTop() && answers < 1000) {
        ladder.answer(true);
        ++answers;
    }
    CHECK(ladder.level() == LEVEL_COUNT && answers == (LEVEL_COUNT - 1) * Course::Ladder::UP_AT,
          "all right reached level %u after %d", ladder.level(), answers);
    /* Everything wrong: back to the bottom, and no lower. */
    for (int i = 0; i < 100; ++i) ladder.answer(false);
    CHECK(ladder.level() == 1, "all wrong left level %u", ladder.level());

    /* A deck over each level's pool covers every question in one pass and
     * the items it held back. */
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        Course::QuestionDeck deck;
        deck.reset(poolSize(level));
        std::set<int> answered;
        Rng rng(level);
        const uint16_t pool = poolSize(level);
        for (int n = 0; n < pool + pool / 8; ++n) answered.insert(deck.next(rng.next()));
        CHECK(static_cast<int>(answered.size()) == pool, "level %u: %zu of %u items dealt",
              level, answered.size(), pool);
    }
}

int main() {
    testLevels();
    testQuestions();
    testItemsAreDifferentQuestions();
    testScatterVaries();
    testLadderThroughTheCourse();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
