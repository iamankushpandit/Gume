// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of games/MultiplicationCourse.h: the twelve levels of
// Multiply, the questions each one numbers for the deck, and the four options
// each question offers. The header is pure C++ with no Arduino in it, and this
// file is what proves that still holds, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/multiplication_course_test.cpp -o multiplication_course_test && ./multiplication_course_test
//
// What it checks. Every level's pool is the size it should be and every item
// in it is a different, true question; together the items are every fact the
// level says it asks. Every number fits where the screen draws it, measured
// with the fonts' own glyph widths. For many random streams, the options are
// four different numbers with exactly one right, the wrong ones are the
// mistakes the header names rather than noise, and the answer is not always
// in the middle of them. And the whole ladder can be climbed and fallen down.
#include "MultiplicationCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <tuple>

namespace MC = MultiplicationCourse;
using MC::Kind;
using MC::Op;
using MC::Question;

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

/* TFT_eSPI's glyph widths for ASCII 32..127: Font16.c (font 2) and
 * Font32rle.c (font 4). textWidth() is the sum of these. */
static const unsigned char FONT2_W[96] = {
    6, 3, 4, 9, 8, 9, 9, 3,  7, 7, 8, 6, 3, 6, 5, 7,  8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 3, 3, 6, 6, 6, 8,  9, 8, 8, 8, 8, 8, 8, 8,  8, 4, 8, 8, 7, 10, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 10, 8, 8, 8, 4, 7, 4, 7, 9,  4, 7, 7, 7, 7, 7, 6, 7,
    7, 4, 5, 6, 4, 8, 7, 8,  7, 8, 6, 6, 5, 7, 8, 8,  6, 7, 7, 5, 3, 5, 8, 6,
};
static const unsigned char FONT4_W[96] = {
    5, 8, 8, 19, 14, 21, 17, 6,   8, 8, 12, 10, 7, 8, 7, 8,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 7, 7, 14, 9, 14, 13,
    25, 16, 17, 18, 18, 16, 15, 19, 18, 6, 13, 17, 13, 21, 18, 19,
    16, 19, 17, 16, 14, 18, 15, 23, 15, 16, 16, 9, 13, 9, 12, 13,
    9, 14, 15, 13, 15, 14, 8, 15,  15, 6, 6, 12, 6, 22, 15, 15,
    15, 15, 8, 12, 7, 14, 12, 18,  13, 13, 12, 13, 13, 13, 15, 1,
};

static int width(const char* s, const unsigned char* table) {
    int w = 0;
    for (; *s; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c < 32 || c > 127) return 10000;     // no glyph: never fits
        w += table[c - 32];
    }
    return w;
}

/* A question as a fact, whichever way round it is written: the key the
 * pool must never repeat. */
static std::tuple<int, int, int> factKey(const Question& q) {
    if (q.op == Op::Divide) return {2, q.shown[0], q.shown[1]};
    if (q.hidden != 2) {
        const int known = q.shown[q.hidden == 0 ? 1 : 0];
        return {1, known, q.answer};
    }
    const int a = q.shown[0] < q.shown[1] ? q.shown[0] : q.shown[1];
    const int b = q.shown[0] < q.shown[1] ? q.shown[1] : q.shown[0];
    return {0, a, b};
}

/* Every check one question must pass, wherever it came from. */
static bool checkQuestion(uint8_t level, uint16_t item, const Question& q, uint32_t seed) {
    const int before = failures;
    const unsigned a = q.shown[0], b = q.shown[1], c = q.shown[2];
    if (q.op == Op::Times) {
        CHECK(a * b == c, "L%u #%u: %u x %u is not %u", level, item, a, b, c);
    } else {
        CHECK(b != 0 && b * c == a, "L%u #%u: %u / %u is not %u", level, item, a, b, c);
        CHECK(q.hidden == 2, "L%u #%u: a division hides its quotient", level, item);
    }
    CHECK(q.hidden <= 2 && q.answer == q.shown[q.hidden], "L%u #%u: answer is not the hidden one",
          level, item);
    for (int i = 0; i < 3; ++i) {
        CHECK(q.shown[i] <= MC::MAX_SHOWN, "L%u #%u: %u will not fit", level, item, q.shown[i]);
    }

    /* Four different options, the answer exactly once, at `correct`. */
    CHECK(q.correct < 4 && q.options[q.correct] == q.answer, "L%u #%u: correct slot", level, item);
    int right = 0;
    int satisfies = 0;
    for (int i = 0; i < 4; ++i) {
        const unsigned o = q.options[i];
        CHECK(o <= MC::MAX_SHOWN, "L%u #%u: option %u too big", level, item, o);
        for (int j = i + 1; j < 4; ++j) {
            CHECK(q.options[j] != o, "L%u #%u seed %u: option %u twice", level, item, seed, o);
        }
        if (o == q.answer) ++right;
        /* Could this option be argued right? Put it in the "?" and see. */
        bool fits = false;
        if (q.op == Op::Divide) fits = o * b == a;
        else if (q.hidden == 2) fits = o == a * b;
        else fits = o * q.shown[q.hidden == 0 ? 1 : 0] == c;
        if (fits) ++satisfies;
        if (o != q.answer && q.hidden != 2) {
            CHECK(o >= 1, "L%u #%u: a 0 offered for a missing number or a quotient", level, item);
        }
    }
    CHECK(right == 1, "L%u #%u: the answer is offered %d times", level, item, right);
    CHECK(satisfies == 1, "L%u #%u seed %u: %d options make the question true", level, item, seed,
          satisfies);

    /* What the screen draws. */
    char left[MC::LEFT_SIZE];
    char rightSide[MC::RIGHT_SIZE];
    q.sides(left, sizeof(left), rightSide, sizeof(rightSide));
    CHECK(width(left, FONT4_W) <= MC::Screen::LEFT_MAX_W, "L%u #%u: '%s' is %dpx, room %d",
          level, item, left, width(left, FONT4_W), MC::Screen::LEFT_MAX_W);
    CHECK(width(rightSide, FONT4_W) <= MC::Screen::RIGHT_MAX_W, "L%u #%u: '%s' is %dpx, room %d",
          level, item, rightSide, width(rightSide, FONT4_W), MC::Screen::RIGHT_MAX_W);
    CHECK(std::strlen(left) < MC::LEFT_SIZE && std::strlen(rightSide) < MC::RIGHT_SIZE,
          "L%u #%u: a side was cut short", level, item);
    for (int i = 0; i < 4; ++i) {
        char label[12];
        std::snprintf(label, sizeof(label), "%u", static_cast<unsigned>(q.options[i]));
        CHECK(width(label, FONT4_W) <= MC::Screen::BUTTON_W - 16, "L%u: label %s too wide",
              level, label);
    }
    CHECK(width(q.prompt(), FONT2_W) <= MC::Screen::PROMPT_W, "prompt '%s' too wide", q.prompt());
    return failures == before;
}

/* The mistakes makeOptions() is allowed to offer for a question, so that a
 * number in no table -- the old "answer plus or minus up to twelve" -- shows
 * up as a failure. The numbers either side of the answer are allowed only
 * where the header says the lists can run dry. */
static bool namedMistake(const Question& q, int o) {
    const int ans = q.answer;
    if (q.op == Op::Times && q.hidden == 2) {
        const int a = q.shown[0], b = q.shown[1];
        const int named[] = {a * (b + 1), a * (b - 1), (a + 1) * b, (a - 1) * b, a + b,
                             a * (b + 2), a * (b - 2), (a + 2) * b, (a - 2) * b};
        for (int v : named) {
            if (v == o) return true;
        }
        return a == 0 || b == 0;
    }
    const int other = q.op == Op::Divide ? q.shown[1] : q.shown[q.hidden == 0 ? 1 : 0];
    const int d = o > ans ? o - ans : ans - o;
    return d <= 3 || o == other;
}

static void testPools() {
    /* The sizes are worked out by hand from the ladder, so a change to the
     * ladder has to change them here too. */
    const uint16_t want[MC::LEVEL_COUNT] = {27, 19, 70, 21, 21, 21, 66, 121, 36, 44, 44, 187};
    for (uint8_t level = 1; level <= MC::LEVEL_COUNT; ++level) {
        const uint16_t pool = MC::poolSize(level);
        CHECK(pool == want[level - 1], "L%u pool is %u, expected %u", level, pool, want[level - 1]);
        CHECK(pool <= Course::QuestionDeck::MAX_ITEMS, "L%u pool %u is over the deck", level, pool);

        /* Every item a different true question. */
        std::set<std::tuple<int, int, int>> seen;
        for (uint16_t item = 0; item < pool; ++item) {
            const Question q = MC::makeQuestion(level, item, 0x1234567U + item);
            checkQuestion(level, item, q, 0);
            const auto key = factKey(q);
            CHECK(seen.insert(key).second, "L%u: item %u repeats another item", level, item);
        }

        /* And together, every fact the level says it asks: build that set
         * from the spec by brute force and compare. */
        const MC::LevelSpec& s = MC::spec(level);
        std::set<std::tuple<int, int, int>> expected;
        auto addProducts = [&]() {
            for (int t = 0; t <= 12; ++t) {
                if (!MC::inSet(s.set, static_cast<uint8_t>(t))) continue;
                for (int m = s.lo; m <= s.hi; ++m) {
                    expected.insert({0, t < m ? t : m, t < m ? m : t});
                }
            }
        };
        auto addDivisions = [&]() {
            for (int d = 0; d <= 12; ++d) {
                if (!MC::inSet(s.set, static_cast<uint8_t>(d))) continue;
                for (int q = s.lo; q <= s.hi; ++q) expected.insert({2, d * q, d});
            }
        };
        switch (s.kind) {
            case Kind::Tables: addProducts(); break;
            case Kind::Divide: addDivisions(); break;
            case Kind::Mixed: addProducts(); addDivisions(); break;
            case Kind::Missing:
                for (int k = 0; k <= 12; ++k) {
                    if (!MC::inSet(s.set, static_cast<uint8_t>(k))) continue;
                    for (int h = s.lo; h <= s.hi; ++h) expected.insert({1, k, h});
                }
                break;
        }
        CHECK(seen == expected, "L%u: dealt %zu facts, the level asks %zu", level, seen.size(),
              expected.size());
    }

    /* What each level is about, spelled out, so a wrong bit in a set shows. */
    bool zeroSeen = false, oneSeen = false;
    for (uint16_t item = 0; item < MC::poolSize(3); ++item) {
        const Question q = MC::makeQuestion(3, item, 7);
        zeroSeen |= q.shown[0] == 0 || q.shown[1] == 0;
        oneSeen |= q.shown[0] == 1 || q.shown[1] == 1;
    }
    CHECK(zeroSeen && oneSeen, "level 3 is where 0 and 1 come in");
    for (uint8_t level : {1, 2, 4, 5, 6, 7, 12}) {
        for (uint16_t item = 0; item < MC::poolSize(level); ++item) {
            const Question q = MC::makeQuestion(level, item, 7);
            if (q.op != Op::Times) continue;
            CHECK(q.shown[0] != 0 && q.shown[1] != 0, "L%u asks x0", level);
            if (level >= 4) CHECK(q.shown[0] != 1 && q.shown[1] != 1, "L%u asks x1", level);
        }
    }
}

static void testOptionsManyStreams() {
    for (uint8_t level = 1; level <= MC::LEVEL_COUNT; ++level) {
        const uint16_t pool = MC::poolSize(level);
        Rng rng(1000 + level);
        int rank[4] = {0, 0, 0, 0};
        int ranked = 0;
        int noise = 0;
        int flipped[2] = {0, 0};
        /* Every item, many streams; then a few thousand draws at random. */
        const int streams = 200;
        for (uint16_t item = 0; item < pool; ++item) {
            for (int k = 0; k < streams; ++k) {
                const uint32_t seed = rng.next();
                const Question q = MC::makeQuestion(level, item, seed);
                if (!checkQuestion(level, item, q, seed)) return;
                int below = 0;
                for (int i = 0; i < 4; ++i) {
                    if (q.options[i] < q.answer) ++below;
                    if (q.options[i] != q.answer && !namedMistake(q, q.options[i])) ++noise;
                }
                ++rank[below];
                ++ranked;
                if (q.op == Op::Times && q.hidden == 2) ++flipped[q.shown[0] > q.shown[1] ? 1 : 0];
                if (q.hidden != 2 && q.op == Op::Times) ++flipped[q.hidden];
            }
        }
        for (int n = 0; n < 4000; ++n) {
            const uint16_t item = static_cast<uint16_t>(rng.next() % pool);
            const uint32_t seed = rng.next();
            if (!checkQuestion(level, item, MC::makeQuestion(level, item, seed), seed)) return;
        }
        CHECK(noise == 0, "L%u: %d wrong options were in no named mistake", level, noise);

        /* The answer must not always sit in the middle. On the levels with
         * no 0 in them, it is the smallest or the largest of the four about
         * half the time, as it would be if the four were in random order. */
        const double extremes = static_cast<double>(rank[0] + rank[3]) / ranked;
        if (level != 3) {
            CHECK(extremes > 0.30 && extremes < 0.70,
                  "L%u: the answer is the smallest or largest %.0f%% of the time", level,
                  extremes * 100);
        }
        for (int r = 0; r < 4; ++r) {
            CHECK(rank[r] > 0, "L%u: the answer never has %d options below it", level, r);
        }
        /* Both ways round, both sides of the sign. */
        if (MC::spec(level).kind != Kind::Divide) {
            CHECK(flipped[0] > 0 && flipped[1] > 0, "L%u: always written one way round", level);
        }
    }
}

static void testScreenText() {
    char banner[MC::BANNER_SIZE];
    for (uint8_t level = 1; level <= MC::LEVEL_COUNT; ++level) {
        MC::levelBanner(banner, sizeof(banner), level);
        char whole[64];
        std::snprintf(whole, sizeof(whole), "Level %u: %s", level, MC::spec(level).name);
        CHECK(std::strcmp(banner, whole) == 0, "L%u: the banner was cut to '%s'", level, banner);
        CHECK(width(banner, FONT2_W) <= MC::Screen::PROMPT_W - 8, "'%s' is %dpx, the strip %d",
              banner, width(banner, FONT2_W), MC::Screen::PROMPT_W);
        /* The header's own line, at font 2 in its 150px rect. */
        char head[16];
        std::snprintf(head, sizeof(head), "Level %u", level);
        CHECK(width(head, FONT2_W) <= 150, "'%s' does not fit the header", head);
    }
    /* The widest a side can be, written out. */
    CHECK(width("144", FONT4_W) <= MC::Screen::LEFT_MAX_W, "three figures on the left");
    CHECK(width("12 = 144", FONT4_W) <= MC::Screen::RIGHT_MAX_W, "the widest right side");
    /* And the operator's gap is what it says: half an "x" and a space. */
    CHECK(MC::Screen::OP_GAP == (FONT4_W['x' - 32] + 1) / 2 + FONT4_W[' ' - 32], "OP_GAP");
}

static void testLadder() {
    /* A player who gets everything right reaches the top and stays there. */
    Course::Ladder ladder;
    Course::QuestionDeck deck;
    ladder.start(MC::LEVEL_COUNT, 0);
    deck.reset(MC::poolSize(ladder.level()));
    Rng rng(31);
    int asked = 0;
    while (!ladder.atTop() && asked < 1000) {
        const uint16_t item = deck.next(rng.next());
        const Question q = MC::makeQuestion(ladder.level(), item, rng.next());
        checkQuestion(ladder.level(), item, q, 0);
        if (ladder.answer(true) != Course::Ladder::Step::Stay) {
            deck.reset(MC::poolSize(ladder.level()));
        }
        ++asked;
    }
    CHECK(ladder.atTop() && asked == (MC::LEVEL_COUNT - 1) * Course::Ladder::UP_AT,
          "all right reached level %u in %d", ladder.level(), asked);

    /* And one who gets half wrong comes back down to the first level. */
    for (int n = 0; n < 2000; ++n) {
        const uint16_t item = deck.next(rng.next());
        const bool right = (rng.next() & 1U) != 0;
        if (!right) deck.missed(item);
        if (ladder.answer(right) != Course::Ladder::Step::Stay) {
            deck.reset(MC::poolSize(ladder.level()));
        }
    }
    CHECK(ladder.level() <= 2, "half right should sink, at level %u", ladder.level());

    /* A stored level comes back, and one from a longer ladder is the top. */
    ladder.start(MC::LEVEL_COUNT, 7);
    CHECK(ladder.level() == 7, "stored 7 is 7");
    ladder.start(MC::LEVEL_COUNT, 200);
    CHECK(ladder.level() == MC::LEVEL_COUNT, "stored 200 is the top");

    /* Out-of-range levels and items do not crash or make nonsense. */
    checkQuestion(1, 0, MC::makeQuestion(0, 0, 5), 5);
    checkQuestion(12, 0, MC::makeQuestion(99, 9999, 5), 5);
    checkQuestion(1, 0, MC::makeQuestion(1, 0, 0), 0);
}

static void testExamples() {
    /* The ones the report quotes. */
    char l[MC::LEFT_SIZE], r[MC::RIGHT_SIZE];
    for (uint8_t level = 1; level <= MC::LEVEL_COUNT; ++level) {
        const Question q = MC::makeQuestion(level, MC::poolSize(level) / 2, 99);
        q.sides(l, sizeof(l), r, sizeof(r));
        std::printf("L%-2u %-22s pool %3u  %s %s %s   [%u %u %u %u]\n", level, MC::spec(level).name,
                    MC::poolSize(level), l, q.op == Op::Divide ? "/" : "x", r, q.options[0],
                    q.options[1], q.options[2], q.options[3]);
    }
}

int main() {
    testPools();
    testOptionsManyStreams();
    testScreenText();
    testLadder();
    testExamples();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
