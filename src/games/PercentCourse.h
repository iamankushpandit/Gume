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

/* WHAT PERCENT TEACHES, LEVEL BY LEVEL.
 *
 * The game marked four wrong answers right -- 25% of 10 was "2", 75% of 50
 * "37" -- because it truncated. Its level followed the streak, so one wrong
 * tap dropped a player to level 1; levels 4 and 5 were level 3 again; the
 * kind of question followed the score in a fixed rotation; most rounds showed
 * the same wrong percent on two buttons; and the shading round printed the
 * shaded amount as a number, so the circle was never read.
 *
 * This course reads and shades a circle in quarters, tens and fives; turns
 * fractions into percents; finds 50% and 10% of a number, then 25% and 75%,
 * then any ten percent, then the fives; asks what percent one number is of
 * another; and adds or takes a percent off. Every answer is a whole number
 * -- the test checks it -- so nothing is ever rounded into being right.
 *
 * The wrong answers are the mistakes the question invites: the top number of
 * a fraction taken as the percent (3/4 as 3%), the part left over (75% of 80
 * given as 20), the percent itself given as the answer, the other direction
 * on "more and less", one tenth out.
 *
 * Pure C++, no Arduino and no heap: test/host/percent_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace PercentCourse {

enum class Mode : uint8_t {
    Read,       // a shaded circle: what percent?
    Shade,      // "Shade 35%": - and + then OK
    Fraction,   // "3/4 = ?": four percents
    Of,         // "30% of 60 = ?": four numbers
    WhatPercent,// "8 is what % of 40?": four percents
    MoreLess,   // "Take 25% off 80": four numbers
};

constexpr uint8_t LEVEL_COUNT = 10;

struct Level {
    const char* name;
    uint8_t pool;
};

/* Pools: 8, 20, 20, 17, 70, 50, 70, 48, 60, 64. */
constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Quarters of a circle", 8},
    {"Tens", 20},
    {"Fives", 20},
    {"Fractions as percents", 17},
    {"Half and a tenth", 70},
    {"A quarter and three", 50},
    {"Tens of percent", 70},
    {"Fives of percent", 48},
    {"What percent is it?", 60},
    {"More and less", 64},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline uint16_t poolSize(uint8_t level) { return LEVELS[clampLevel(level) - 1].pool; }
inline const char* levelName(uint8_t level) { return LEVELS[clampLevel(level) - 1].name; }

constexpr uint8_t LEVEL_LINE_MAX = 32;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* How far one press of - or + moves the shading. */
inline uint8_t shadeStep(uint8_t level) {
    level = clampLevel(level);
    return level == 1 ? 25 : (level == 2 ? 10 : 5);
}
/* From Fives on the shaded amount is not printed: the circle is what is
 * read. */
inline bool showShadedValue(uint8_t level) { return clampLevel(level) < 3; }

struct Question {
    uint8_t level = 1;
    Mode mode = Mode::Read;
    /* The percent the circle shows or the question names. */
    uint8_t percent = 0;
    /* Of and More and less: the number. What percent: the whole. */
    uint8_t base = 0;
    /* What percent: the part. Fraction: top and bottom. */
    uint8_t part = 0;
    uint8_t top = 0;
    uint8_t bottom = 0;
    /* More and less: true to add. */
    bool more = false;
    uint16_t answer = 0;
    uint16_t options[4] = {};
    uint8_t correct = 0;
    /* The options are percents, written with a %. */
    bool percents = false;
};

/* The question above the buttons. */
inline int prompt(const Question& q, char* out, size_t len) {
    switch (q.mode) {
        case Mode::Read: return snprintf(out, len, "What percent?");
        case Mode::Shade: return snprintf(out, len, "Shade %u%%", static_cast<unsigned>(q.percent));
        case Mode::Fraction:
            return snprintf(out, len, "%u/%u = ?", static_cast<unsigned>(q.top),
                            static_cast<unsigned>(q.bottom));
        case Mode::Of:
            return snprintf(out, len, "%u%% of %u = ?", static_cast<unsigned>(q.percent),
                            static_cast<unsigned>(q.base));
        case Mode::WhatPercent:
            return snprintf(out, len, "%u is what %% of %u?", static_cast<unsigned>(q.part),
                            static_cast<unsigned>(q.base));
        case Mode::MoreLess:
        default:
            return q.more ? snprintf(out, len, "Add %u%% to %u", static_cast<unsigned>(q.percent),
                                     static_cast<unsigned>(q.base))
                          : snprintf(out, len, "Take %u%% off %u", static_cast<unsigned>(q.percent),
                                     static_cast<unsigned>(q.base));
    }
}

/* One button's text. */
inline int optionText(const Question& q, uint16_t v, char* out, size_t len) {
    return q.percents ? snprintf(out, len, "%u%%", static_cast<unsigned>(v))
                      : snprintf(out, len, "%u", static_cast<unsigned>(v));
}

/* The percent the circle shows once a question is answered. */
inline uint8_t shownPercent(const Question& q) {
    if (q.mode == Mode::Fraction || q.mode == Mode::WhatPercent) return static_cast<uint8_t>(q.answer);
    return q.percent;
}

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

struct Options {
    uint16_t v[4] = {};
    uint8_t n = 0;
    uint16_t lo = 1;
    uint16_t hi = 400;
    uint8_t step = 1;

    bool add(int value) {
        if (n >= 4 || value < lo || value > hi || value % step != 0) return false;
        for (uint8_t i = 0; i < n; ++i) {
            if (v[i] == value) return false;
        }
        v[n++] = static_cast<uint16_t>(value);
        return true;
    }
};

inline void place(Question& q, Options& o, int key, const int* more, uint8_t m, uint32_t& s) {
    const int answer = q.answer;
    o.add(answer);
    o.add(key);
    int pick[6];
    for (uint8_t i = 0; i < m; ++i) pick[i] = more[i];
    for (uint8_t i = m; i > 1; --i) {
        const uint8_t j = static_cast<uint8_t>(stir(s) % i);
        const int t = pick[i - 1];
        pick[i - 1] = pick[j];
        pick[j] = t;
    }
    for (uint8_t i = 0; i < m && o.n < 4; ++i) o.add(pick[i]);
    const bool up = (stir(s) & 1U) != 0;
    for (int d = o.step; o.n < 4 && d < 400; d += o.step) {
        o.add(answer + (up ? d : -d));
        o.add(answer + (up ? -d : d));
    }
    for (uint8_t i = 3; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(stir(s) % (i + 1));
        const uint16_t t = o.v[i];
        o.v[i] = o.v[j];
        o.v[j] = t;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        q.options[i] = o.v[i];
        if (o.v[i] == q.answer) q.correct = i;
    }
}

}   // namespace detail

/* Question `item` of `level`, options included. */
inline bool make(uint8_t level, uint16_t item, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    if (item >= poolSize(level)) return false;
    uint32_t s = rnd ^ 0x3243F6A9UL;
    if (s == 0) s = 1;
    detail::stir(s);
    q = Question{};
    q.level = level;
    detail::Options o;
    int key = 0;
    int more[5];
    uint8_t m = 0;

    switch (level) {
        case 1:
        case 2:
        case 3: {
            /* Read the circle, or shade it. The wrong readings: the empty
             * part, one step either side, a step on the wrong scale. */
            const uint16_t k = static_cast<uint16_t>(item / 2);
            q.percent = static_cast<uint8_t>(level == 1 ? 25 * (k + 1)
                                             : (level == 2 ? 10 * (k + 1) : 10 * k + 5));
            q.mode = (item & 1U) ? Mode::Shade : Mode::Read;
            q.answer = q.percent;
            if (q.mode == Mode::Shade) return true;
            q.percents = true;
            const uint8_t step = level == 1 ? 25 : (level == 2 ? 10 : 5);
            o.step = level == 1 ? 25 : 5;
            o.hi = 100;
            key = 100 - q.percent;
            more[m++] = q.percent + step;
            more[m++] = q.percent - step;
            more[m++] = q.percent + 2 * step;
            break;
        }
        case 4: {
            /* 1/2; 1/4, 3/4; the fifths; the tenths; 1/20. */
            static constexpr uint8_t FACTS[17][2] = {
                {1, 2}, {1, 4}, {3, 4}, {1, 5}, {2, 5}, {3, 5}, {4, 5}, {1, 10}, {2, 10},
                {3, 10}, {4, 10}, {5, 10}, {6, 10}, {7, 10}, {8, 10}, {9, 10}, {1, 20}};
            q.mode = Mode::Fraction;
            q.top = FACTS[item][0];
            q.bottom = FACTS[item][1];
            q.answer = static_cast<uint16_t>(100 * q.top / q.bottom);
            q.percents = true;
            o.hi = 100;
            /* The top number as the percent; the top times ten; one part's
             * worth; the rest. */
            key = q.top;
            more[m++] = 10 * q.top;
            more[m++] = 100 / q.bottom;
            more[m++] = 100 - q.answer;
            more[m++] = q.bottom;
            break;
        }
        case 5:
        case 6:
        case 7:
        case 8: {
            q.mode = Mode::Of;
            if (level == 5) {
                if (item < 50) {
                    q.percent = 50;
                    q.base = static_cast<uint8_t>(2 * (item + 1));
                } else {
                    q.percent = 10;
                    q.base = static_cast<uint8_t>(10 * (item - 50 + 1));
                }
            } else if (level == 6) {
                q.percent = item < 25 ? 25 : 75;
                q.base = static_cast<uint8_t>(4 * (item % 25 + 1));
            } else if (level == 7) {
                static constexpr uint8_t P[7] = {20, 30, 40, 60, 70, 80, 90};
                q.percent = P[item / 10];
                q.base = static_cast<uint8_t>(10 * (item % 10 + 1));
            } else {
                static constexpr uint8_t P[8] = {5, 15, 35, 45, 55, 65, 85, 95};
                static constexpr uint8_t B[6] = {20, 40, 60, 80, 100, 200};
                q.percent = P[item / 6];
                q.base = B[item % 6];
            }
            q.answer = static_cast<uint16_t>(q.base * q.percent / 100);
            /* The rest of the number; the percent itself; a tenth out. */
            key = q.base - q.answer;
            more[m++] = q.percent;
            more[m++] = q.answer + q.base / 10;
            more[m++] = q.answer - q.base / 10;
            if (level == 5) more[m++] = q.percent == 50 ? q.base / 10 : q.base / 2;
            if (level == 8) more[m++] = q.answer + q.base / 20;
            break;
        }
        case 9: {
            /* Wholes 10, 20, 25, 40, 50 and the parts that give a whole number
             * of fives. */
            static constexpr uint8_t W[5] = {10, 20, 25, 40, 50};
            uint16_t k = item;
            for (uint8_t i = 0; i < 5; ++i) {
                for (uint8_t p = 1; p < W[i]; ++p) {
                    if ((100 * p) % W[i] != 0 || ((100 * p) / W[i]) % 5 != 0) continue;
                    if (k-- == 0) {
                        q.base = W[i];
                        q.part = p;
                    }
                }
            }
            q.mode = Mode::WhatPercent;
            q.answer = static_cast<uint16_t>(100 * q.part / q.base);
            q.percents = true;
            o.hi = 100;
            /* The part itself as the percent; the rest; one part off. */
            key = q.part;
            more[m++] = 100 - q.answer;
            more[m++] = q.base - q.part;
            more[m++] = q.answer + 5;
            more[m++] = q.answer - 5;
            break;
        }
        case 10:
        default: {
            static constexpr uint8_t P[4] = {10, 20, 25, 50};
            q.mode = Mode::MoreLess;
            q.more = item < 32;
            const uint16_t k = static_cast<uint16_t>(item % 32);
            q.percent = P[k / 8];
            q.base = static_cast<uint8_t>(20 * (k % 8 + 1));
            const int delta = q.base * q.percent / 100;
            q.answer = static_cast<uint16_t>(q.more ? q.base + delta : q.base - delta);
            /* The other way; the percent added as a number; only the part. */
            key = q.more ? q.base - delta : q.base + delta;
            more[m++] = q.more ? q.base + q.percent : q.base - q.percent;
            more[m++] = delta;
            more[m++] = q.answer + 5;
            break;
        }
    }
    detail::place(q, o, key, more, m, s);
    return true;
}

}   // namespace PercentCourse
