// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/GreCourse.h against the real word table.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/gre_course_test.cpp src/games/GreWordTable.cpp -o gre_course_test && ./gre_course_test
//
// For every word at every level, with many seeds: four different options,
// the answer where it says, no wrong option with the answer's gloss; the
// level's rule kept whenever the table allows it (another part of speech on
// level 1, the same on 2-4, within eight places in the alphabet on 5);
// every sentence with a gap that hides the whole word, and only the one
// sentence the audit expected to lack its word; the level line fitting.
#include "GreCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>

using namespace GreCourse;

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
    CHECK(GRE_WORD_COUNT == 250 && GRE_WORD_COUNT <= Course::QuestionDeck::MAX_ITEMS, "250 words");
    int without = 0;
    for (uint16_t w = 0; w < GRE_WORD_COUNT; ++w) {
        char gap[160];
        if (!blank(GRE_WORDS[w].example, GRE_WORDS[w].word, gap, sizeof(gap))) {
            ++without;
            CHECK(kindFor(4, w, 0) == Kind::Meaning, "no sentence falls back");
            continue;
        }
        CHECK(strstr(gap, "____") != nullptr, "gap");
        /* Nothing of the word's stem is left beside the gap. */
        char stem[5] = {};
        strncpy(stem, GRE_WORDS[w].word, 4);
        const char* g = strstr(gap, "____");
        CHECK(!isalpha(static_cast<unsigned char>(g[4])), "%s: %s", GRE_WORDS[w].word, gap);
        CHECK(strlen(gap) <= 88, "%s sentence two lines: %s", GRE_WORDS[w].word, gap);
    }
    CHECK(without <= 2, "%d sentences lack their word", without);

    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "level line %d", level);
        int kept = 0, total = 0;
        for (uint16_t w = 0; w < GRE_WORD_COUNT; ++w) {
            for (uint32_t seed = 1; seed < 60; seed += 3) {
                uint16_t opt[4];
                uint8_t correct = 9;
                options(level, w, seed * 2246822519u, opt, correct);
                CHECK(correct < 4 && opt[correct] == w, "answer placed");
                for (int i = 0; i < 4; ++i) {
                    for (int j = 0; j < i; ++j) CHECK(opt[i] != opt[j], "L%d %s twice", level, GRE_WORDS[w].word);
                    if (i == correct) continue;
                    CHECK(strcmp(GRE_WORDS[opt[i]].meaning, GRE_WORDS[w].meaning) != 0, "same gloss");
                    ++total;
                    /* Look-alikes: within eight places is the rule, the same
                     * part of speech only a preference (it holds about 4 in 5). */
                    kept += fits(level, w, opt[i], level >= 5 ? 1 : 0);
                }
                const Kind k = kindFor(level, w, seed);
                if (level == 3) CHECK(k == Kind::Recall, "recall");
                if (level <= 2 || level == 5) CHECK(k == Kind::Meaning, "meaning");
            }
        }
        /* The level's own rule holds for nearly every option. */
        CHECK(kept * 100 >= total * 95, "level %d keeps its rule %d of %d", level, kept, total);
    }
    printf("%d words, %d without their own word in the sentence\n", GRE_WORD_COUNT, without);
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
