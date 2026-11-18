// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/TimeCourse.h: the levels of Time and the
// questions each one asks. The header is pure C++, and this file is what
// proves it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/time_course_test.cpp -o time_course_test && ./time_course_test
//
// For every numbered level and item, and for many draws of the two drawn
// levels, under many random streams: the question is the kind the level says,
// on the minutes the level says; it has four different options that fit on a
// button, exactly one right, the level's commonest mistake among them, none
// within three minutes of another where the dial is read to the minute, and
// the right one neither in one slot nor at one rank most of the time. Then
// that no two items of a level are the same question, and that the ladder
// climbs to the top.
#include "TimeCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>
#include <set>

using namespace TimeCourse;

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

static int dial(int a, int b) {
    int d = a > b ? a - b : b - a;
    if (d > 360) d = 720 - d;
    return d;
}

struct Tally {
    int slot[4] = {};
    int rank[4] = {};
    int total = 0;
};

/* Everything a question of `level` must be, whichever way it was made. */
static void checkQuestion(uint8_t level, const Question& q, Tally& t) {
    const int m = q.shown % 60;
    const int h = q.shown / 60;
    CHECK(q.shown < HALF_DAY, "level %u shown in range", level);
    CHECK(q.minuteMarks == (level >= 6), "level %u marks", level);
    switch (level) {
        case 1: CHECK(m == 0, "level 1 on the hour"); break;
        case 2: CHECK(m == 0 || m == 30, "level 2 half past"); break;
        case 3: CHECK(m % 15 == 0, "level 3 quarters"); break;
        case 4: CHECK(m % 5 == 0 && m >= 5 && m <= 30, "level 4 minutes past"); break;
        case 5: CHECK(m % 5 == 0 && m >= 35, "level 5 minutes to"); break;
        case 6: CHECK(m % 5 != 0, "level 6 not on a five"); break;
        case 7: CHECK(m % 5 == 0 && q.label == Label::Words, "level 7 words"); break;
        case 8:
            CHECK(m % 5 == 0 && q.label == Label::Day, "level 8 day");
            if (q.afterMidnight) {
                CHECK(h == 0 && q.answer == m, "level 8 midnight");
            } else {
                CHECK(h >= 1 && q.answer == (h + 12) * 60 + m, "level 8 p.m.");
            }
            break;
        case 9:
            CHECK(m % 5 == 0 && q.change != 0, "level 9 change");
            CHECK(q.answer == onClock(q.shown + q.change), "level 9 answer");
            break;
        case 10:
            CHECK(q.label == Label::Minutes && m % 5 == 0 && m > 0 && q.answer == 60 - m,
                  "level 10 until");
            break;
    }
    char prompt[64];
    const int pn = TimeCourse::prompt(q, prompt, sizeof(prompt));
    CHECK(pn > 0 && pn <= PROMPT_MAX, "level %u prompt fits: %s", level, prompt);

    int right = 0;
    for (int i = 0; i < 4; ++i) {
        char label[32];
        const int n = format(q.label, q.options[i], label, sizeof(label));
        CHECK(n > 0 && n < LABEL_MAX, "level %u label fits: %s", level, label);
        for (int j = i + 1; j < 4; ++j) {
            CHECK(q.options[i] != q.options[j], "level %u distinct", level);
            char other[32];
            format(q.label, q.options[j], other, sizeof(other));
            CHECK(std::strcmp(label, other) != 0, "level %u labels distinct", level);
            if (level == 6) {
                CHECK(dial(q.options[i], q.options[j]) >= 3, "level 6 three minutes apart");
            }
            /* Never the middle of answer-5, answer, answer+5. */
            if (level == 6 && i == 0 && j == 1) {
                CHECK(!(has(q, onClock(q.answer + 5)) && has(q, onClock(q.answer + 715))),
                      "level 6 answer between two fives");
            }
        }
        if (q.label == Label::Minutes) CHECK(q.options[i] > 0 && q.options[i] < 60, "minutes");
        if (q.options[i] == q.answer) ++right;
    }
    CHECK(right == 1 && q.options[q.correct] == q.answer, "level %u one right", level);

    /* The level's own mistake is there. */
    switch (level) {
        case 1:
            if (swapped(q.shown) != q.shown) CHECK(has(q, swapped(q.shown)), "level 1 hands swapped");
            break;
        case 2:
            if (m == 30) CHECK(has(q, at(h + 1, 30)), "level 2 next hour");
            break;
        case 3:
            if (m == 45) CHECK(has(q, at(h + 1, 45)), "level 3 next hour");
            break;
        case 4: CHECK(has(q, at(h, m / 5)), "level 4 numeral as minutes"); break;
        case 5: CHECK(has(q, at(h + 1, m)), "level 5 next hour"); break;
        case 7:
            if (m > 30) CHECK(has(q, at(h + 1, 60 - m)), "level 7 to as past");
            if (m > 0 && m <= 30) CHECK(has(q, at(h - 1, 60 - m)), "level 7 past as to");
            break;
        case 8:
            CHECK(has(q, q.afterMidnight ? 12 * 60 + m : h * 60 + m), "level 8 twelve-hour reading");
            break;
        case 9: CHECK(has(q, onClock(q.shown - q.change)), "level 9 wrong way"); break;
        case 10:
            if (m != 30) CHECK(has(q, m), "level 10 minutes past");
            break;
    }

    ++t.slot[q.correct];
    /* Rank as the player sees them: earliest to latest. */
    int below = 0;
    for (int i = 0; i < 4; ++i) {
        if (q.options[i] < q.answer) ++below;
    }
    ++t.rank[below];
    ++t.total;
}

static void checkTally(uint8_t level, const Tally& t) {
    for (int i = 0; i < 4; ++i) {
        CHECK(t.slot[i] * 100 > t.total * 18 && t.slot[i] * 100 < t.total * 32,
              "level %u slot %d holds the answer %d of %d", level, i, t.slot[i], t.total);
    }
    /* As Fingers and Counting hold it: the answer is not the earliest, nor
     * the latest, half the time. */
    CHECK(t.rank[0] * 100 < t.total * 50, "level %u answer earliest %d of %d", level, t.rank[0], t.total);
    CHECK(t.rank[3] * 100 < t.total * 50, "level %u answer latest %d of %d", level, t.rank[3], t.total);
    std::printf("level %2u %-20s pool %3u  ranks %d/%d/%d/%d\n", level, levelName(level),
                poolSize(level), t.rank[0], t.rank[1], t.rank[2], t.rank[3]);
}

int main() {
    CHECK(clampLevel(0) == 1 && clampLevel(99) == LEVEL_COUNT, "clamp");
    CHECK(swapped(at(4, 0)) == at(12, 20), "4:00 swapped is 12:20");
    CHECK(swapped(at(11, 37)) == at(7, 58), "11:37 swapped is 7:58");
    char buf[32];
    format(Label::Words, at(3, 45), buf, sizeof(buf));
    CHECK(std::strcmp(buf, "quarter to 4") == 0, "words: %s", buf);
    format(Label::Words, at(12, 40), buf, sizeof(buf));
    CHECK(std::strcmp(buf, "20 to 1") == 0, "words: %s", buf);
    format(Label::Words, at(12, 15), buf, sizeof(buf));
    CHECK(std::strcmp(buf, "quarter past 12") == 0, "words: %s", buf);
    format(Label::Day, 15 * 60 + 40, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "15:40") == 0, "day: %s", buf);
    format(Label::Clock, at(0, 5), buf, sizeof(buf));
    CHECK(std::strcmp(buf, "12:05") == 0, "clock: %s", buf);

    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        const int n = levelLine(level, line, sizeof(line));
        CHECK(n > 0 && n <= LEVEL_LINE_MAX, "level %u line fits", level);
        Tally t;
        Rng rng(level * 104729U);
        if (numbered(level)) {
            const uint16_t pool = poolSize(level);
            CHECK(pool <= Course::QuestionDeck::MAX_ITEMS, "level %u pool fits the deck", level);
            std::set<uint32_t> seen;
            for (uint16_t item = 0; item < pool; ++item) {
                Question first;
                CHECK(makeNumbered(level, item, rng.next(), first), "level %u item %u", level, item);
                seen.insert(first.shown | (first.afterMidnight ? 0x10000U : 0U));
                for (int k = 0; k < 48; ++k) {
                    Question q;
                    makeNumbered(level, item, rng.next(), q);
                    CHECK(q.shown == first.shown && q.answer == first.answer, "level %u stable", level);
                    checkQuestion(level, q, t);
                }
            }
            CHECK(static_cast<int>(seen.size()) == pool, "level %u: %zu different of %u", level,
                  seen.size(), pool);
            Question none;
            CHECK(!makeNumbered(level, pool, 1, none), "level %u item past the pool", level);
        } else {
            std::set<uint32_t> seen;
            for (int k = 0; k < 20000; ++k) {
                Question q;
                makeDrawn(level, rng.next(), q);
                CHECK(q.level == level, "level %u drawn", level);
                seen.insert(token(q));
                checkQuestion(level, q, t);
            }
            const size_t whole = level == 6 ? 576 : 144 * CHANGE_COUNT;
            CHECK(seen.size() * 100 > whole * 95, "level %u reaches %zu of %zu", level,
                  seen.size(), whole);
        }
        checkTally(level, t);
    }

    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 200; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
