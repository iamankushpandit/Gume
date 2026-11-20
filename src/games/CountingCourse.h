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

/* WHAT COUNTING TEACHES, LEVEL BY LEVEL.
 *
 * The game used to ask one thing: 1..10 circles in rows of seven, the same
 * picture for the same number every time. Ten questions in all, so a player
 * who could count to ten had nothing more to learn here, and a player who
 * could not met eight circles as a shape they had seen before rather than as
 * eight things to count. Rows of seven also hid the fives and tens that every
 * later sum leans on.
 *
 * So the number itself, in the order it is learned: to five, to ten in rows
 * of five, the same counts scattered (and zero, an empty panel), to twenty in
 * rows of ten, one more and one less, tens and ones to 99 with rods of ten,
 * and counting in twos, fives and tens -- which is where Multiply takes over.
 * Adding and taking away belong to Shape Arith and Math, so they are not here.
 *
 * Every level is a small numbered pool, so engine/Course.h's QuestionDeck
 * deals each one before any comes back; none is large enough to need
 * RecentQuestions. Every item is a different question -- a different count,
 * or the same total drawn a different way -- so a deck that never repeats an
 * item never repeats a question.
 *
 * Pure C++, no Arduino and no heap: test/host/counting_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace CountingCourse {

/* How the objects are laid out. Each is a way of seeing a number, not
 * decoration: a ten-frame's rows of five, a twenty-frame's rows of ten split
 * five and five, rods that ARE ten. The game draws them; the capacities below
 * are what its panel can hold, and the game static_asserts its geometry
 * against them. */
enum class Picture : uint8_t {
    Row,        // one row, left to right
    Fives,      // rows of five, filled in order
    Scatter,    // chosen cells of a staggered grid, different each time
    Tens,       // rows of ten, split five and five
    Rods,       // rods of ten dots and loose ones
    Groups,     // equal groups (pairs, or fives), each in its own box
};

enum class Ask : uint8_t { HowMany, OneMore, OneLess, HowManyDots, InAll };

constexpr uint8_t LEVEL_COUNT = 7;

/* What the panel can draw. */
constexpr uint8_t ROW_MAX = 5;
constexpr uint8_t FIVES_PER_ROW = 5;
constexpr uint8_t FIVES_ROWS = 2;
constexpr uint8_t SCATTER_COLS = 7;
constexpr uint8_t SCATTER_ROWS = 3;
constexpr uint8_t SCATTER_CELLS = SCATTER_COLS * SCATTER_ROWS;
constexpr uint8_t TENS_PER_ROW = 10;
constexpr uint8_t TENS_ROWS = 2;
constexpr uint8_t RODS_MAX = 9;
constexpr uint8_t ONES_MAX = 9;
constexpr uint8_t GROUPS_MAX = 10;
/* The answer buttons show two digits at most. */
constexpr uint8_t OPTION_MAX = 99;

static_assert(SCATTER_CELLS <= 32, "the scattered places are bits of one uint32_t");

struct Level {
    const char* name;
    uint8_t pool;
};

/* Pools: 5 + 10 + 11 + 15 + 38 + 90 + 29. Each is the number of items
 * make() numbers below; the test checks every one of them. */
constexpr Level LEVELS[LEVEL_COUNT] = {
    {"To five", 5},                 // 1..5 in a row
    {"To ten", 10},                 // 1..10 in rows of five
    {"Any pattern", 11},            // 0..10 scattered
    {"To twenty", 15},              // 6..20 in rows of ten
    {"One more, one less", 38},     // 1..19 shown, more or less
    {"Tens and ones", 90},          // 10..99 as rods and ones
    {"Count in steps", 29},         // 1..10 pairs, 1..10 fives, 1..9 tens
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

/* "Level 7: " is nine characters; one digit is all the ladder needs. */
static_assert(LEVEL_COUNT <= 9, "the level line has room for one digit");
constexpr uint8_t LEVEL_LINE_MAX = 9 + longestName();

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}

inline uint16_t poolSize(uint8_t level) {
    return LEVELS[clampLevel(level) - 1].pool;
}

inline const char* levelName(uint8_t level) {
    return LEVELS[clampLevel(level) - 1].name;
}

/* "Level 4: To twenty", for the prompt line of the first question of a level
 * just reached. Returns what snprintf returns. */
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* The prompt line. Every wording names what is counted: with rods of ten on
 * the panel, "How many objects?" could fairly be answered by counting the
 * rods, so the levels that draw rods or groups ask about the dots. */
inline const char* askText(Ask ask) {
    switch (ask) {
        case Ask::OneMore: return "What is one more?";
        case Ask::OneLess: return "What is one less?";
        case Ask::HowManyDots: return "How many dots?";
        case Ask::InAll: return "How many dots in all?";
        case Ask::HowMany: break;
    }
    return "How many objects?";
}

struct Question {
    Picture picture = Picture::Row;
    Ask ask = Ask::HowMany;
    /* Objects on the panel, all told. */
    uint8_t shown = 0;
    /* Groups: 2 or 5 dots a box. Rods: 10. Otherwise 1. */
    uint8_t groupSize = 1;
    /* Scatter: bit i set means cell i of the grid holds an object, chosen
     * once per question so a repaint shows the same picture. */
    uint32_t places = 0;
    uint8_t answer = 0;
    uint8_t options[4] = {};
    uint8_t correct = 0;
};

namespace detail {

/* One 32-bit number in, as many as a question needs out. */
inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* The four buttons as they are being filled: the answer first, then the
 * likeliest mistakes, then neighbours. Anything out of range or already
 * there is refused, so the four are always four different numbers. */
struct Options {
    uint8_t v[4] = {};
    uint8_t n = 0;
    int lo = 0;
    int hi = OPTION_MAX;

    bool add(int value) {
        if (n >= 4 || value < lo || value > hi) return false;
        for (uint8_t i = 0; i < n; ++i) {
            if (v[i] == value) return false;
        }
        v[n++] = static_cast<uint8_t>(value);
        return true;
    }

    /* Four numbers in a run, starting at `low` and moved to fit the range.
     * Miscounting is one or two out, in either direction, and where the
     * answer sits in the run is chosen at random -- the answer must not be
     * findable as "always the second one up". */
    void addRun(int low) {
        if (low < lo) low = lo;
        if (low + 3 > hi) low = hi - 3;
        for (int k = 0; k < 4; ++k) add(low + k);
    }

    /* Whatever is still missing: the nearest numbers, alternating sides,
     * the first side chosen by `upFirst`. lo..hi always spans at least four,
     * so this always finishes. */
    void fill(int answer, bool upFirst) {
        for (int d = 1; n < 4 && d <= OPTION_MAX + 1; ++d) {
            add(upFirst ? answer + d : answer - d);
            add(upFirst ? answer - d : answer + d);
        }
    }
};

/* Which `count` cells of the grid are filled, all equally likely. */
inline uint32_t scatter(uint8_t count, uint32_t& s) {
    uint8_t cells[SCATTER_CELLS];
    for (uint8_t i = 0; i < SCATTER_CELLS; ++i) cells[i] = i;
    uint32_t places = 0;
    for (uint8_t i = 0; i < count && i < SCATTER_CELLS; ++i) {
        const uint8_t j = static_cast<uint8_t>(i + stir(s) % (SCATTER_CELLS - i));
        const uint8_t t = cells[i];
        cells[i] = cells[j];
        cells[j] = t;
        places |= 1UL << cells[i];
    }
    return places;
}

}   // namespace detail

/* The question for `item` of `level` (item below poolSize(level)), with its
 * options in a random order. `rnd` is any 32-bit random number; a level's
 * question depends only on the item, and only the options' order and a
 * scattered arrangement on `rnd`. False for an item outside the pool. */
inline bool make(uint8_t level, uint16_t item, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    if (item >= poolSize(level)) return false;

    uint32_t s = rnd ^ 0x9E3779B9UL;
    if (s == 0) s = 1;
    detail::stir(s);

    q = Question{};
    detail::Options o;
    int answer = 0;

    switch (level) {
        case 1: {
            /* A handful in a row: counting one by one, and seeing it. */
            q.picture = Picture::Row;
            q.shown = static_cast<uint8_t>(item + 1);
            answer = q.shown;
            o.lo = 1;
            o.hi = ROW_MAX + 2;
            o.add(answer);
            o.addRun(answer - static_cast<int>(detail::stir(s) % 4));
            break;
        }
        case 2: {
            /* Rows of five, as on a ten-frame: seven is five and two. */
            q.picture = Picture::Fives;
            q.shown = static_cast<uint8_t>(item + 1);
            answer = q.shown;
            o.lo = 1;
            o.hi = FIVES_PER_ROW * FIVES_ROWS + 2;
            o.add(answer);
            o.addRun(answer - static_cast<int>(detail::stir(s) % 4));
            break;
        }
        case 3: {
            /* The same counts with no pattern to recognise, and zero: an
             * empty panel is a count too. */
            q.picture = Picture::Scatter;
            q.shown = static_cast<uint8_t>(item);
            q.places = detail::scatter(q.shown, s);
            answer = q.shown;
            o.lo = 0;
            o.hi = 12;
            o.add(answer);
            o.addRun(answer - static_cast<int>(detail::stir(s) % 4));
            break;
        }
        case 4: {
            /* Rows of ten, split five and five: fourteen is ten and four.
             * Counting only the short row is the mistake this picture
             * invites, so past ten it is offered about half the time. */
            q.picture = Picture::Tens;
            q.shown = static_cast<uint8_t>(item + 6);
            answer = q.shown;
            o.lo = 1;
            o.hi = TENS_PER_ROW * TENS_ROWS + 2;
            o.add(answer);
            if (answer > TENS_PER_ROW && (detail::stir(s) & 1U) != 0) {
                o.add(answer - TENS_PER_ROW);
            }
            o.addRun(answer - static_cast<int>(detail::stir(s) % 4));
            break;
        }
        case 5: {
            /* One more and one less than what is shown, 0..20. The mistakes
             * are answering the count itself and going the wrong way, so the
             * count is always offered and the run around the two is placed
             * at random. */
            const uint8_t n = static_cast<uint8_t>(1 + item / 2);
            const bool more = (item % 2) == 0;
            q.picture = Picture::Tens;
            q.ask = more ? Ask::OneMore : Ask::OneLess;
            q.shown = n;
            answer = more ? n + 1 : n - 1;
            o.lo = 0;
            o.hi = TENS_PER_ROW * TENS_ROWS;
            o.add(answer);
            o.add(n);
            const int pairLow = answer < n ? answer : n;
            o.addRun(pairLow - static_cast<int>(detail::stir(s) % 3));
            break;
        }
        case 6: {
            /* Tens and ones. The digits written the wrong way round (34 for
             * 43, or 4 for 40 -- the rods read as ones), one ten out, and one
             * out. */
            const uint8_t n = static_cast<uint8_t>(item + 10);
            const uint8_t tens = n / 10;
            const uint8_t ones = n % 10;
            q.picture = Picture::Rods;
            q.ask = Ask::HowManyDots;
            q.groupSize = 10;
            q.shown = n;
            answer = n;
            o.lo = 1;
            o.hi = OPTION_MAX;
            o.add(answer);
            o.add(ones * 10 + tens);
            const bool tenUp = (detail::stir(s) & 1U) != 0;
            if (!o.add(tenUp ? n + 10 : n - 10)) o.add(tenUp ? n - 10 : n + 10);
            const bool oneUp = (detail::stir(s) & 1U) != 0;
            if (!o.add(oneUp ? n + 1 : n - 1)) o.add(oneUp ? n - 1 : n + 1);
            break;
        }
        default: {
            /* Counting in twos, fives and tens: equal groups, how many in
             * all. Counting the boxes instead of the dots is the first
             * mistake; the multiples either side are the next. */
            uint8_t k;
            uint8_t step;
            if (item < 10) {
                k = static_cast<uint8_t>(item + 1);
                step = 2;
            } else if (item < 20) {
                k = static_cast<uint8_t>(item - 9);
                step = 5;
            } else {
                k = static_cast<uint8_t>(item - 19);
                step = 10;
            }
            q.picture = step == 10 ? Picture::Rods : Picture::Groups;
            q.ask = Ask::InAll;
            q.groupSize = step;
            q.shown = static_cast<uint8_t>(k * step);
            answer = q.shown;
            o.lo = 1;
            o.hi = OPTION_MAX;
            o.add(answer);
            o.add(k);
            /* Two multiples next to the answer: both below it, one either
             * side, or both above. Only the arrangements that fit are drawn
             * from -- at two pairs there is nothing below, at nine tens
             * nothing above -- because falling back to "one either side"
             * whenever the first choice did not fit made the answer the
             * middle multiple most of the time. */
            constexpr int8_t SIDES[3][2] = {{-2, -1}, {-1, 1}, {1, 2}};
            uint8_t fit[3];
            uint8_t fits = 0;
            for (uint8_t w = 0; w < 3; ++w) {
                const int a = (k + SIDES[w][0]) * step;
                const int b = (k + SIDES[w][1]) * step;
                if (a >= o.lo && b <= o.hi && a != k && b != k) fit[fits++] = w;
            }
            if (fits > 0) {
                const uint8_t w = fit[detail::stir(s) % fits];
                o.add((k + SIDES[w][0]) * step);
                o.add((k + SIDES[w][1]) * step);
            }
            for (int d = 1; o.n < 4 && d <= 3; ++d) {
                o.add((k + d) * step);
                o.add((k - d) * step);
            }
            break;
        }
    }
    o.fill(answer, (detail::stir(s) & 1U) != 0);

    /* Shuffle all four, so a slot says nothing about which is right. */
    for (uint8_t i = 3; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % (i + 1));
        const uint8_t t = o.v[i];
        o.v[i] = o.v[j];
        o.v[j] = t;
    }
    q.answer = static_cast<uint8_t>(answer);
    for (uint8_t i = 0; i < 4; ++i) {
        q.options[i] = o.v[i];
        if (o.v[i] == q.answer) q.correct = i;
    }
    return true;
}

}   // namespace CountingCourse
