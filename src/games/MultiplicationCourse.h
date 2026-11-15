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
#include <string.h>
#include <stdio.h>

/* Multiply as a course: the tables in the order they are taught, then the
 * missing number, then division as the same facts read backwards, then both
 * mixed.
 *
 * The game used to pick a table and a multiplier at random from a set that
 * grew with the score of the visit. So every visit began with the twos, table
 * 1 sat in every level from the second up, 6 x 12 came before 7 x 3, and one
 * wrong option in three was a number within twelve of the answer that was in
 * no table at all. Here each level is a set of facts that can be numbered, so
 * engine/Course.h's deck can deal all of them before any comes back, and the
 * wrong options are the mistakes a learner actually makes with that fact.
 *
 * Pure C++, no Arduino and no heap: test/host/multiplication_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number.
 */
namespace MultiplicationCourse {

/* What a level asks.
 *   Tables   a x b = ?, a from a set of tables, b from a range. A fact is
 *            dealt once however it is written: 2 x 5 and 5 x 2 are one item,
 *            and which way round it is shown is a coin toss.
 *   Missing  ? x b = c, or a x ? = c: the step between knowing 7 x 8 and
 *            knowing 56 / 8.
 *   Divide   a / b = ?, b from a set of divisors, the answer from a range.
 *   Mixed    the Tables facts and the Divide facts of one set, together. */
enum class Kind : uint8_t { Tables, Missing, Divide, Mixed };

/* How a question is drawn: `shown[0] op shown[1] = shown[2]`, with the one at
 * `hidden` as "?". */
enum class Op : uint8_t { Times, Divide };

struct LevelSpec {
    const char* name;
    Kind kind;
    uint16_t set;       // bit n: table n (Tables, Mixed) or divisor n (Divide, Mixed)
    uint8_t lo;         // multiplier, hidden factor or answer: lo..hi
    uint8_t hi;
};

constexpr uint16_t bits(uint8_t a) { return static_cast<uint16_t>(1U << a); }
constexpr uint16_t between(uint8_t lo, uint8_t hi) {
    return static_cast<uint16_t>(((1U << (hi + 1)) - 1U) & ~((1U << lo) - 1U));
}

/* The ladder. Each name follows "Level 12: " on the prompt line, in font 2 on
 * a strip 280px wide, so it is short; the host test measures every one.
 *
 * Twos, fives and tens first because they are the tables a player can already
 * count in; then threes and fours; then a level that mixes everything so far
 * and is where 0 and 1 come in, because x0 and x1 are rules to meet once, not
 * a share of every level; then the hard pairs two at a time, 6 and 8 together
 * because 8 x 6 is 6 x 8, likewise 7 and 9. From level 4 the multiplier
 * starts at 2: x1 has been learned, and it was filling the hard levels with
 * questions that are not hard. */
inline constexpr LevelSpec LEVELS[] = {
    {"Twos, fives, tens",  Kind::Tables,  bits(2) | bits(5) | bits(10), 1, 10},
    {"Threes and fours",   Kind::Tables,  bits(3) | bits(4), 1, 10},
    {"Mix with 0 and 1",   Kind::Tables,
        bits(0) | bits(1) | bits(2) | bits(3) | bits(4) | bits(5) | bits(10), 0, 12},
    {"Sixes and eights",   Kind::Tables,  bits(6) | bits(8), 2, 12},
    {"Sevens and nines",   Kind::Tables,  bits(7) | bits(9), 2, 12},
    {"Elevens, twelves",   Kind::Tables,  bits(11) | bits(12), 2, 12},
    {"All the tables",     Kind::Tables,  between(2, 12), 2, 12},
    {"Missing number",     Kind::Missing, between(2, 12), 2, 12},
    {"Divide by 2, 5, 10", Kind::Divide,  bits(2) | bits(5) | bits(10), 1, 12},
    {"Divide by 3, 4, 6, 8", Kind::Divide, bits(3) | bits(4) | bits(6) | bits(8), 2, 12},
    {"Divide by 7, 9, 11, 12", Kind::Divide, bits(7) | bits(9) | bits(11) | bits(12), 2, 12},
    {"Times and divide",   Kind::Mixed,   between(2, 12), 2, 12},
};
inline constexpr uint8_t LEVEL_COUNT = sizeof(LEVELS) / sizeof(LEVELS[0]);

/* The largest table or multiplier any level uses. */
inline constexpr uint8_t TOP = 12;
/* The largest number a question or an option can show. Three figures: the
 * button labels and the equation pieces are sized for it. */
inline constexpr uint16_t MAX_SHOWN = 999;
/* What the deck in engine/Course.h can number. */
inline constexpr uint16_t MAX_POOL = 256;
/* The longest level name, in characters. Not NAME_MAX: that is a POSIX macro,
 * which the PC build never sees and the ESP32 toolchain does. */
inline constexpr uint8_t LEVEL_NAME_MAX = 22;

constexpr const LevelSpec& spec(uint8_t level) {
    return LEVELS[(level < 1 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level)) - 1];
}

constexpr bool inSet(uint16_t set, uint8_t n) { return n <= 15 && ((set >> n) & 1U) != 0; }

constexpr uint8_t setCount(uint16_t set) {
    uint8_t n = 0;
    for (uint8_t i = 0; i <= TOP; ++i) {
        if (inSet(set, i)) ++n;
    }
    return n;
}

/* The n-th member of a set, counting from 0 in ascending order. */
constexpr uint8_t setMember(uint16_t set, uint16_t n) {
    for (uint8_t i = 0; i <= TOP; ++i) {
        if (!inSet(set, i)) continue;
        if (n == 0) return i;
        --n;
    }
    return 0;
}

/* Is table a times b a fact of its own, or the same fact as table b times a,
 * already counted? The smaller table owns it. */
constexpr bool ownsFact(const LevelSpec& s, uint8_t a, uint8_t b) {
    if (!inSet(s.set, a) || b < s.lo || b > s.hi) return false;
    const bool mirrored = inSet(s.set, b) && b < a && a >= s.lo && a <= s.hi;
    return !mirrored;
}

constexpr uint16_t factCount(const LevelSpec& s) {
    uint16_t n = 0;
    for (uint8_t a = 0; a <= TOP; ++a) {
        for (uint8_t b = s.lo; b <= s.hi; ++b) {
            if (ownsFact(s, a, b)) ++n;
        }
    }
    return n;
}

constexpr uint16_t span(const LevelSpec& s) { return static_cast<uint16_t>(s.hi - s.lo + 1); }

constexpr uint16_t divisionCount(const LevelSpec& s) {
    return static_cast<uint16_t>(setCount(s.set) * span(s));
}

/* How many items the deck numbers at this level. */
constexpr uint16_t poolSize(uint8_t level) {
    const LevelSpec& s = spec(level);
    switch (s.kind) {
        case Kind::Tables:  return factCount(s);
        case Kind::Missing: return static_cast<uint16_t>(setCount(s.set) * span(s));
        case Kind::Divide:  return divisionCount(s);
        case Kind::Mixed:   return static_cast<uint16_t>(factCount(s) + divisionCount(s));
    }
    return 0;
}

constexpr bool everyPoolFits() {
    for (uint8_t l = 1; l <= LEVEL_COUNT; ++l) {
        const LevelSpec& s = spec(l);
        if (s.lo > s.hi || s.hi > TOP || (s.set >> (TOP + 1)) != 0) return false;
        if (poolSize(l) == 0 || poolSize(l) > MAX_POOL) return false;
    }
    return true;
}
static_assert(everyPoolFits(),
              "a level reaches past TOP, has no questions, or more than the deck can number");

constexpr bool everyNameFits() {
    for (uint8_t l = 0; l < LEVEL_COUNT; ++l) {
        uint8_t n = 0;
        while (LEVELS[l].name[n] != '\0') ++n;
        if (n == 0 || n > LEVEL_NAME_MAX) return false;
    }
    return true;
}
static_assert(everyNameFits(), "a level name is longer than the prompt line has room for");

/* The biggest number any level can put on the screen: a product of the two
 * largest factors, or a wrong option a little past it (see makeOptions()). */
static_assert((TOP + 2) * TOP <= MAX_SHOWN, "an option would not fit in three figures");

/* Where the game draws a question, stated here rather than in the game so
 * that the host test measures every question against the very numbers the
 * screen uses. The game asserts CANVAS_W against GAME_CANVAS_WIDTH.
 *
 * The equation is not centred as one string. Its operator stands at OP_X and
 * the two sides are hung off it, the left one ending OP_GAP before it and the
 * right one starting OP_GAP after. Font 4 has no division sign, so the game
 * makes one from two glyphs drawn over each other, and a string with a gap in
 * it cannot be centred without measuring it -- which on a scaled panel
 * textWidth() answers in the panel's pixels rather than the canvas's. A fixed
 * operator also keeps the sign still from one question to the next. OP_X is
 * the place that centres "7 x 8 = ?". */
namespace Screen {
inline constexpr int16_t CANVAS_W = 320;
inline constexpr int16_t PANEL_X = 26;
inline constexpr int16_t PANEL_Y = 70;
inline constexpr int16_t PANEL_W = 268;
inline constexpr int16_t PANEL_H = 56;
inline constexpr int16_t PANEL_PAD = 8;     // past the rounded corner
inline constexpr int16_t OP_X = 142;
inline constexpr int16_t OP_GAP = 12;       // half an "x" in font 4, and a space
inline constexpr int16_t LEFT_MAX_W = OP_X - OP_GAP - (PANEL_X + PANEL_PAD);
inline constexpr int16_t RIGHT_MAX_W = PANEL_X + PANEL_W - PANEL_PAD - (OP_X + OP_GAP);
inline constexpr int16_t BUTTON_W = 132;
inline constexpr int16_t PROMPT_X = 20;
inline constexpr int16_t PROMPT_W = CANVAS_W - 2 * PROMPT_X;
static_assert(LEFT_MAX_W > 0 && RIGHT_MAX_W > 0, "the operator is outside the panel");
}   // namespace Screen

/* The two sides of a question as text: "56" and "8 = ?". `left` holds three
 * figures, `right` "12 = 144"; the sizes say so and the test measures them. */
inline constexpr size_t LEFT_SIZE = 4;
inline constexpr size_t RIGHT_SIZE = 12;
static_assert(MAX_SHOWN <= 999 && LEFT_SIZE >= 3 + 1 && RIGHT_SIZE >= 2 + 3 + 3 + 1,
              "a side of the largest question would be cut short");

struct Question {
    Op op = Op::Times;
    uint16_t shown[3] = {0, 0, 0};
    uint8_t hidden = 2;             // which of shown[] is "?"
    uint16_t answer = 0;
    uint16_t options[4] = {0, 0, 0, 0};
    uint8_t correct = 0;            // which option is the answer

    const char* prompt() const {
        if (op == Op::Divide) return "Tap the answer";
        return hidden == 2 ? "Tap the product" : "Tap the missing number";
    }

    /* The text either side of the operator. */
    void sides(char* left, size_t leftSize, char* right, size_t rightSize) const {
        char n[3][4];
        for (uint8_t i = 0; i < 3; ++i) {
            if (i == hidden) {
                snprintf(n[i], sizeof(n[i]), "?");
            } else {
                /* Never above MAX_SHOWN; the modulo tells the compiler so. */
                snprintf(n[i], sizeof(n[i]), "%u",
                         static_cast<unsigned>(shown[i] % (MAX_SHOWN + 1)));
            }
        }
        snprintf(left, leftSize, "%s", n[0]);
        snprintf(right, rightSize, "%s = %s", n[1], n[2]);
    }
};

/* A small stream of its own, so one random number from the caller can make
 * every choice a question needs. xorshift32. */
struct Stream {
    uint32_t s;
    explicit Stream(uint32_t seed) : s(seed != 0 ? seed : 0x9E3779B9U) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }
};

namespace detail {

/* A short list of candidate wrong answers, each kept once and only if it is
 * wrong and fits on a button. */
struct Candidates {
    static constexpr uint8_t CAP = 8;
    uint16_t v[CAP] = {};
    uint8_t n = 0;

    void add(int32_t value, uint16_t answer, int32_t least) {
        if (value < least || value > MAX_SHOWN || value == answer || n >= CAP) return;
        for (uint8_t i = 0; i < n; ++i) {
            if (v[i] == value) return;
        }
        v[n++] = static_cast<uint16_t>(value);
    }
};

/* The wrong answers for one question, sorted two ways: likely or less
 * likely, and below or above the answer.
 *
 * The second split is what keeps the options from giving the answer away.
 * The likely mistakes for 7 x 8 are 49, 48, 63 and 64 -- one step back or on
 * in either table -- and any three of those four have the answer somewhere
 * in the middle of them. A player who noticed would strike out the largest
 * and the smallest every time and toss a coin between the other two. So how
 * many of the three sit below the answer is chosen first, at random, and the
 * mistakes are taken to fit it. */
struct Mistakes {
    Candidates below[2];        // [0] likely, [1] less likely
    Candidates above[2];
    uint16_t answer;
    int32_t least;

    Mistakes(uint16_t ans, int32_t lowest) : answer(ans), least(lowest) {}

    void add(int32_t value, bool likely) {
        Candidates* side = value < static_cast<int32_t>(answer) ? below : above;
        side[likely ? 0 : 1].add(value, answer, least);
    }

    /* Moves mistakes from one side into `chosen` until it holds `upTo`:
     * the likely ones first, each picked at random from those left. */
    static void take(Candidates* side, Candidates& chosen, uint8_t upTo, uint16_t answer,
                     int32_t least, Stream& rnd) {
        for (uint8_t k = 0; k < 2; ++k) {
            Candidates& list = side[k];
            while (chosen.n < upTo && list.n > 0) {
                const uint8_t i = static_cast<uint8_t>(rnd.below(list.n));
                const uint16_t value = list.v[i];
                list.v[i] = list.v[--list.n];
                chosen.add(value, answer, least);
            }
        }
    }

    /* Three different wrong answers. Only when both lists run dry -- 0 x 0,
     * and little else -- do the numbers either side of the answer fill in. */
    void choose(Stream& rnd, uint16_t out[3]) {
        Candidates chosen;
        const uint8_t wantBelow = static_cast<uint8_t>(rnd.below(4));
        take(below, chosen, wantBelow, answer, least, rnd);
        take(above, chosen, 3, answer, least, rnd);
        take(below, chosen, 3, answer, least, rnd);
        for (int32_t k = 1; chosen.n < 3; ++k) {
            chosen.add(static_cast<int32_t>(answer) + k, answer, least);
            chosen.add(static_cast<int32_t>(answer) - k, answer, least);
        }
        for (uint8_t i = 0; i < 3; ++i) out[i] = chosen.v[i];
    }
};

}   // namespace detail

/* Four options, one of them the answer.
 *
 * The wrong ones are what a player who does not yet know the fact says:
 *   a x b = ?   the next or the previous one in either table, a x (b +- 1)
 *               and (a +- 1) x b -- counting on or back one step too many.
 *               With a 0 or a 1, adding instead of multiplying (1 x 7 = 8)
 *               and mixing up the two rules (1 x 7 = 0, 0 x 7 = 7) are the
 *               mistakes, and both fall out of the same list; a + b is a
 *               likely mistake only there, because 7 + 8 = 15 offered for
 *               7 x 8 is struck out at a glance and leaves three choices.
 *   ? x b = c   one more or one fewer than the answer, or b itself; then two
 *               or three either side.
 *   a / b = ?   the same, the divisor being the one copied -- 56 / 8 = 8.
 * Every one is a different number from the others and from the answer, and
 * none can be argued right: each form has exactly one right answer. */
inline void makeOptions(Question& q, Stream& rnd) {
    const uint16_t ans = q.answer;
    const bool product = q.op == Op::Times && q.hidden == 2;
    /* A product can be 0, and 1 x 7 = 0 is a real mistake. A missing factor
     * or a quotient here is never 0, and offering 0 for one is not. */
    detail::Mistakes m(ans, product ? 0 : 1);

    if (product) {
        const int32_t a = q.shown[0];
        const int32_t b = q.shown[1];
        m.add(a * (b + 1), true);
        m.add(a * (b - 1), true);
        m.add((a + 1) * b, true);
        m.add((a - 1) * b, true);
        m.add(a + b, a <= 1 || b <= 1);
        m.add(a * (b + 2), false);
        m.add(a * (b - 2), false);
        m.add((a + 2) * b, false);
        m.add((a - 2) * b, false);
    } else {
        /* The missing number and the quotient are both "how many of the
         * other one", and go wrong the same way. */
        const int32_t h = ans;
        const int32_t other = q.op == Op::Divide ? q.shown[1] : q.shown[q.hidden == 0 ? 1 : 0];
        m.add(h + 1, true);
        m.add(h - 1, true);
        m.add(other, true);
        /* Further out, so that each side has three to offer: with only two
         * above the answer it could never be the smallest of the four. */
        m.add(h + 2, false);
        m.add(h - 2, false);
        m.add(h + 3, false);
        m.add(h - 3, false);
    }

    uint16_t wrong[3];
    m.choose(rnd, wrong);
    q.correct = static_cast<uint8_t>(rnd.below(4));
    uint8_t w = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        q.options[i] = i == q.correct ? ans : wrong[w++];
    }
}

namespace detail {

inline void tableFact(const LevelSpec& s, uint16_t item, uint8_t& a, uint8_t& b) {
    for (uint8_t x = 0; x <= TOP; ++x) {
        for (uint8_t y = s.lo; y <= s.hi; ++y) {
            if (!ownsFact(s, x, y)) continue;
            if (item == 0) {
                a = x;
                b = y;
                return;
            }
            --item;
        }
    }
    a = 0;
    b = 0;
}

inline void product(Question& q, uint8_t a, uint8_t b, Stream& rnd) {
    if ((rnd.next() & 1U) != 0) {
        const uint8_t t = a;
        a = b;
        b = t;
    }
    q.op = Op::Times;
    q.shown[0] = a;
    q.shown[1] = b;
    q.shown[2] = static_cast<uint16_t>(a * b);
    q.hidden = 2;
    q.answer = q.shown[2];
}

inline void division(Question& q, const LevelSpec& s, uint16_t item) {
    const uint8_t divisor = setMember(s.set, item / span(s));
    const uint8_t quotient = static_cast<uint8_t>(s.lo + item % span(s));
    q.op = Op::Divide;
    q.shown[0] = static_cast<uint16_t>(divisor * quotient);
    q.shown[1] = divisor;
    q.shown[2] = quotient;
    q.hidden = 2;
    q.answer = quotient;
}

}   // namespace detail

/* The question numbered `item` at `level`, its four options chosen with
 * `rnd`. `item` is below poolSize(level); one past it is taken modulo. */
inline Question makeQuestion(uint8_t level, uint16_t item, uint32_t rnd) {
    const LevelSpec& s = spec(level);
    const uint16_t pool = poolSize(level);
    if (pool != 0) item = static_cast<uint16_t>(item % pool);
    Stream stream(rnd);
    Question q;

    switch (s.kind) {
        case Kind::Tables: {
            uint8_t a = 0, b = 0;
            detail::tableFact(s, item, a, b);
            detail::product(q, a, b, stream);
            break;
        }
        case Kind::Missing: {
            /* Item = the factor shown, then the factor hidden: 7 x ? = 56
             * and 8 x ? = 56 are two questions with two answers. Which side
             * of the sign the "?" is on is a coin toss. */
            const uint8_t known = setMember(s.set, item / span(s));
            const uint8_t hiddenFactor = static_cast<uint8_t>(s.lo + item % span(s));
            q.op = Op::Times;
            q.hidden = (stream.next() & 1U) != 0 ? 0 : 1;
            q.shown[q.hidden] = hiddenFactor;
            q.shown[q.hidden == 0 ? 1 : 0] = known;
            q.shown[2] = static_cast<uint16_t>(known * hiddenFactor);
            q.answer = hiddenFactor;
            break;
        }
        case Kind::Divide:
            detail::division(q, s, item);
            break;
        case Kind::Mixed: {
            const uint16_t facts = factCount(s);
            if (item < facts) {
                uint8_t a = 0, b = 0;
                detail::tableFact(s, item, a, b);
                detail::product(q, a, b, stream);
            } else {
                detail::division(q, s, static_cast<uint16_t>(item - facts));
            }
            break;
        }
    }
    makeOptions(q, stream);
    return q;
}

/* "Level 4: Sixes and eights", for the prompt line after a climb. */
inline void levelBanner(char* out, size_t size, uint8_t level) {
    snprintf(out, size, "Level %u: %s", static_cast<unsigned>(level), spec(level).name);
}
/* "Level 12: " plus the longest name, and its terminator. */
inline constexpr size_t BANNER_SIZE = 10 + LEVEL_NAME_MAX + 1;

}   // namespace MultiplicationCourse
