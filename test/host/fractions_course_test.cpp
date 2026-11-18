// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/FractionsCourse.h: the levels of Fractions and
// the questions each one asks. The header is pure C++, and this file is what
// proves it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/fractions_course_test.cpp -o fractions_course_test && ./fractions_course_test
//
// For every numbered item and many drawn ones: the answer is right, worked out
// here again from the question; four options no two of which are equal in
// value, so exactly one is right -- 1/2 is never offered against 2/4 -- with
// the level's commonest slip among them, every label fitting its button and
// every small pie in eighths or coarser; Compare's two fractions differ; and
// the right answer in each slot about a quarter of the time.
#include "FractionsCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <tuple>

using namespace FractionsCourse;

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

static bool hasExactly(const Question& q, const Frac& f) {
    for (int i = 0; i < 4; ++i) {
        const Frac& o = q.options[i];
        if (o.whole == f.whole && o.n == f.n && o.d == f.d) return true;
    }
    return false;
}

static void checkOne(uint8_t level, const Question& q, int* slot, int& total) {
    char line[48];
    CHECK(std::strlen(prompt(q)) <= 40, "prompt fits");
    if (q.mode == Mode::Compare) {
        CHECK(!same(q.a, q.b), "level %u compare differs", level);
        CHECK(q.correct == (greater(q.a, q.b) ? 0 : 1), "level %u bigger", level);
        CHECK(q.hidePies == (level == 9), "pies hidden on 9");
        if (level == 4) CHECK(q.a.n == 1 && q.b.n == 1, "unit fractions");
        if (level == 5) CHECK(q.a.d == q.b.d, "same bottom");
        if (level == 6) CHECK(q.a.n == q.b.n && q.a.d != q.b.d, "same top");
        if (level >= 8) CHECK(q.a.d != q.b.d, "different bottoms");
        ++slot[q.correct];
        ++total;
        return;
    }
    switch (level) {
        case 1: case 2: case 3:
            CHECK(same(q.answer, q.a) && q.answer.n < q.answer.d, "pie answer");
            CHECK(hasExactly(q, frac(static_cast<uint8_t>(q.a.d - q.a.n), q.a.d)) ||
                      same(frac(static_cast<uint8_t>(q.a.d - q.a.n), q.a.d), q.answer),
                  "level %u the empty slices", level);
            break;
        case 7:
            CHECK(same(q.answer, q.a), "level 7 same amount");
            if (q.mode == Mode::NameThePie) CHECK(gcd(q.answer.n, q.answer.d) == 1, "simplest");
            else CHECK(q.answer.d > q.a.d, "cut smaller");
            break;
        case 10:
            CHECK(q.answer.d == 1 && q.answer.n * q.a.d == q.a.n * q.b.n, "of a number");
            CHECK(hasExactly(q, frac(static_cast<uint8_t>(q.b.n / q.a.d), 1)) ||
                      q.b.n / q.a.d == q.answer.n, "divided only");
            break;
        case 11: {
            const int want = q.op == 1 ? q.a.n + q.b.n : q.a.n - q.b.n;
            CHECK(q.answer.d == q.a.d && q.answer.n == want && want > 0 && want < q.a.d, "sum");
            if (q.op == 1) {
                CHECK(hasExactly(q, frac(static_cast<uint8_t>(want), static_cast<uint8_t>(2 * q.a.d))),
                      "bottoms added");
            }
            break;
        }
        case 12:
            CHECK(same(q.answer, q.a), "level 12 same amount");
            CHECK(top(q.a) > q.a.d && top(q.a) < 3 * q.a.d, "between one and three");
            break;
    }
    if (q.mode == Mode::Written) {
        CHECK(questionText(q, line, sizeof(line)) <= 14, "question fits: %s", line);
    }
    int right = 0;
    for (int i = 0; i < 4; ++i) {
        const Frac& o = q.options[i];
        char label[16];
        CHECK(text(o, label, sizeof(label)) <= 7, "label fits: %s", label);
        CHECK(o.d >= 1 && (o.n > 0 || o.whole > 0), "a value");
        if (q.mode == Mode::FindThePie) CHECK(o.d <= 8 && o.n < o.d && o.whole == 0, "small pie %s", label);
        for (int j = i + 1; j < 4; ++j) CHECK(!same(o, q.options[j]), "level %u equal options", level);
        if (same(o, q.answer)) ++right;
    }
    CHECK(right == 1 && same(q.options[q.correct], q.answer), "level %u one right", level);
    ++slot[q.correct];
    ++total;
}

int main() {
    CHECK(same(frac(2, 4), frac(1, 2)) && greater(frac(2, 3), frac(3, 5)), "values");
    char t[16];
    Frac m;
    m.whole = 1;
    m.n = 3;
    m.d = 4;
    text(m, t, sizeof(t));
    CHECK(std::strcmp(t, "1  3/4") == 0, "mixed: %s", t);
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "line fits: %s", line);
        int slot[4] = {};
        int total = 0;
        Rng rng(level * 7919U);
        if (numbered(level)) {
            CHECK(poolSize(level) <= Course::QuestionDeck::MAX_ITEMS, "pool fits");
            std::set<std::tuple<int, int, int, int, int, int>> seen;
            for (uint16_t item = 0; item < poolSize(level); ++item) {
                Question first;
                CHECK(make(level, item, rng.next(), first), "level %u item %u", level, item);
                seen.insert(std::make_tuple(static_cast<int>(first.mode), first.a.whole * 256 + first.a.n,
                                            first.a.d, first.b.n, first.b.d, first.op));
                for (int k = 0; k < 60; ++k) {
                    Question q;
                    make(level, item, rng.next(), q);
                    CHECK(same(q.answer, first.answer), "stable");
                    checkOne(level, q, slot, total);
                }
            }
            CHECK(static_cast<int>(seen.size()) == poolSize(level), "level %u: %zu different of %u",
                  level, seen.size(), poolSize(level));
            Question none;
            CHECK(!make(level, poolSize(level), 1, none), "past the pool");
        } else {
            for (int k = 0; k < 20000; ++k) {
                Question q;
                makeDrawn(level, rng.next(), q);
                checkOne(level, q, slot, total);
            }
        }
        const int ways = (level >= 4 && level <= 6) || level == 8 || level == 9 ? 2 : 4;
        for (int i = 0; i < ways; ++i) {
            CHECK(slot[i] * 100 > total * (ways == 2 ? 40 : 18) &&
                      slot[i] * 100 < total * (ways == 2 ? 60 : 32),
                  "level %u slot %d %d of %d", level, i, slot[i], total);
        }
        std::printf("level %2u %-24s pool %3u\n", level, levelName(level), poolSize(level));
    }
    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 300; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
