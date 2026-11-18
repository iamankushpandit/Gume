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

/* WHAT SHAPE ARITH TEACHES, LEVEL BY LEVEL.
 *
 * The game used to ask one thing: a sum of two groups of one to four shapes,
 * or a difference with a start of at most eight. Thirty-two questions, drawn
 * independently, so the same few came round again within minutes and nothing
 * ever asked for a sum past eight, a zero, or a ten.
 *
 * This game owns the stage where the answer can still be COUNTED: adding as
 * joining two groups and taking away as removing one, with the shapes on the
 * panel. In the order it is learned: adding within five, taking away within
 * five, the same within ten (in rows of five, as on a ten-frame), zero --
 * adding nothing, taking nothing, taking everything -- how many more fill a
 * frame, and crossing ten both ways. Where the picture goes away, Math takes
 * over; the same facts with numerals only are its levels.
 *
 * Every level is a small numbered pool (none over 40), so engine/Course.h's
 * QuestionDeck deals each one before any comes back, and every item is a
 * different question. The shape is decoration and is chosen by the game; it
 * is not part of the item.
 *
 * Pure C++, no Arduino and no heap: test/host/shape_arith_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace ShapeArithCourse {

enum class Op : uint8_t {
    Add,        // a shapes, then b more arrive: how many in all?
    TakeAway,   // a shapes, b of them go: how many are left?
    MakeUp,     // a shapes in a frame of `whole` places: how many more fill it?
};

constexpr uint8_t LEVEL_COUNT = 8;

/* What one panel can draw: rows of five, two rows up to ten and four rows up
 * to twenty. The game static_asserts its grid against these. */
constexpr uint8_t PER_ROW = 5;
constexpr uint8_t SMALL_GRID = 10;
constexpr uint8_t PANEL_MAX = 20;
/* The answer buttons show two digits at most. */
constexpr uint8_t OPTION_MAX = 99;

struct Level {
    const char* name;
    uint8_t pool;
};

/* Pools: 10 + 10 + 35 + 35 + 40 + 39 + 36 + 36. Each is the number of items
 * make() numbers below; the test checks every one of them. */
constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Add to five", 10},            // a + b <= 5
    {"Take away to five", 10},      // a 2..5, b 1..a-1
    {"Add to ten", 35},             // a + b = 6..10
    {"Take away to ten", 35},       // a 6..10, b 1..a-1
    {"Zero", 40},                   // a + 0, 0 + a, a - 0, a - a
    {"How many more", 39},          // a + ? = whole, whole 5..10
    {"Past ten", 36},               // a + b = 11..18, a, b 2..9
    {"Back past ten", 36},          // a 11..18 take away b 2..9, left 2..9
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

/* "Level 8: " is nine characters; one digit is all the ladder needs. */
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

inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

struct Question {
    Op op = Op::Add;
    /* Add: the first group and the group that arrives. TakeAway: the start
     * and the group that goes. MakeUp: the shapes there and the frame's
     * places (`whole`); b is the answer. */
    uint8_t a = 0;
    uint8_t b = 0;
    uint8_t whole = 0;
    uint8_t answer = 0;
    uint8_t options[4] = {};
    uint8_t correct = 0;

    /* Shapes in each panel, all told, as the game draws them. */
    uint8_t leftCount() const { return op == Op::MakeUp ? whole : a; }
    uint8_t rightCount() const { return op == Op::MakeUp ? 0 : b; }
};

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* The four buttons as they are filled: the answer, then one likely mistake,
 * then a run of neighbours placed at random around the answer. Anything out
 * of range or already there is refused, so the four are four numbers. */
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

    void addRun(int low) {
        if (low < lo) low = lo;
        if (low + 3 > hi) low = hi - 3;
        for (int k = 0; k < 4; ++k) add(low + k);
    }

    void fill(int answer, bool upFirst) {
        for (int d = 1; n < 4 && d <= OPTION_MAX + 1; ++d) {
            add(upFirst ? answer + d : answer - d);
            add(upFirst ? answer - d : answer + d);
        }
    }
};

/* The (a, b) of item `item` among pairs with a in aLo..aHi and b in
 * bLo(a)..bHi(a), in row order. Every level is one of these walks, so the
 * pool sizes in LEVELS are what the test counts them to be. */
template <typename Lo, typename Hi>
inline bool walk(uint16_t item, uint8_t aLo, uint8_t aHi, Lo bLo, Hi bHi, uint8_t& a,
                 uint8_t& b) {
    uint16_t k = 0;
    for (int x = aLo; x <= aHi; ++x) {
        for (int y = bLo(x); y <= bHi(x); ++y) {
            if (k++ == item) {
                a = static_cast<uint8_t>(x);
                b = static_cast<uint8_t>(y);
                return true;
            }
        }
    }
    return false;
}

}   // namespace detail

/* The question for `item` of `level` (item below poolSize(level)), with its
 * options in a random order. The question depends only on the item; only the
 * options' order and which neighbours fill them depend on `rnd`. False for
 * an item outside the pool. */
inline bool make(uint8_t level, uint16_t item, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    if (item >= poolSize(level)) return false;

    uint32_t s = rnd ^ 0x9E3779B9UL;
    if (s == 0) s = 1;
    detail::stir(s);

    q = Question{};
    uint8_t a = 0;
    uint8_t b = 0;
    bool ok = false;
    switch (level) {
        case 1:
            q.op = Op::Add;
            ok = detail::walk(item, 1, 4, [](int) { return 1; }, [](int x) { return 5 - x; }, a, b);
            break;
        case 2:
            q.op = Op::TakeAway;
            ok = detail::walk(item, 2, 5, [](int) { return 1; }, [](int x) { return x - 1; }, a, b);
            break;
        case 3:
            q.op = Op::Add;
            ok = detail::walk(item, 1, 9, [](int x) { return x < 6 ? 6 - x : 1; },
                              [](int x) { return 10 - x; }, a, b);
            break;
        case 4:
            q.op = Op::TakeAway;
            ok = detail::walk(item, 6, 10, [](int) { return 1; }, [](int x) { return x - 1; }, a, b);
            break;
        case 5: {
            /* Four forms of ten: a + 0, 0 + a, a take away 0, a take away a. */
            const uint8_t n = static_cast<uint8_t>(1 + item % 10);
            const uint8_t form = static_cast<uint8_t>(item / 10);
            q.op = form < 2 ? Op::Add : Op::TakeAway;
            a = form == 1 ? 0 : n;
            b = form == 0 || form == 2 ? 0 : n;
            ok = true;
            break;
        }
        case 6:
            q.op = Op::MakeUp;
            ok = detail::walk(item, 5, 10, [](int) { return 1; }, [](int x) { return x - 1; }, a, b);
            /* walk gave (whole, there). */
            q.whole = a;
            a = b;
            b = static_cast<uint8_t>(q.whole - a);
            break;
        case 7:
            q.op = Op::Add;
            ok = detail::walk(item, 2, 9, [](int x) { return 11 - x; }, [](int) { return 9; }, a, b);
            break;
        default: {
            /* Walk (left, taken), then the start is their sum. */
            q.op = Op::TakeAway;
            uint8_t left = 0;
            ok = detail::walk(item, 2, 9, [](int x) { return 11 - x; }, [](int) { return 9; },
                              left, b);
            a = static_cast<uint8_t>(left + b);
            break;
        }
    }
    if (!ok) return false;
    q.a = a;
    q.b = b;

    int answer = 0;
    detail::Options o;
    o.lo = 0;
    /* The mistakes a player makes, one of which is always offered:
     *  Add       -- counting one group only (the larger, which is what is
     *               seen first), or recounting a shape (one too many);
     *  TakeAway  -- counting the ones that went (b), or not taking them
     *               away at all (a);
     *  MakeUp    -- counting the shapes that are there (a), or answering
     *               the size of the frame (whole).
     * Then four neighbours around the answer, the answer's place in the run
     * chosen at random, so it is neither always the biggest nor always in
     * the middle. */
    int mistakes[2];
    switch (q.op) {
        case Op::Add:
            answer = a + b;
            mistakes[0] = a > b ? a : b;
            mistakes[1] = answer + 1;
            o.hi = answer + 4 > 21 ? answer + 4 : 21;
            break;
        case Op::TakeAway:
            answer = a - b;
            mistakes[0] = b;
            mistakes[1] = a;
            o.hi = a + 2;
            break;
        case Op::MakeUp:
        default:
            answer = b;
            mistakes[0] = a;
            mistakes[1] = q.whole;
            o.hi = q.whole + 2;
            break;
    }
    if (o.hi > OPTION_MAX) o.hi = OPTION_MAX;
    if (o.hi < 4) o.hi = 4;
    o.add(answer);
    const uint8_t first = static_cast<uint8_t>(detail::stir(s) & 1U);
    if (!o.add(mistakes[first])) o.add(mistakes[1 - first]);
    o.addRun(answer - static_cast<int>(detail::stir(s) % 4));
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

}   // namespace ShapeArithCourse
