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

#include "engine/Course.h"

/* WHAT MATH TEACHES, IN THE ORDER IT TEACHES IT.
 *
 * Math used to be five bands of random numbers, picked by how many answers
 * this visit had got right: every visit began at 3 + 4, the top was 99 + 99
 * twenty answers later, and nothing in between asked for the steps a player
 * actually has to learn -- partners of ten, crossing ten, carrying, borrowing.
 * A player who could find the units digit could find the right button, so
 * the levels meant to teach carrying never needed a carry.
 *
 * So this is a course of twelve steps, each one thing, each building on the
 * last. The first five are small enough to number, and Course::QuestionDeck
 * deals every question of the level before any comes back. From level 6 the
 * space is hundreds or thousands of sums, and they are drawn at random with
 * the game's RecentQuestions keeping the last few from returning.
 *
 * The wrong buttons are the mistakes a learner really makes at that step --
 * forgetting the carried ten, taking the smaller digit from the larger,
 * doing the sign that is shown in a missing-number question -- because a
 * wrong option nobody would choose teaches nothing, and four random numbers
 * near the answer let the last digit give it away.
 *
 * Pure C++, no Arduino: test/host/math_course_test.cpp compiles it as it is
 * and asks every question of every numbered level. Randomness comes in as a
 * number, so the game passes esp_random() and the test passes its own. */
namespace MathCourse {

constexpr uint8_t LEVEL_COUNT = 12;

/* The largest number any question puts on a button or in the equation, and
 * the largest operand. 999 + 999 is the top of the course; the options built
 * from it (forgetting a carry, a hundred too many) stay under four digits'
 * worth of room. */
constexpr int16_t MAX_OPERAND = 999;
constexpr int16_t MAX_SHOWN = 9999;

/* What the screen leaves for each piece of text, in pixels. MathGame.cpp
 * static_asserts its own layout against these, so a change on either side
 * that would stop a question fitting fails to compile rather than cutting a
 * digit off on the panel. */
constexpr int16_t QUESTION_TEXT_W = 252;   // the question panel, less 8px a side
constexpr int16_t PROMPT_W = 280;          // the strip between panel and buttons
constexpr int16_t LABEL_W = 120;           // an answer button, less drawButton's 6px a side

/* Font 4's advance widths for the only characters an equation uses. Font 4
 * has no other glyphs that matter here; these are TFT_eSPI's widtbl_f32. */
constexpr int16_t FONT4_DIGIT = 14;
constexpr int16_t FONT4_SPACE = 5;
constexpr int16_t FONT4_PLUS = 10;
constexpr int16_t FONT4_EQUALS = 14;
constexpr int16_t FONT4_QUESTION = 13;

constexpr int16_t digitsOf(int32_t n) {
    int16_t d = 1;
    while (n >= 10) {
        n /= 10;
        ++d;
    }
    return d;
}

/* The widest equation is two of the largest numbers and the question mark:
 * "9999 + 9999 = ?" bounds every form, the missing-number ones included. */
constexpr int16_t EQUATION_CHARS = 2 * digitsOf(MAX_SHOWN) + 3 + 3 + 1;   // " + ", " = ", "?"
constexpr int16_t EQUATION_PX = 2 * digitsOf(MAX_SHOWN) * FONT4_DIGIT + 4 * FONT4_SPACE +
                                FONT4_PLUS + FONT4_EQUALS + FONT4_QUESTION;
static_assert(EQUATION_PX <= QUESTION_TEXT_W, "the widest equation does not fit the panel at font 4");
static_assert(digitsOf(MAX_SHOWN) * FONT4_DIGIT <= LABEL_W,
              "the largest option does not fit an answer button at font 4");
static_assert(MAX_SHOWN <= 32767, "options are int16_t");
static_assert(2 * MAX_OPERAND + 100 <= MAX_SHOWN, "a three-digit sum's options would overflow");

/* ---- the levels ---------------------------------------------------------- */

/* The bounds the numbered levels are built from, stated once: the generator
 * walks them and the pool sizes are counted from them, so the two cannot come
 * to disagree. */
constexpr int16_t WITHIN = 10;        // levels 1 and 2: sums and take-aways to ten
constexpr int16_t BOND_LOW = 5;       // level 3: partners of 5 up to partners of 10
constexpr int16_t BOND_HIGH = 10;
constexpr int16_t BOND_FORMS = 3;     // a + ? = N, ? + a = N, N - ? = a
constexpr int16_t CROSS_LOW = 2;      // level 4: single digits whose sum passes ten
constexpr int16_t TEENS = 9;          // level 4: 10 + n and n + 10; level 5: 11..19

constexpr uint16_t addWithinCount() {          // a + b, a and b >= 1, a + b <= 10
    uint16_t n = 0;
    for (int16_t a = 1; a < WITHIN; ++a) n = static_cast<uint16_t>(n + (WITHIN - a));
    return n;
}
constexpr uint16_t takeWithinCount() {         // a - b, 1 <= b <= a <= 10
    uint16_t n = 0;
    for (int16_t a = 1; a <= WITHIN; ++a) n = static_cast<uint16_t>(n + a);
    return n;
}
constexpr uint16_t bondsPerForm() {            // (N, a) with a in 0..N
    uint16_t n = 0;
    for (int16_t total = BOND_LOW; total <= BOND_HIGH; ++total) n = static_cast<uint16_t>(n + total + 1);
    return n;
}
constexpr uint16_t crossPairCount() {          // a, b in 2..9, a + b >= 11
    uint16_t n = 0;
    for (int16_t a = CROSS_LOW; a <= 9; ++a) n = static_cast<uint16_t>(n + (a - 1));
    return n;
}

struct Level {
    const char* name;
    uint16_t pool;      // questions in the level, numbered 0..pool-1; 0 = drawn at random
    bool stories;       // may a question be asked as a word problem
    int16_t floor;      // the smallest value a button may show: 0 only where 0 is an answer
};

/* Stories arrive at level 4. Below it a player is still learning what the
 * sign means, and reading a sentence is the harder half at that stage; at
 * level 12 the question IS the shape of the equation, which a story hides. */
inline constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Add within 10", addWithinCount(), false, 1},
    {"Take away within 10", takeWithinCount(), false, 0},
    {"Number bonds", static_cast<uint16_t>(bondsPerForm() * BOND_FORMS), false, 0},
    {"Cross ten", static_cast<uint16_t>(crossPairCount() + 2 * TEENS), true, 1},
    {"Back through ten", static_cast<uint16_t>(TEENS * 9), true, 1},
    {"Tens and ones", 0, true, 1},
    {"Ones across a ten", 0, true, 1},
    {"Two digits, no regrouping", 0, true, 1},
    {"Carrying", 0, true, 1},
    {"Borrowing", 0, true, 1},
    {"Three digits", 0, true, 1},
    {"Missing numbers", 0, false, 1},
};

constexpr bool poolsFitTheDeck() {
    for (uint8_t i = 0; i < LEVEL_COUNT; ++i) {
        if (LEVELS[i].pool > Course::QuestionDeck::MAX_ITEMS) return false;
    }
    return true;
}
static_assert(poolsFitTheDeck(), "a numbered level is larger than the deck can deal");

inline const Level& level(uint8_t n) {
    if (n < 1) n = 1;
    if (n > LEVEL_COUNT) n = LEVEL_COUNT;
    return LEVELS[n - 1];
}
inline uint16_t poolSize(uint8_t n) { return level(n).pool; }
inline bool numbered(uint8_t n) { return poolSize(n) > 0; }

/* "Level 12: " and the longest name, which the game names in its prompt
 * strip on the way up. */
constexpr size_t longestName() {
    size_t best = 0;
    for (uint8_t i = 0; i < LEVEL_COUNT; ++i) {
        size_t n = 0;
        while (LEVELS[i].name[n] != '\0') ++n;
        if (n > best) best = n;
    }
    return best;
}
constexpr size_t LEVEL_LINE_MAX = sizeof("Level 12: ") + longestName();
static_assert(LEVEL_COUNT < 100, "the level line has room for two digits");

inline void levelLine(uint8_t n, char* out, size_t len) {
    snprintf(out, len, "Level %u: %s", static_cast<unsigned>(n), level(n).name);
}

/* ---- a question ---------------------------------------------------------- */

enum class Form : uint8_t {
    Sum,            // a + b = ?
    Difference,     // a - b = ?
    MissingSecond,  // a + ? = c
    MissingFirst,   // ? + b = c
    MissingTaken,   // a - ? = c
    MissingStart,   // ? - b = c
};

/* Always the whole true equation, a + b = c or a - b = c, whichever number
 * the form hides: the answer is one of the three, and the other two are what
 * the player is shown. */
struct Question {
    Form form = Form::Sum;
    int16_t a = 0;
    int16_t b = 0;
    int16_t c = 0;
    int16_t answer = 0;
    int16_t options[4] = {};
    uint8_t correct = 0;
};

inline bool adds(Form f) {
    return f == Form::Sum || f == Form::MissingSecond || f == Form::MissingFirst;
}

inline void set(Question& q, Form f, int16_t a, int16_t b) {
    q.form = f;
    q.a = a;
    q.b = b;
    q.c = static_cast<int16_t>(adds(f) ? a + b : a - b);
    switch (f) {
        case Form::Sum:
        case Form::Difference:
            q.answer = q.c;
            break;
        case Form::MissingFirst:
        case Form::MissingStart:
            q.answer = a;
            break;
        default:
            q.answer = b;
            break;
    }
}

/* Many numbers from the one that was handed in. xorshift: the stream only has
 * to differ between seeds, not to be good. */
class Dice {
public:
    explicit Dice(uint32_t seed) : s_(seed ^ 0x9E3779B9u) {
        if (s_ == 0) s_ = 1;
        next();
    }
    uint32_t next() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return s_;
    }
    /** 0..n-1 */
    int16_t below(int16_t n) { return n <= 1 ? 0 : static_cast<int16_t>(next() % static_cast<uint32_t>(n)); }
    /** lo..hi, both included */
    int16_t from(int16_t lo, int16_t hi) { return static_cast<int16_t>(lo + below(static_cast<int16_t>(hi - lo + 1))); }

private:
    uint32_t s_;
};

/* Question `item` of a numbered level, 0..poolSize()-1, in a fixed order
 * that the deck then deals at random. False for an item out of range or a
 * level that is not numbered. */
inline bool makeNumbered(uint8_t n, uint16_t item, Question& q) {
    if (n < 1 || n > LEVEL_COUNT || item >= poolSize(n)) return false;
    int16_t i = static_cast<int16_t>(item);
    switch (n) {
        case 1:
            for (int16_t a = 1; a < WITHIN; ++a) {
                const int16_t count = static_cast<int16_t>(WITHIN - a);
                if (i < count) {
                    set(q, Form::Sum, a, static_cast<int16_t>(1 + i));
                    return true;
                }
                i = static_cast<int16_t>(i - count);
            }
            return false;
        case 2:
            for (int16_t a = 1; a <= WITHIN; ++a) {
                if (i < a) {
                    set(q, Form::Difference, a, static_cast<int16_t>(1 + i));
                    return true;
                }
                i = static_cast<int16_t>(i - a);
            }
            return false;
        case 3: {
            const int16_t form = static_cast<int16_t>(i / bondsPerForm());
            i = static_cast<int16_t>(i % bondsPerForm());
            for (int16_t total = BOND_LOW; total <= BOND_HIGH; ++total) {
                if (i <= total) {
                    const int16_t part = i;     // the part that is shown
                    const int16_t gap = static_cast<int16_t>(total - part);
                    if (form == 0) set(q, Form::MissingSecond, part, gap);       // 4 + ? = 10
                    else if (form == 1) set(q, Form::MissingFirst, gap, part);   // ? + 4 = 10
                    else set(q, Form::MissingTaken, total, gap);                 // 10 - ? = 4
                    return true;
                }
                i = static_cast<int16_t>(i - (total + 1));
            }
            return false;
        }
        case 4:
            if (i < static_cast<int16_t>(crossPairCount())) {
                for (int16_t a = CROSS_LOW; a <= 9; ++a) {
                    const int16_t count = static_cast<int16_t>(a - 1);
                    if (i < count) {
                        set(q, Form::Sum, a, static_cast<int16_t>(11 - a + i));
                        return true;
                    }
                    i = static_cast<int16_t>(i - count);
                }
                return false;
            }
            i = static_cast<int16_t>(i - crossPairCount());
            if (i < TEENS) set(q, Form::Sum, 10, static_cast<int16_t>(1 + i));
            else set(q, Form::Sum, static_cast<int16_t>(1 + i - TEENS), 10);
            return true;
        case 5:
            set(q, Form::Difference, static_cast<int16_t>(11 + i / 9), static_cast<int16_t>(1 + i % 9));
            return true;
        default:
            return false;
    }
}

/* A question of any level from one random number. On a numbered level it is
 * an item at random -- the game deals those from the deck instead -- and
 * above them each level is built digit by digit so that what the level is
 * about (a carry, a borrow, a zero to borrow across) is always there rather
 * than hoped for. */
inline void makeDrawn(uint8_t n, uint32_t rnd, Question& q) {
    if (n < 1) n = 1;
    if (n > LEVEL_COUNT) n = LEVEL_COUNT;
    if (numbered(n)) {
        makeNumbered(n, static_cast<uint16_t>(rnd % poolSize(n)), q);
        return;
    }
    Dice d(rnd);
    switch (n) {
        case 6: {
            /* No column regroups: tens and tens, or a two-digit number and
             * some ones that stay inside its ten. */
            switch (d.below(4)) {
                case 0: {
                    const int16_t x = d.from(1, 8);
                    const int16_t y = d.from(1, static_cast<int16_t>(9 - x));
                    set(q, Form::Sum, static_cast<int16_t>(x * 10), static_cast<int16_t>(y * 10));
                    break;
                }
                case 1: {
                    const int16_t x = d.from(2, 9);
                    const int16_t y = d.from(1, static_cast<int16_t>(x - 1));
                    set(q, Form::Difference, static_cast<int16_t>(x * 10), static_cast<int16_t>(y * 10));
                    break;
                }
                case 2: {
                    const int16_t t = d.from(1, 9);
                    const int16_t u = d.from(0, 8);
                    const int16_t v = d.from(1, static_cast<int16_t>(9 - u));
                    set(q, Form::Sum, static_cast<int16_t>(t * 10 + u), v);
                    break;
                }
                default: {
                    const int16_t t = d.from(1, 9);
                    const int16_t u = d.from(1, 9);
                    const int16_t v = d.from(1, u);
                    set(q, Form::Difference, static_cast<int16_t>(t * 10 + u), v);
                    break;
                }
            }
            return;
        }
        case 7: {
            /* Ones that cross into the next ten, both ways, inside 100. The
             * teens minus ones were level 5, so take-aways start at 20. */
            if (d.below(2) == 0) {
                const int16_t t = d.from(1, 8);
                const int16_t u = d.from(1, 9);
                const int16_t v = d.from(static_cast<int16_t>(10 - u), 9);
                set(q, Form::Sum, static_cast<int16_t>(t * 10 + u), v);
            } else {
                const int16_t t = d.from(2, 9);
                const int16_t u = d.from(0, 8);
                const int16_t v = d.from(static_cast<int16_t>(u + 1), 9);
                set(q, Form::Difference, static_cast<int16_t>(t * 10 + u), v);
            }
            return;
        }
        case 8: {
            if (d.below(2) == 0) {
                const int16_t ta = d.from(1, 8);
                const int16_t tb = d.from(1, static_cast<int16_t>(9 - ta));
                const int16_t ua = d.from(0, 9);
                const int16_t ub = d.from(0, static_cast<int16_t>(9 - ua));
                set(q, Form::Sum, static_cast<int16_t>(ta * 10 + ua), static_cast<int16_t>(tb * 10 + ub));
            } else {
                const int16_t ta = d.from(2, 9);
                const int16_t tb = d.from(1, static_cast<int16_t>(ta - 1));
                const int16_t ua = d.from(0, 9);
                const int16_t ub = d.from(0, ua);
                set(q, Form::Difference, static_cast<int16_t>(ta * 10 + ua), static_cast<int16_t>(tb * 10 + ub));
            }
            return;
        }
        case 9: {
            /* The ones always carry; the tens may carry too, which is the
             * first time an answer reaches three digits. */
            const int16_t ua = d.from(1, 9);
            const int16_t ub = d.from(static_cast<int16_t>(10 - ua), 9);
            const int16_t ta = d.from(1, 9);
            const int16_t tb = d.from(1, 9);
            set(q, Form::Sum, static_cast<int16_t>(ta * 10 + ua), static_cast<int16_t>(tb * 10 + ub));
            return;
        }
        case 10: {
            const int16_t ta = d.from(2, 9);
            const int16_t tb = d.from(1, static_cast<int16_t>(ta - 1));
            const int16_t ua = d.from(0, 8);
            const int16_t ub = d.from(static_cast<int16_t>(ua + 1), 9);
            set(q, Form::Difference, static_cast<int16_t>(ta * 10 + ua), static_cast<int16_t>(tb * 10 + ub));
            return;
        }
        case 11: {
            /* A three-digit number and a two- or three-digit one, with the
             * ones regrouping every time. One question in three, half the
             * take-aways, borrows across a zero in the tens (304 - 148),
             * which is where the method breaks for a player who has only
             * learned it by rote. */
            const int16_t kind = d.below(3);
            const int16_t ha = d.from(kind == 2 ? 2 : 1, 9);
            const int16_t ta = kind == 2 ? 0 : d.from(0, 9);
            if (kind == 0) {
                const int16_t ua = d.from(1, 9);
                const int16_t ub = d.from(static_cast<int16_t>(10 - ua), 9);
                const int16_t hb = d.from(0, 9);
                const int16_t tb = d.from(hb == 0 ? 1 : 0, 9);
                set(q, Form::Sum, static_cast<int16_t>(ha * 100 + ta * 10 + ua),
                    static_cast<int16_t>(hb * 100 + tb * 10 + ub));
            } else {
                const int16_t ua = d.from(0, 8);
                const int16_t ub = d.from(static_cast<int16_t>(ua + 1), 9);
                const int16_t hb = d.from(kind == 2 ? 1 : 0, static_cast<int16_t>(ha - 1));
                const int16_t tb = d.from(hb == 0 ? 1 : 0, 9);
                set(q, Form::Difference, static_cast<int16_t>(ha * 100 + ta * 10 + ua),
                    static_cast<int16_t>(hb * 100 + tb * 10 + ub));
            }
            return;
        }
        default: {
            /* Two-digit numbers, the missing one in any of the places. Each
             * shown number is two digits, so the question is about which
             * operation finds the gap, not about the size of the sum. */
            const int16_t form = d.below(4);
            if (form < 2) {
                const int16_t a = d.from(10, 89);
                const int16_t b = d.from(10, static_cast<int16_t>(99 - a));
                set(q, form == 0 ? Form::MissingFirst : Form::MissingSecond, a, b);
            } else {
                const int16_t a = d.from(20, 99);
                const int16_t b = d.from(10, static_cast<int16_t>(a - 10));
                set(q, form == 2 ? Form::MissingTaken : Form::MissingStart, a, b);
            }
            return;
        }
    }
}

/* One number for "has this question come up lately", for RecentQuestions on
 * the levels too large to number. Ten bits are enough for 999. */
inline uint32_t token(uint8_t n, const Question& q) {
    return (static_cast<uint32_t>(n) << 24) | (static_cast<uint32_t>(q.form) << 20) |
           (static_cast<uint32_t>(q.a & 0x3FF) << 10) | static_cast<uint32_t>(q.b & 0x3FF);
}
static_assert(MAX_OPERAND < 1024, "an operand no longer fits ten bits of the token");

/* ---- the mistakes -------------------------------------------------------- */

/* Each column added on its own and the carry never taken: 47 + 8 written as
 * 45, 78 + 45 as 113 -- the top column is written in full, because that is
 * what a player who has forgotten to carry writes there. */
inline int16_t forgotCarry(int16_t a, int16_t b) {
    const int16_t top = static_cast<int16_t>(digitsOf(a > b ? a : b) - 1);
    int32_t out = 0;
    int32_t place = 1;
    for (int16_t k = 0; k <= top; ++k) {
        const int32_t sum = (a / place) % 10 + (b / place) % 10;
        out += (k == top ? sum : sum % 10) * place;
        place *= 10;
    }
    return static_cast<int16_t>(out);
}

/* In every column, the smaller digit taken from the larger: 52 - 7 as 55,
 * 13 - 5 as 12. The commonest wrong answer in subtraction, and the one that
 * shows the player has not borrowed. */
inline int16_t smallerFromLarger(int16_t a, int16_t b) {
    int32_t out = 0;
    int32_t place = 1;
    while (place <= a) {
        const int32_t da = (a / place) % 10;
        const int32_t db = (b / place) % 10;
        out += (da > db ? da - db : db - da) * place;
        place *= 10;
    }
    return static_cast<int16_t>(out);
}

/* The two numbers written side by side: 10 + 5 as 105. */
inline int16_t joined(int16_t a, int16_t b) {
    int32_t place = 1;
    for (int16_t k = 0; k < digitsOf(b); ++k) place *= 10;
    const int32_t out = a * place + b;
    return out > MAX_SHOWN ? static_cast<int16_t>(-1) : static_cast<int16_t>(out);
}

inline int16_t apart(int16_t a, int16_t b) { return static_cast<int16_t>(a > b ? a - b : b - a); }

/* The likely wrong answers for one question. `sure` are kept whenever they
 * are a legal option -- the mistake the level exists to catch -- and the
 * rest are shuffled and taken as room allows. */
struct Mistakes {
    static constexpr uint8_t CAPACITY = 8;
    int16_t value[CAPACITY] = {};
    uint8_t count = 0;
    uint8_t sure = 0;
    void must(int16_t v) {
        if (count < CAPACITY) {
            value[count++] = v;
            sure = count;
        }
    }
    void maybe(int16_t v) {
        if (count < CAPACITY) value[count++] = v;
    }
};

inline void likelyMistakes(uint8_t n, const Question& q, Dice& d, Mistakes& m) {
    const int16_t ans = q.answer;
    const int16_t a = q.a;
    const int16_t b = q.b;
    const int16_t c = q.c;
    const bool add = adds(q.form);
    /* From level 6 one option always shares the answer's last digit, so
     * working out the units alone no longer finds the button. Ten below an
     * answer of 10 is nothing, so then it is ten above. */
    const int16_t ten = (d.below(2) == 0 || ans - 10 < 1) ? 10 : -10;
    switch (n) {
        case 1:
            m.maybe(static_cast<int16_t>(ans + 1));     // counted the first number again
            m.maybe(static_cast<int16_t>(ans - 1));
            m.maybe(apart(a, b));                       // took away instead
            m.maybe(static_cast<int16_t>(ans + 2));
            m.maybe(static_cast<int16_t>(ans - 2));
            break;
        case 2:
            m.maybe(static_cast<int16_t>(ans + 1));
            m.maybe(static_cast<int16_t>(ans - 1));
            m.maybe(static_cast<int16_t>(a + b));       // added instead
            m.maybe(static_cast<int16_t>(ans + 2));
            m.maybe(static_cast<int16_t>(ans - 2));
            break;
        case 3: {
            /* 4 + ? = 10 answered 14: doing the sign that is shown is THE
             * mistake of a missing number, so it is always there. */
            const int16_t whole = q.form == Form::MissingTaken ? a : c;
            const int16_t shown = q.form == Form::MissingTaken ? c : (q.form == Form::MissingFirst ? b : a);
            m.must(static_cast<int16_t>(whole + shown));
            m.maybe(static_cast<int16_t>(ans + 1));
            m.maybe(static_cast<int16_t>(ans - 1));
            m.maybe(shown);                             // copied the part it was given
            m.maybe(whole);
            break;
        }
        case 4:
            if (a == 10 || b == 10) {
                m.must(a == 10 ? b : a);                // forgot the ten
                m.maybe(joined(a, b));                  // 10 + 5 written 105
            } else {
                m.must(static_cast<int16_t>(ans - 10)); // wrote the ones and lost the ten
                m.maybe(apart(a, b));
            }
            m.maybe(static_cast<int16_t>(ans + 1));
            m.maybe(static_cast<int16_t>(ans - 1));
            break;
        case 5:
            m.must(smallerFromLarger(a, b));            // 13 - 5 as 12
            m.maybe(static_cast<int16_t>(ans + 1));
            m.maybe(static_cast<int16_t>(ans - 1));
            m.maybe(static_cast<int16_t>(a + b));
            m.maybe(static_cast<int16_t>(ans - 10));
            break;
        case 6:
        case 8:
            m.must(static_cast<int16_t>(ans + ten));    // a slip into the wrong column
            m.maybe(static_cast<int16_t>(ans - ten));
            m.maybe(static_cast<int16_t>(ans + 1));
            m.maybe(static_cast<int16_t>(ans - 1));
            m.maybe(add ? apart(a, b) : static_cast<int16_t>(a + b));
            if (n == 6 && add && b < 10) m.maybe(static_cast<int16_t>(a + 10 * b));  // 42 + 5 as 92
            if (n == 6 && a % 10 == 0 && b % 10 == 0) m.maybe(static_cast<int16_t>(ans / 10));  // 30 + 40 as 7
            break;
        case 7:
        case 9:
        case 10:
        case 11:
            if (add) {
                m.must(forgotCarry(a, b));
                m.maybe(static_cast<int16_t>(ans + 10));
                m.maybe(static_cast<int16_t>(ans + 1));
                m.maybe(static_cast<int16_t>(ans - 1));
                if (n == 11) {
                    m.maybe(static_cast<int16_t>(ans - 10));
                    m.maybe(static_cast<int16_t>(ans + 100));
                    m.maybe(static_cast<int16_t>(ans - 100));
                } else {
                    m.maybe(apart(a, b));
                }
            } else {
                m.must(smallerFromLarger(a, b));
                m.must(static_cast<int16_t>(ans + 10));  // borrowed, and never paid the ten back
                m.maybe(static_cast<int16_t>(ans + 1));
                m.maybe(static_cast<int16_t>(ans - 1));
                m.maybe(static_cast<int16_t>(ans - 10));
                if (n == 11) m.maybe(static_cast<int16_t>(ans + 100));
                else m.maybe(static_cast<int16_t>(a + b));
            }
            break;
        default: {
            /* ? + 38 = 75 answered 113, ? - 27 = 45 answered 18. */
            int16_t shownSign = 0;
            switch (q.form) {
                case Form::MissingFirst: shownSign = static_cast<int16_t>(c + b); break;
                case Form::MissingSecond: shownSign = static_cast<int16_t>(c + a); break;
                case Form::MissingTaken: shownSign = static_cast<int16_t>(a + c); break;
                case Form::MissingStart: shownSign = static_cast<int16_t>(c - b); break;
                default: shownSign = add ? apart(a, b) : static_cast<int16_t>(a + b); break;
            }
            /* Ten out goes the same way as the sign mistake two times in
             * three. Taken by a coin toss, the two sat one either side of
             * the answer half the time, and then the answer could only be
             * one of the middle two buttons. */
            int16_t tenWay = (shownSign > ans) == (d.below(3) != 0) ? 10 : -10;
            if (ans - 10 < 1) tenWay = 10;
            m.must(shownSign);
            m.must(static_cast<int16_t>(ans + tenWay));
            m.maybe(static_cast<int16_t>(ans - tenWay));
            m.maybe(static_cast<int16_t>(ans + 1));
            m.maybe(static_cast<int16_t>(ans - 1));
            break;
        }
    }
}

/* Four different values, exactly one of them right, the right one on a
 * button chosen at random. Every equation here has one solution, so a wrong
 * value is wrong however it is argued; what is left to rule out is a value
 * the screen cannot show, a negative, and a 0 on a level where 0 is never
 * the answer -- which would be a button nobody needs to think about.
 *
 * How many of the three wrong values sit below the answer is chosen at
 * random before the likely mistakes are taken to fit it. Taken as they came,
 * the mistakes fell mostly on one side: "one more, one less, two more, two
 * less, took away instead" has three below and two above, so on level 1 the
 * answer was never the smallest button, and on level 4 it was the second
 * largest three times in four. A player who noticed could strike out the
 * end buttons and toss a coin, with no sum done at all -- the same thing
 * MultiplicationCourse.h found and answered the same way. */
inline void makeOptions(uint8_t n, Question& q, uint32_t rnd) {
    Dice d(rnd);
    Mistakes m;
    likelyMistakes(n, q, d, m);
    const int16_t lowest = level(n).floor;

    int16_t wrong[3] = {};
    uint8_t got = 0;
    uint8_t below = 0;
    auto offer = [&](int16_t v) -> bool {
        if (got >= 3 || v < lowest || v > MAX_SHOWN || v == q.answer) return false;
        for (uint8_t k = 0; k < got; ++k) {
            if (wrong[k] == v) return false;
        }
        wrong[got++] = v;
        if (v < q.answer) ++below;
        return true;
    };

    /* The level's own mistake first, wherever it falls; it fixes part of the
     * split, and the rest is chosen among what is still possible. */
    for (uint8_t i = 0; i < m.sure; ++i) offer(m.value[i]);
    const int16_t room = static_cast<int16_t>(q.answer - lowest);   // values below the answer
    int16_t most = static_cast<int16_t>(below + (3 - got));
    if (most > room) most = room;
    const int16_t wantBelow =
        most <= below ? static_cast<int16_t>(below)
                      : static_cast<int16_t>(below + d.below(static_cast<int16_t>(most - below + 1)));
    int16_t needBelow = static_cast<int16_t>(wantBelow - below);
    int16_t needAbove = static_cast<int16_t>(3 - got - needBelow);

    for (uint8_t i = m.count; i > m.sure + 1; --i) {
        const uint8_t j = static_cast<uint8_t>(m.sure + d.below(static_cast<int16_t>(i - m.sure)));
        const int16_t t = m.value[i - 1];
        m.value[i - 1] = m.value[j];
        m.value[j] = t;
    }
    for (uint8_t i = m.sure; i < m.count; ++i) {
        const int16_t v = m.value[i];
        if (v < q.answer ? needBelow <= 0 : needAbove <= 0) continue;
        if (offer(v)) {
            if (v < q.answer) --needBelow;
            else --needAbove;
        }
    }

    /* Neighbours for whatever each side still lacks. There is room below for
     * every one asked of it (`most` saw to that), and upward always has room:
     * the answer is at most 1998, far under MAX_SHOWN. */
    for (int16_t step = 1; (needBelow > 0 || needAbove > 0) && step <= 2 * MAX_OPERAND; ++step) {
        if (needBelow > 0 && offer(static_cast<int16_t>(q.answer - step))) --needBelow;
        if (needAbove > 0 && offer(static_cast<int16_t>(q.answer + step))) --needAbove;
    }

    for (uint8_t i = 2; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(d.below(static_cast<int16_t>(i + 1)));
        const int16_t t = wrong[i];
        wrong[i] = wrong[j];
        wrong[j] = t;
    }
    q.correct = static_cast<uint8_t>(d.below(4));
    uint8_t w = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        q.options[i] = i == q.correct ? q.answer : wrong[w++];
    }
}

/* ---- how it is written --------------------------------------------------- */

inline void equation(const Question& q, char* out, size_t len) {
    const int a = q.a;
    const int b = q.b;
    const int c = q.c;
    switch (q.form) {
        case Form::Sum: snprintf(out, len, "%d + %d = ?", a, b); break;
        case Form::Difference: snprintf(out, len, "%d - %d = ?", a, b); break;
        case Form::MissingSecond: snprintf(out, len, "%d + ? = %d", a, c); break;
        case Form::MissingFirst: snprintf(out, len, "? + %d = %d", b, c); break;
        case Form::MissingTaken: snprintf(out, len, "%d - ? = %d", a, c); break;
        default: snprintf(out, len, "? - %d = %d", b, c); break;
    }
}

/* Names and things to count.
 *
 * Short, because the whole problem has to wrap into three lines of font 2, and
 * from as many places as a classroom has young players in it. No pronouns anywhere
 * in the templates: "Ava has 7 shells and finds 5 more" needs none, and a
 * pronoun would be a guess about a made-up young player that the sentence does not
 * need to make. */
inline constexpr const char* NAMES[] = {
    "Ava", "Ben", "Chen", "Dara", "Eli", "Farah", "Gus", "Hana",
    "Ines", "Jai", "Kofi", "Lena", "Mo", "Nia", "Omar", "Priya",
    "Rosa", "Sam", "Tess", "Uma", "Wren", "Yusuf", "Zara", "Iris",
};
constexpr uint8_t NAME_COUNT = sizeof(NAMES) / sizeof(NAMES[0]);

inline constexpr const char* THINGS[] = {
    "apples", "marbles", "stickers", "pencils", "shells", "buttons",
    "grapes", "blocks", "coins", "leaves", "beads", "crayons",
    "acorns", "stamps", "cherries", "pebbles",
};
constexpr uint8_t THING_COUNT = sizeof(THINGS) / sizeof(THINGS[0]);
constexpr uint8_t STORY_SHAPES = 3;    // for each of adding and taking away

/* 128 holds the longest template with two four-digit numbers, two of the
 * longest names and the longest thing; the host test writes every one. */
constexpr size_t STORY_MAX = 128;

/* Which questions may be told as a story. Only a sum or a take-away, never
 * a missing number, and only with two or more of each thing: "1 more go in"
 * and "1 are taken away" were on the screen before, and the comparison
 * "Ava has 7. Ben has 7. How many more has Ava?" asked for "none" as a
 * number. */
inline bool storyFits(uint8_t n, const Question& q) {
    if (!level(n).stories) return false;
    if (q.form != Form::Sum && q.form != Form::Difference) return false;
    return q.a >= 2 && q.b >= 2 && q.c >= 1;
}

/* The sum already exists by the time this runs. A word problem is a way of
 * ASKING the question, not a different question, which is what keeps the
 * four answer buttons and the whole scoring path identical. */
inline void tellStory(const Question& q, uint8_t shape, uint8_t who, uint8_t other, uint8_t thing,
                      char* out, size_t len) {
    const char* name = NAMES[who % NAME_COUNT];
    /* Two names, and they must differ or the question reads as nonsense. */
    uint8_t second = static_cast<uint8_t>(other % NAME_COUNT);
    if (second == who % NAME_COUNT) second = static_cast<uint8_t>((second + 1) % NAME_COUNT);
    const char* things = THINGS[thing % THING_COUNT];
    const unsigned a = static_cast<unsigned>(q.a);
    const unsigned b = static_cast<unsigned>(q.b);

    if (adds(q.form)) {
        switch (shape % STORY_SHAPES) {
            case 0:
                snprintf(out, len, "%s has %u %s and finds %u more. How many now?", name, a, things, b);
                break;
            case 1:
                snprintf(out, len, "There are %u %s in a box. %u more go in. How many in all?", a, things, b);
                break;
            default:
                snprintf(out, len, "%s picks %u %s on Monday and %u on Tuesday. How many?", name, a, things, b);
                break;
        }
        return;
    }
    switch (shape % STORY_SHAPES) {
        case 0:
            snprintf(out, len, "%s has %u %s and gives %u away. How many are left?", name, a, things, b);
            break;
        case 1:
            snprintf(out, len, "%u %s are on the table. %u are taken away. How many stay?", a, things, b);
            break;
        default:
            snprintf(out, len, "%s has %u %s. %s has %u. How many more has %s?", name, a, things,
                     NAMES[second], b, name);
            break;
    }
}

inline void tellStory(const Question& q, uint32_t rnd, char* out, size_t len) {
    Dice d(rnd);
    const uint8_t shape = static_cast<uint8_t>(d.below(STORY_SHAPES));
    const uint8_t who = static_cast<uint8_t>(d.below(NAME_COUNT));
    const uint8_t other = static_cast<uint8_t>(d.below(NAME_COUNT));
    const uint8_t thing = static_cast<uint8_t>(d.below(THING_COUNT));
    tellStory(q, shape, who, other, thing, out, len);
}

}   // namespace MathCourse
