// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/MoneyCourse.h: the levels of Money and the
// rounds each one asks. The header is pure C++, and this file is what proves
// it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/money_course_test.cpp -o money_course_test && ./money_course_test
//
// For every numbered item and many drawn rounds: a pile adds up to what the
// round says, uses only the level's coins and fits its box; Make asks an
// amount the tray can build; More's two piles differ and the more valuable
// one often has fewer coins; Change is paid minus the prices; four different
// amounts, exactly one right, the commonest slip among them, every one shown
// in fives where the level counts in fives, and the right one in each slot
// about a quarter of the time. Fewest coins is checked against a search.
#include "MoneyCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>

using namespace MoneyCourse;

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

static bool has(const Round& r, int v) {
    for (int i = 0; i < 4; ++i) {
        if (r.options[i] == v) return true;
    }
    return false;
}

static void pileFits(uint8_t level, const uint8_t* coins, uint8_t n, uint8_t maxN) {
    const Level& l = levelOf(level);
    CHECK(n >= 1 && n <= maxN, "level %u pile of %u", level, n);
    for (uint8_t i = 0; i < n; ++i) {
        bool allowed = false;
        for (uint8_t k = 0; k < l.coinKinds; ++k) allowed = allowed || coins[i] == COINS[k];
        CHECK(allowed, "level %u coin %u in play", level, coins[i]);
    }
}

struct Tally {
    int slot[4] = {};
    int total = 0;
};

static void checkOptions(const Round& r, Tally& t) {
    int right = 0;
    for (int i = 0; i < 4; ++i) {
        CHECK(r.options[i] >= 1, "positive");
        char text[16];
        CHECK(cents(r.options[i], text, sizeof(text)) <= 6, "fits a button: %s", text);
        for (int j = i + 1; j < 4; ++j) CHECK(r.options[i] != r.options[j], "distinct");
        if (r.options[i] == r.target) ++right;
        if (r.level == 5 || r.level == 8) CHECK(r.options[i] % 5 == 0, "level %u in fives", r.level);
    }
    CHECK(right == 1 && r.options[r.correct] == r.target, "one right");
    ++t.slot[r.correct];
    ++t.total;
}

/* The least number of coins for `c`, by search. */
static int leastBySearch(int c) {
    int best[200];
    best[0] = 0;
    for (int v = 1; v <= c; ++v) {
        best[v] = 1000;
        for (int k = 0; k < 5; ++k) {
            if (COINS[k] <= v && best[v - COINS[k]] + 1 < best[v]) best[v] = best[v - COINS[k]] + 1;
        }
    }
    return best[c];
}

int main() {
    CHECK(clampLevel(0) == 1 && clampLevel(99) == LEVEL_COUNT, "clamp");
    for (int c = 1; c < 200; ++c) CHECK(fewestCoins(c) == leastBySearch(c), "fewest for %d", c);
    char buf[16];
    cents(125, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "$1.25") == 0, "dollars: %s", buf);
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        CHECK(levelLine(level, buf, sizeof(buf)) >= 0, "line");
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "line fits: %s", line);
        const Level& l = levelOf(level);
        Tally t;
        int trap = 0;
        int mores = 0;
        Rng rng(level * 7919U);
        if (numbered(level)) {
            CHECK(poolSize(level) <= Course::QuestionDeck::MAX_ITEMS, "level %u pool fits", level);
            for (uint16_t item = 0; item < poolSize(level); ++item) {
                for (int k = 0; k < 40; ++k) {
                    Round r;
                    CHECK(make(level, item, rng.next(), r), "level %u item %u", level, item);
                    switch (r.mode) {
                        case Mode::Total:
                            CHECK(value(r.a, r.aCount) == r.target, "total adds up");
                            CHECK(r.target >= l.lo && r.target <= l.hi, "total in range");
                            pileFits(level, r.a, r.aCount, 8);
                            CHECK(has(r, r.target - r.a[0]), "a coin missed");
                            checkOptions(r, t);
                            break;
                        case Mode::Make:
                            CHECK(r.target >= 1 && r.target <= l.hi, "make in range");
                            CHECK(fewestCoins(r.target) <= MAX_BUILT, "buildable in the box");
                            CHECK(r.fewest == (level == 7), "fewest on level 7");
                            break;
                        case Mode::More: {
                            const int a = value(r.a, r.aCount);
                            const int b = value(r.b, r.bCount);
                            CHECK(a != b, "piles differ");
                            CHECK(r.more == (a > b ? 0 : 1), "more is right");
                            pileFits(level, r.a, r.aCount, 9);
                            pileFits(level, r.b, r.bCount, 9);
                            const bool fewerWins = a > b ? r.aCount < r.bCount : r.bCount < r.aCount;
                            if (fewerWins) ++trap;
                            ++mores;
                            break;
                        }
                        case Mode::Change:
                            CHECK(r.target == r.paid - r.price - r.price2, "change");
                            CHECK(r.price % 5 == 0, "level 5 price in fives");
                            CHECK(has(r, r.price), "the price given back");
                            checkOptions(r, t);
                            break;
                    }
                }
            }
            Round none;
            CHECK(!make(level, poolSize(level), 1, none), "past the pool");
        } else {
            for (int k = 0; k < 20000; ++k) {
                Round r;
                makeDrawn(level, rng.next(), r);
                CHECK(r.mode == Mode::Change && r.price + r.price2 < r.paid, "drawn change");
                CHECK(r.target == r.paid - r.price - r.price2, "change adds up");
                if (level == 8) {
                    CHECK(r.price2 > 0 && has(r, r.paid - r.price), "a price left out");
                } else {
                    CHECK(has(r, r.price), "the price given back");
                }
                checkOptions(r, t);
            }
        }
        if (t.total > 0) {
            for (int i = 0; i < 4; ++i) {
                CHECK(t.slot[i] * 100 > t.total * 18 && t.slot[i] * 100 < t.total * 32,
                      "level %u slot %d %d of %d", level, i, t.slot[i], t.total);
            }
        }
        if (mores > 0) CHECK(trap * 100 > mores * 30, "level %u fewer coins worth more %d of %d", level, trap, mores);
        std::printf("level %u %-20s pool %3u  trap %d/%d\n", level, levelName(level),
                    numbered(level) ? poolSize(level) : 0, trap, mores);
    }

    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 200; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
