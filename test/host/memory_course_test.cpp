// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/MemoryCourse.h: the boards Memory deals at each
// level. The header is pure C++, and this file is what proves it, since it
// will not compile otherwise.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/memory_course_test.cpp -o memory_course_test && ./memory_course_test
//
// For every level, over many boards dealt one after another as the game deals
// them: the board shape, every pair exactly twice, no face on two pairs, no
// two cards of different pairs showing the same text, a sum that makes its
// number, every face short enough for a card, every face of the level seen
// before one comes back, the pass rule, and the ladder climbing to the top.
#include "MemoryCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace MemoryCourse;

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
    uint32_t seed = 12345;
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        Course::QuestionDeck deck;
        deck.reset(poolSize(level));
        const uint16_t pool = poolSize(level);
        int seenCount[64] = {};
        bool seen[64] = {};
        int distinctBeforeRepeat = 0;
        bool repeated = false;
        for (int board = 0; board < 4000; ++board) {
            seed = seed * 1664525u + 1013904223u;
            Board b;
            deal(level, deck, seed, b);
            CHECK(b.rows == LEVELS[level - 1].rows && b.cols == LEVELS[level - 1].cols,
                  "level %d shape", level);
            CHECK(b.pairs * 2 == b.total() && b.pairs <= MAX_PAIRS, "level %d pairs", level);
            int count[MAX_PAIRS] = {};
            for (uint8_t i = 0; i < b.total(); ++i) {
                CHECK(b.pairOf(i) < b.pairs, "pair index");
                if (b.pairOf(i) < b.pairs) ++count[b.pairOf(i)];
            }
            for (uint8_t p = 0; p < b.pairs; ++p) {
                CHECK(count[p] == 2, "level %d pair %d appears %d times", level, p, count[p]);
                CHECK(b.item[p] < pool, "item in pool");
                for (uint8_t q = 0; q < p; ++q) {
                    CHECK(b.item[p] != b.item[q], "level %d face dealt twice", level);
                }
                if (!seen[b.item[p]]) {
                    seen[b.item[p]] = true;
                    if (!repeated) ++distinctBeforeRepeat;
                } else {
                    repeated = true;
                }
                ++seenCount[b.item[p]];
            }
            /* Two cards show the same text only if they are one pair. */
            for (uint8_t i = 0; i < b.total(); ++i) {
                char fi[FACE_MAX + 2];
                b.faceOf(i, fi, sizeof(fi));
                CHECK(strlen(fi) >= 1 && strlen(fi) <= FACE_MAX, "face length '%s'", fi);
                for (uint8_t j = 0; j < i; ++j) {
                    char fj[FACE_MAX + 2];
                    b.faceOf(j, fj, sizeof(fj));
                    if (b.pairOf(i) != b.pairOf(j)) {
                        CHECK(strcmp(fi, fj) != 0, "level %d two pairs show '%s'", level, fi);
                    }
                }
                /* Level 8: the two halves differ, and a sum makes its number. */
                if (level == 8) {
                    const uint8_t p = b.pairOf(i);
                    char a[FACE_MAX + 2], c[FACE_MAX + 2];
                    face(level, b.item[p], 0, b.variant[p], a, sizeof(a));
                    face(level, b.item[p], 1, b.variant[p], c, sizeof(c));
                    CHECK(strcmp(a, c) != 0, "level 8 halves alike '%s'", a);
                    const char* plus = strchr(c, '+');
                    if (plus != nullptr) {
                        CHECK(atoi(c) + atoi(plus + 1) == atoi(a), "sum %s for %s", c, a);
                        CHECK(atoi(c) >= 1 && atoi(plus + 1) >= 1, "sum parts %s", c);
                    }
                } else {
                    char a[FACE_MAX + 2], c[FACE_MAX + 2];
                    const uint8_t p = b.pairOf(i);
                    face(level, b.item[p], 0, b.variant[p], a, sizeof(a));
                    face(level, b.item[p], 1, b.variant[p], c, sizeof(c));
                    CHECK(strcmp(a, c) == 0, "level %d halves differ", level);
                }
            }
        }
        /* Every face of the level is shown before most come back: the deck
         * holds back an eighth of a pass. */
        CHECK(distinctBeforeRepeat >= pool - pool / 8 - MAX_PAIRS,
              "level %d: %d distinct faces before a repeat, pool %d", level,
              distinctBeforeRepeat, pool);
        for (uint16_t i = 0; i < pool; ++i) {
            CHECK(seenCount[i] > 0, "level %d face %d never dealt", level, i);
        }
    }

    /* Level 7's faces are all from look-alike families. */
    CHECK(strlen(LOOKALIKE) == LOOKALIKE_COUNT, "lookalike count");
    CHECK(strlen(PLAIN) == PLAIN_COUNT, "plain count");
    CHECK(strlen(BELONG_UPPER) == BELONG_LETTERS && strlen(BELONG_LOWER) == BELONG_LETTERS,
          "belong count");

    /* The pass rule. */
    for (uint8_t pairs = 3; pairs <= MAX_PAIRS; ++pairs) {
        CHECK(judge(pairs, pairs) == 1, "perfect is right");
        CHECK(judge(pairs, pairs * 2) == 1, "twice is right");
        CHECK(judge(pairs, pairs * 2 + 1) == 0, "just over is nothing");
        CHECK(judge(pairs, pairs * 3) == 0, "three times is nothing");
        CHECK(judge(pairs, pairs * 3 + 1) == -1, "over three times is wrong");
    }
    CHECK(!keepsBest(1) && !keepsBest(5) && keepsBest(6) && keepsBest(7) && keepsBest(8),
          "best only on the full board");

    /* Names fit the level line, and the ladder reaches the top. */
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[LEVEL_LINE_MAX + 8];
        const int n = levelLine(level, line, sizeof(line));
        CHECK(n > 0 && n <= LEVEL_LINE_MAX, "level line %d too long", level);
    }
    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 6 * LEVEL_COUNT; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "ladder climbs");

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
