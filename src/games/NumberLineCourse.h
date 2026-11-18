// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* WHAT NUMBER LINE TEACHES, LEVEL BY LEVEL.
 *
 * The game asked thirty sums within ten, with no levels, and printed the
 * answer before asking for it: the frog hopped to the result and stood on it
 * with the number written inside. A player who could read a numeral could
 * not be wrong.
 *
 * This course keeps that as its first step -- watch the hops, read where the
 * frog landed -- and from then on asks first and hops after, so the frog
 * checks the answer instead of giving it: adding, then taking away, within
 * ten; how many hops between two marks; crossing ten in two hops, to ten and
 * on; hops of ten to a hundred; which ten a number is nearest; equal hops,
 * which are the times tables walked; and the line below zero.
 *
 * Every wrong option is a mistake the line itself invites: the starting tick
 * counted as the first hop, the hop count read off as the answer, the frog
 * sent the wrong way, a ten lost or gained crossing ten, one hop too many.
 *
 * A question is a start, a hop and a direction on a line from `lo` to `hi`.
 * Pure C++, no Arduino and no heap: test/host/number_line_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace NumberLineCourse {

constexpr uint8_t LEVEL_COUNT = 9;

enum class Ask : uint8_t {
    Land,       // a + b = ?, or a - b = ?: where does the frog land?
    Hops,       // a + ? = c: how many hops from one mark to the other?
    Nearest,    // which ten is this number nearest?
    Times,      // n hops of k from zero: where does it land?
};

struct Level {
    const char* name;
    uint8_t pool;
};

/* Pools: 15 + 40 + 40 + 80 + 72 + 110 + 72 + 30 + 80. The test checks each
 * one is exactly the number of different questions. */
constexpr Level LEVELS[LEVEL_COUNT] = {
    {"First hops", 15},         // watch, then read: within five
    {"Hop right", 40},          // add within ten, answer first
    {"Hop left", 40},           // take away within ten
    {"How many hops", 80},      // 3 + ? = 8, 9 - ? = 4
    {"Through ten", 72},        // 8 + 5 as 8 to 10, then 3 more
    {"Hops of ten", 110},       // 30 + 40 on a line to 100
    {"Nearest ten", 72},        // 47 is nearest to 50
    {"Equal hops", 30},         // 4 hops of 3
    {"Below zero", 80},         // 2 - 5 on a line from -5 to 5
};

constexpr uint8_t textLength(const char* s) {
    uint8_t n = 0;
    while (s[n] != '\0') ++n;
    return n;
}

constexpr uint8_t longestName() {
    uint8_t best = 0;
    for (uint8_t i = 0; i < LEVEL_COUNT; ++i) {
        const uint8_t n = textLength(LEVELS[i].name);
        if (n > best) best = n;
    }
    return best;
}

static_assert(LEVEL_COUNT <= 9, "the level line has room for one digit");
constexpr uint8_t LEVEL_LINE_MAX = 9 + longestName();
/* "100 - 50 = ?" and "47 is nearest to" are the longest questions. */
constexpr uint8_t QUESTION_MAX = 20;

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}

inline uint16_t poolSize(uint8_t level) { return LEVELS[clampLevel(level) - 1].pool; }
inline const char* levelName(uint8_t level) { return LEVELS[clampLevel(level) - 1].name; }

inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

struct Question {
    Ask ask = Ask::Land;
    /* The line: its two ends, how far apart its ticks are, and how often a
     * tick carries its number (1 = every tick). */
    int8_t lo = 0;
    int8_t hi = 10;
    uint8_t tick = 1;
    uint8_t labelEvery = 1;
    /* Where the frog starts, how far one hop goes and how many hops; `add`
     * says which way. Nearest has no hops: the frog stands on `start`. */
    int8_t start = 0;
    uint8_t hop = 1;
    uint8_t hops = 0;
    bool add = true;
    /* Through ten: the hops go in two arcs, to the ten and then the rest,
     * rather than one at a time. */
    bool viaTen = false;
    /* First hops: the frog hops before the question, and the player reads
     * where it landed. From level 2 the question comes first. */
    bool watchFirst = false;
    int16_t answer = 0;
    int16_t options[4] = {};
    uint8_t correct = 0;

    /* Where the frog ends up. */
    int16_t landing() const {
        const int16_t d = static_cast<int16_t>(hop * hops);
        return static_cast<int16_t>(add ? start + d : start - d);
    }

    /* The question line: "6 + 4 = ?", "3 + ? = 8", "4 hops of 3 = ?",
     * "47 is nearest to". */
    int text(char* out, size_t len) const {
        const int d = hop * hops;
        switch (ask) {
            case Ask::Hops:
                return snprintf(out, len, "%d %c ? = %d", start, add ? '+' : '-', landing());
            case Ask::Nearest:
                return snprintf(out, len, "%d is nearest to", start);
            case Ask::Times:
                return snprintf(out, len, "%u hops of %u = ?", hops, hop);
            case Ask::Land:
            default:
                return snprintf(out, len, "%d %c %d = ?", start, add ? '+' : '-', d);
        }
    }

    /* The same line with the answer in place of the question mark, for
     * after a right answer. */
    int solved(char* out, size_t len) const {
        const int d = hop * hops;
        switch (ask) {
            case Ask::Hops:
                return snprintf(out, len, "%d %c %d = %d", start, add ? '+' : '-', d, landing());
            case Ask::Nearest:
                return snprintf(out, len, "%d is nearest to %d", start, answer);
            case Ask::Times:
                return snprintf(out, len, "%u hops of %u = %d", hops, hop, answer);
            case Ask::Land:
            default:
                return snprintf(out, len, "%d %c %d = %d", start, add ? '+' : '-', d, answer);
        }
    }
};

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

struct Options {
    int16_t v[4] = {};
    uint8_t n = 0;
    int16_t lo = 0;
    int16_t hi = 10;

    bool add(int value) {
        if (n >= 4 || value < lo || value > hi) return false;
        for (uint8_t i = 0; i < n; ++i) {
            if (v[i] == value) return false;
        }
        v[n++] = static_cast<int16_t>(value);
        return true;
    }
};

/* Item `item` of the pairs (a, b) with a in [aLo, aHi], b in [1, bMax] and
 * a + b at most `cap`: the triangular table walked a row at a time. */
inline bool pairUnder(uint8_t item, int aLo, int aHi, int bMax, int cap, int& a, int& b) {
    for (a = aLo; a <= aHi; ++a) {
        int row = cap - a;
        if (row > bMax) row = bMax;
        if (row <= 0) continue;
        if (item < row) {
            b = 1 + item;
            return true;
        }
        item = static_cast<uint8_t>(item - row);
    }
    return false;
}

}   // namespace detail

/* The four options: the answer, the level's commonest mistake, then two of
 * its other mistakes chosen at random, then -- where those collide or fall
 * off the line -- the nearest numbers a step away. Placed at random. */
inline void makeOptions(uint8_t level, Question& q, uint32_t rnd) {
    uint32_t s = rnd ^ 0x6C8E9CF5UL;
    if (s == 0) s = 1;
    detail::stir(s);

    detail::Options o;
    int key = -9999;
    int more[5];
    uint8_t moreCount = 0;
    int step = 1;
    /* Which way the fallback walks: both sides, or one only. */
    int only = 0;
    const int a = q.start;
    const int d = q.hop * q.hops;
    const int sign = q.add ? 1 : -1;

    switch (q.ask) {
        case Ask::Land:
            o.lo = q.lo < 0 ? -10 : 0;
            o.hi = q.hi;
            if (level == 5) {
                /* Crossing ten: a ten lost or gained, and the start counted
                 * as the first hop. */
                key = q.answer - 10 * sign;
                more[moreCount++] = q.answer - sign;
                more[moreCount++] = q.answer + sign;
                more[moreCount++] = q.answer - 10 * sign - sign;
                o.hi = 30;
            } else if (level == 6) {
                /* Tens: one hop of ten too few or too many, the tens added
                 * as ones (30 + 40 = 34), the frog sent the wrong way. */
                key = q.answer - 10 * sign;
                more[moreCount++] = q.answer + 10 * sign;
                more[moreCount++] = a + sign * (d / 10);
                more[moreCount++] = a - sign * d;
                step = 10;
                o.hi = 150;
            } else if (level == 9) {
                /* Below zero: the sign lost, and zero counted as a hop. */
                key = -q.answer;
                more[moreCount++] = q.answer - sign;
                more[moreCount++] = q.answer + sign;
                more[moreCount++] = a - sign * d;
                o.lo = -10;
                o.hi = 10;
            } else {
                /* The starting tick counted as the first hop, the frog sent
                 * the wrong way, the hop count read off as the answer. */
                key = q.answer - sign;
                more[moreCount++] = a - sign * d;
                more[moreCount++] = d;
                more[moreCount++] = q.answer + sign;
            }
            break;
        case Ask::Hops:
            /* The landing mark read as the answer, the ticks counted with
             * the start included, the other mark. */
            o.lo = 1;
            o.hi = 10;
            key = q.answer + 1;
            more[moreCount++] = q.landing();
            more[moreCount++] = a;
            more[moreCount++] = q.answer - 1;
            break;
        case Ask::Nearest: {
            /* Rounded the wrong way, and the ones digit taken for the tens
             * (47 to 70). */
            const int lower = (a / 10) * 10;
            key = q.answer == lower ? lower + 10 : lower;
            more[moreCount++] = (a % 10) * 10;
            more[moreCount++] = lower - 10;
            more[moreCount++] = lower + 20;
            o.lo = 0;
            o.hi = 100;
            step = 10;
            break;
        }
        case Ask::Times:
            /* One hop too many or too few -- one side only, chosen at
             * random, or the answer would sit between the two every time --
             * two too many, the two numbers added, the hop count read off. */
            {
                const int side = (detail::stir(s) & 1U) != 0 ? q.hop : -q.hop;
                key = q.answer + side;
                more[moreCount++] = q.answer + 2 * side;
                /* Unless they happen to be one hop the other way: 2 hops of
                 * 2 read off as 2 would put the answer between again. */
                if (q.hops + q.hop != q.answer - side) more[moreCount++] = q.hops + q.hop;
                if (q.hops != q.answer - side) more[moreCount++] = q.hops;
                step = q.hop;
                only = side > 0 ? 1 : -1;
            }
            o.lo = 1;
            o.hi = 110;
            break;
    }

    o.add(q.answer);
    o.add(key);
    for (uint8_t i = moreCount; i > 1; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % i);
        const int t = more[i - 1];
        more[i - 1] = more[j];
        more[j] = t;
    }
    for (uint8_t i = 0; i < moreCount && o.n < 4; ++i) o.add(more[i]);
    const bool upFirst = (detail::stir(s) & 1U) != 0;
    for (int k = 2 * step; only != 0 && o.n < 4 && k < 400; k += step) {
        o.add(q.answer + only * k);
    }
    for (int k = step; o.n < 4 && k < 400; k += step) {
        if (only != 0 && k == step) continue;
        o.add(q.answer + (upFirst ? k : -k));
        o.add(q.answer + (upFirst ? -k : k));
    }

    for (uint8_t i = 3; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % (i + 1));
        const int16_t t = o.v[i];
        o.v[i] = o.v[j];
        o.v[j] = t;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        q.options[i] = o.v[i];
        if (o.v[i] == q.answer) q.correct = i;
    }
}

/* Question `item` of `level` (item below poolSize(level)), options included.
 * The question depends only on the item; the options' order on `rnd`. */
inline bool make(uint8_t level, uint16_t item, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    if (item >= poolSize(level)) return false;
    q = Question{};
    int a = 0;
    int b = 0;
    const uint8_t i8 = static_cast<uint8_t>(item);
    switch (level) {
        case 1:
            detail::pairUnder(i8, 0, 4, 5, 5, a, b);
            q.start = static_cast<int8_t>(a);
            q.hops = static_cast<uint8_t>(b);
            q.watchFirst = true;
            break;
        case 2:
            detail::pairUnder(i8, 0, 9, 5, 10, a, b);
            q.start = static_cast<int8_t>(a);
            q.hops = static_cast<uint8_t>(b);
            break;
        case 3:
            /* The same pairs, run backwards: from a + b, take b. */
            detail::pairUnder(i8, 0, 9, 5, 10, a, b);
            q.start = static_cast<int8_t>(a + b);
            q.hops = static_cast<uint8_t>(b);
            q.add = false;
            break;
        case 4:
            detail::pairUnder(static_cast<uint8_t>(item % 40), 0, 9, 5, 10, a, b);
            q.ask = Ask::Hops;
            q.add = item < 40;
            q.start = static_cast<int8_t>(q.add ? a : a + b);
            q.hops = static_cast<uint8_t>(b);
            break;
        case 5: {
            /* a + b with both single digits and the sum past ten, and the
             * same facts taken away: 13 - 5. */
            uint8_t k = static_cast<uint8_t>(item % 36);
            for (a = 2; a <= 9; ++a) {
                if (k < a - 1) break;
                k = static_cast<uint8_t>(k - (a - 1));
            }
            b = 10 - a + 1 + k;     // a + b runs 11 .. a + 9
            q.lo = 0;
            q.hi = 20;
            q.labelEvery = 2;
            q.viaTen = true;
            q.add = item < 36;
            q.start = static_cast<int8_t>(q.add ? a : a + b);
            q.hops = static_cast<uint8_t>(b);
            break;
        }
        case 6:
            /* Tens: a + b at most 100 with b at least ten, both ways. */
            detail::pairUnder(static_cast<uint8_t>(item % 55), 0, 9, 10, 10, a, b);
            q.lo = 0;
            q.hi = 100;
            q.tick = 10;
            q.hop = 10;
            q.add = item < 55;
            q.start = static_cast<int8_t>(10 * (q.add ? a : a + b));
            q.hops = static_cast<uint8_t>(b);
            break;
        case 7: {
            /* Nine decades, eight numbers in each: not the tens, not the
             * fives, so no rule about halfway is needed. */
            const int decade = item / 8;
            int ones = 1 + item % 8;
            if (ones >= 5) ++ones;
            q.ask = Ask::Nearest;
            q.lo = static_cast<int8_t>(10 * decade);
            q.hi = static_cast<int8_t>(10 * decade + 10);
            q.start = static_cast<int8_t>(10 * decade + ones);
            q.answer = static_cast<int16_t>(10 * decade + (ones > 5 ? 10 : 0));
            break;
        }
        case 8: {
            /* Equal hops from zero: twos, threes, fours and fives on a line to
             * twenty, tens on a line to a hundred. */
            static constexpr uint8_t STEP[] = {2, 3, 4, 5, 10};
            static constexpr uint8_t FIRST[] = {0, 9, 14, 18, 21};
            uint8_t g = 4;
            while (g > 0 && item < FIRST[g]) --g;
            q.ask = Ask::Times;
            q.hop = STEP[g];
            q.hops = static_cast<uint8_t>(2 + item - FIRST[g]);
            q.lo = 0;
            q.hi = static_cast<int8_t>(q.hop == 10 ? 100 : 20);
            q.tick = q.hop == 10 ? 10 : 1;
            q.labelEvery = q.hop == 10 ? 1 : 2;
            break;
        }
        case 9:
        default:
            /* From -5 to 5: start, hop count 1..5, landing on the line. */
            detail::pairUnder(static_cast<uint8_t>(item % 40), 0, 9, 5, 10, a, b);
            q.lo = -5;
            q.hi = 5;
            q.add = item < 40;
            q.start = static_cast<int8_t>((q.add ? a : a + b) - 5);
            q.hops = static_cast<uint8_t>(b);
            break;
    }
    if (q.ask != Ask::Nearest) {
        q.answer = q.ask == Ask::Hops ? static_cast<int16_t>(q.hop * q.hops) : q.landing();
    }
    makeOptions(level, q, rnd);
    return true;
}

}   // namespace NumberLineCourse
