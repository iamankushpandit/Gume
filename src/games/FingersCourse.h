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

/* WHAT FINGERS TEACHES, LEVEL BY LEVEL.
 *
 * The game asked two things at random: how many fingers are up (1..10,
 * raised anywhere), and "show me N", which scored the moment N were up -- so
 * tapping fingers one after another without counting always won, and half
 * the questions tested nothing. Twenty questions, no levels.
 *
 * This game owns the numbers to ten on two hands: one hand first, raised in
 * order from the thumb; then a whole hand and some more, so five reads at a
 * glance; then any fingers, where showing a number ends on Done and can be
 * wrong; how many are DOWN, which is each number's partner to ten; the two
 * hands added; and changing a number already shown, counting on and back.
 * Past ten belongs to Counting and Shape Arith.
 *
 * Fingers are numbered 0..9: 0..4 the left hand from the thumb, 5..9 the
 * right hand from the thumb, so "in order from the thumb" is the same
 * numbering on both hands and the game decides where each one is drawn.
 *
 * Pure C++, no Arduino and no heap: test/host/fingers_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace FingersCourse {

enum class Ask : uint8_t {
    HowMany,    // fingers are up: how many?
    ShowMe,     // all down: raise N, then Done (levels 1-2 score at N)
    HowManyDown,// fingers are up: how many are down?
    InAll,      // some up on each hand: how many in all?
    MakeIt,     // k are up: make it N, then Done
};

constexpr uint8_t LEVEL_COUNT = 6;
constexpr uint8_t FINGERS = 10;
constexpr uint16_t ALL_UP = (1U << FINGERS) - 1;

struct Level {
    const char* name;
    uint8_t pool;
};

/* Pools: 10 + 10 + 20 + 11 + 36 + 110. Each is the number of items make()
 * numbers below; the test checks every one of them. */
constexpr Level LEVELS[LEVEL_COUNT] = {
    {"One hand", 10},               // 1..5 in order, counted and shown
    {"A hand and more", 10},        // 6..10 as five and some more
    {"Any fingers", 20},            // 1..10 anywhere, counted and shown
    {"How many are down", 11},      // 0..10 up, the partner to ten
    {"Both hands", 36},             // 0..5 on each hand, how many in all
    {"Change it", 110},             // k up, make it N
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

/* Whether the player answers with the hands (and Done) rather than with
 * one of four numbers. */
inline bool showsWithHands(Ask ask) {
    return ask == Ask::ShowMe || ask == Ask::MakeIt;
}

inline uint8_t countUp(uint16_t mask) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < FINGERS; ++i) {
        if ((mask >> i) & 1U) ++n;
    }
    return n;
}

/* The first `n` fingers in order: the left hand from the thumb, then the
 * right hand from the thumb. Six is a whole hand and one more. */
inline uint16_t inOrder(uint8_t n) {
    return static_cast<uint16_t>((1U << (n > FINGERS ? FINGERS : n)) - 1);
}

struct Question {
    Ask ask = Ask::HowMany;
    /* The fingers up when the question starts. */
    uint16_t raised = 0;
    /* What is right: the number to tap, or the number to have up on Done. */
    uint8_t answer = 0;
    /* InAll: how many on the left hand and the right. */
    uint8_t left = 0;
    uint8_t right = 0;
    /* Number questions only. */
    uint8_t options[4] = {};
    uint8_t correct = 0;
    /* Levels 1 and 2 keep a running "Up: n" while showing, and score the
     * moment the count is reached; from level 3 it is Done that answers. */
    bool countsAloud = false;
};

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

struct Options {
    uint8_t v[4] = {};
    uint8_t n = 0;
    int lo = 0;
    int hi = FINGERS;

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
        for (int d = 1; n < 4 && d <= FINGERS + 1; ++d) {
            add(upFirst ? answer + d : answer - d);
            add(upFirst ? answer - d : answer + d);
        }
    }
};

/* `count` fingers chosen at random, all ways equally likely. */
inline uint16_t scatter(uint8_t count, uint32_t& s) {
    uint8_t cells[FINGERS];
    for (uint8_t i = 0; i < FINGERS; ++i) cells[i] = i;
    uint16_t mask = 0;
    for (uint8_t i = 0; i < count && i < FINGERS; ++i) {
        const uint8_t j = static_cast<uint8_t>(i + stir(s) % (FINGERS - i));
        const uint8_t t = cells[i];
        cells[i] = cells[j];
        cells[j] = t;
        mask = static_cast<uint16_t>(mask | (1U << cells[i]));
    }
    return mask;
}

}   // namespace detail

/* The question for `item` of `level` (item below poolSize(level)). The
 * question depends only on the item; which fingers are up where the level
 * scatters them, and the options' order, depend on `rnd`. False for an item
 * outside the pool. */
inline bool make(uint8_t level, uint16_t item, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    if (item >= poolSize(level)) return false;

    uint32_t s = rnd ^ 0x9E3779B9UL;
    if (s == 0) s = 1;
    detail::stir(s);

    q = Question{};
    /* The mistake each kind invites, offered beside the answer:
     *  HowMany     -- one out, either way (a finger skipped or counted twice);
     *                 past five, the second hand alone (the five not seen);
     *  HowManyDown -- the ones that are up;
     *  InAll       -- one hand only. */
    int mistake = -1;
    detail::Options o;
    o.lo = 1;
    switch (level) {
        case 1:
        case 2: {
            const uint8_t n = static_cast<uint8_t>((level == 1 ? 1 : 6) + item % 5);
            q.ask = item < 5 ? Ask::HowMany : Ask::ShowMe;
            q.answer = n;
            q.raised = q.ask == Ask::HowMany ? inOrder(n) : 0;
            q.countsAloud = true;
            if (level == 2) mistake = n - 5;
            break;
        }
        case 3: {
            const uint8_t n = static_cast<uint8_t>(1 + item % 10);
            q.ask = item < 10 ? Ask::HowMany : Ask::ShowMe;
            q.answer = n;
            q.raised = q.ask == Ask::HowMany ? detail::scatter(n, s) : 0;
            break;
        }
        case 4: {
            const uint8_t up = static_cast<uint8_t>(item);
            q.ask = Ask::HowManyDown;
            q.raised = detail::scatter(up, s);
            q.answer = static_cast<uint8_t>(FINGERS - up);
            o.lo = 0;
            mistake = up;
            break;
        }
        case 5: {
            q.ask = Ask::InAll;
            q.left = static_cast<uint8_t>(item / 6);
            q.right = static_cast<uint8_t>(item % 6);
            q.raised = static_cast<uint16_t>(inOrder(q.left) | (inOrder(q.right) << 5));
            q.answer = static_cast<uint8_t>(q.left + q.right);
            o.lo = 0;
            mistake = q.left > q.right ? q.left : q.right;
            break;
        }
        default: {
            /* Every ordered pair (k, N) of 0..10 with k != N. */
            const uint8_t k = static_cast<uint8_t>(item / 10);
            uint8_t target = static_cast<uint8_t>(item % 10);
            if (target >= k) ++target;
            q.ask = Ask::MakeIt;
            q.raised = inOrder(k);
            q.answer = target;
            break;
        }
    }

    if (showsWithHands(q.ask)) return true;

    const int answer = q.answer;
    o.add(answer);
    if (mistake >= 0) o.add(mistake);
    if (level == 2) {
        /* The mistake there is always below the answer, and ten is the top
         * of the range, so a run of neighbours would leave the answer the
         * biggest of the four most of the time. Two neighbours from either
         * side instead, chosen at random. */
        int8_t near[4] = {-2, -1, 1, 2};
        for (uint8_t i = 3; i > 0; --i) {
            const uint8_t j = static_cast<uint8_t>(detail::stir(s) % (i + 1));
            const int8_t t = near[i];
            near[i] = near[j];
            near[j] = t;
        }
        for (uint8_t i = 0; i < 4; ++i) o.add(answer + near[i]);
    } else {
        o.addRun(answer - static_cast<int>(detail::stir(s) % 4));
    }
    o.fill(answer, (detail::stir(s) & 1U) != 0);

    for (uint8_t i = 3; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % (i + 1));
        const uint8_t t = o.v[i];
        o.v[i] = o.v[j];
        o.v[j] = t;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        q.options[i] = o.v[i];
        if (o.v[i] == q.answer) q.correct = i;
    }
    return true;
}

}   // namespace FingersCourse
