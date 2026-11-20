// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/SpaceCourse.h: every question Space can ask.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/space_course_test.cpp -o space_course_test && ./space_course_test
//
// For every item of every level, with many seeds: the question within three
// word-wrapped lines of 36 characters, every option within 20, the fact
// within two lines of 48; four different options; and the generated ones
// right -- the planet asked for really is that far out, next, nearer or
// between, the rocky/gas/ice answer is the only one of its kind among the
// four, the longest or shortest year really is, and the layer above or
// below really is. Plus every level holding enough to deal, and a deck
// covering each level before repeating.
#include "SpaceCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>

using namespace SpaceCourse;

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            if (failures <= 25) {                                             \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                   \
                printf(__VA_ARGS__);                                          \
                printf("\n");                                                 \
            }                                                                 \
        }                                                                     \
    } while (0)

/* Lines `text` needs when wrapped by word at `width` characters. */
static int linesFor(const char* text, int width) {
    int lines = 1, col = 0;
    const char* p = text;
    while (*p) {
        while (*p == ' ') ++p;
        int w = 0;
        while (p[w] && p[w] != ' ') ++w;
        if (w == 0) break;
        if (col == 0) {
            col = w;
        } else if (col + 1 + w <= width) {
            col += 1 + w;
        } else {
            ++lines;
            col = w;
        }
        p += w;
    }
    return lines;
}

static int indexOf(const char* const* names, int n, const char* s) {
    for (int i = 0; i < n; ++i)
        if (strcmp(names[i], s) == 0) return i;
    return -1;
}

int main() {
    int total = 0;
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "level line %d", level);
        const uint16_t pool = poolSize(level);
        total += pool;
        CHECK(pool >= 14 && pool <= Course::QuestionDeck::MAX_ITEMS, "level %d pool %d", level, pool);
        for (uint16_t item = 0; item < pool; ++item) {
            for (uint32_t seed = 1; seed < 400; seed += 7) {
                Question q;
                make(level, item, seed * 2654435761u, q);
                CHECK(linesFor(q.text, 36) <= 3, "L%d question too long: %s", level, q.text);
                CHECK(linesFor(q.fact, 48) <= 2, "L%d fact too long: %s", level, q.fact);
                for (int o = 0; o < 4; ++o) {
                    CHECK(strlen(q.options[o]) >= 1 && strlen(q.options[o]) <= 20,
                          "L%d option '%s'", level, q.options[o]);
                    for (int p = 0; p < o; ++p)
                        CHECK(strcmp(q.options[o], q.options[p]) != 0, "L%d same option twice: %s / %s",
                              level, q.text, q.options[o]);
                }
                const bool generated = item >= rowCount(level);
                if (!generated) continue;
                const int a = indexOf(PLANETS, 8, q.options[0]);
                if (level == 2) {
                    CHECK(a >= 0, "planet answer");
                    for (int o = 0; o < 8; ++o) {
                        char want[TEXT_MAX];
                        snprintf(want, sizeof(want), "Which planet is %s from the Sun?", ORDINALS[o]);
                        if (strcmp(q.text, want) == 0) CHECK(a == o, "%s -> %s", q.text, q.options[0]);
                        snprintf(want, sizeof(want), "Which planet comes next after %s?", PLANETS[o]);
                        if (strcmp(q.text, want) == 0) CHECK(a == o + 1, "%s -> %s", q.text, q.options[0]);
                        snprintf(want, sizeof(want), "Which planet is just nearer the Sun than %s?", PLANETS[o]);
                        if (strcmp(q.text, want) == 0) CHECK(a == o - 1, "%s -> %s", q.text, q.options[0]);
                        if (o + 2 < 8) {
                            snprintf(want, sizeof(want), "Which planet lies between %s and %s?",
                                     PLANETS[o], PLANETS[o + 2]);
                            if (strcmp(q.text, want) == 0) CHECK(a == o + 1, "%s -> %s", q.text, q.options[0]);
                        }
                    }
                    for (int o = 1; o < 4; ++o) CHECK(indexOf(PLANETS, 8, q.options[o]) >= 0, "planet option");
                } else if (level == 3) {
                    CHECK(a >= 0, "planet answer");
                    for (int o = 1; o < 4; ++o) {
                        const int b = indexOf(PLANETS, 8, q.options[o]);
                        CHECK(b >= 0 && PLANET_GROUP[b] != PLANET_GROUP[a], "%s: %s is also one",
                              q.text, q.options[o]);
                    }
                } else if (level == 6) {
                    const bool longest = strstr(q.text, "longest") != nullptr;
                    for (int o = 1; o < 4; ++o) {
                        const int b = indexOf(PLANETS, 8, q.options[o]);
                        CHECK(b >= 0 && (longest ? b < a : b > a), "%s: %s", q.text, q.options[o]);
                    }
                } else if (level == 5) {
                    const int l = indexOf(LAYERS, 5, q.options[0]);
                    CHECK(l >= 0, "layer answer");
                    for (int x = 0; x < 5; ++x) {
                        char want[TEXT_MAX];
                        snprintf(want, sizeof(want), "Which layer of air is just above the %s?", LAYERS_LOWER[x]);
                        if (strcmp(q.text, want) == 0) CHECK(l == x + 1, "%s -> %s", q.text, q.options[0]);
                        snprintf(want, sizeof(want), "Which layer of air is just below the %s?", LAYERS_LOWER[x]);
                        if (strcmp(q.text, want) == 0) CHECK(l == x - 1, "%s -> %s", q.text, q.options[0]);
                    }
                }
            }
        }
        /* The generated questions of a level are all different, question
         * and answer together. */
        for (uint16_t i = rowCount(level); i < pool; ++i) {
            for (uint16_t j = rowCount(level); j < i; ++j) {
                Question a, b;
                make(level, i, 5, a);
                make(level, j, 5, b);
                CHECK(strcmp(a.text, b.text) != 0 || strcmp(a.options[0], b.options[0]) != 0,
                      "L%d generated twice: %s %s", level, a.text, a.options[0]);
            }
        }
        Course::QuestionDeck deck;
        deck.reset(pool);
        bool seen[256] = {};
        int distinct = 0;
        uint32_t s = 11u + level;
        for (uint16_t k = 0; k < pool; ++k) {
            s = s * 1664525u + 1013904223u;
            const uint16_t d = deck.next(s);
            if (d < 256 && !seen[d]) {
                seen[d] = true;
                ++distinct;
            }
        }
        CHECK(distinct >= pool - pool / 8, "level %d deck %d of %d", level, distinct, pool);
    }
    /* Every written row is on a level, and no question is written twice. */
    for (uint16_t i = 0; i < ROW_COUNT; ++i) {
        CHECK(ROWS[i].level >= 1 && ROWS[i].level <= LEVEL_COUNT, "row level");
        for (uint16_t j = 0; j < i; ++j)
            CHECK(strcmp(ROWS[i].question, ROWS[j].question) != 0, "written twice: %s", ROWS[i].question);
    }
    printf("%d questions over %d levels (%d written)\n", total, LEVEL_COUNT, ROW_COUNT);
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
