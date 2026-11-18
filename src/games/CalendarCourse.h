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

/* WHAT CALENDAR TEACHES, LEVEL BY LEVEL.
 *
 * The game asked the day or month after or before a name: twelve questions
 * about days and twenty-two about months, with no levels and no repeat
 * guard. It never asked across the join -- after Sunday, after December --
 * though the join is what makes the week and the year cycles, and its wrong
 * tiles were names drawn at random, so most questions could be answered by
 * striking out the ones that were obviously far away.
 *
 * Two tracks, each with its own level: the Days button is the week, the
 * Months button the year. Days: the next day, then round the week, then two
 * or three days on, then a week and more. Months: the next month, round the
 * year, the months' numbers, how many days a month has, then several
 * months on. Seasons are left out on purpose: which months are summer
 * depends on where the player lives.
 *
 * The wrong tiles are the mistakes of each question: the neighbour on the
 * other side (before given for after), the starting day counted as the
 * first, the name the question started from, one month out on a number.
 *
 * Pure C++, no Arduino and no heap: test/host/calendar_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace CalendarCourse {

constexpr uint8_t DAYS = 0;
constexpr uint8_t MONTHS = 1;
constexpr uint8_t TRACKS = 2;

constexpr const char* DAY_NAMES[7] = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
constexpr const char* MONTH_NAMES[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"};
/* Days in each month of an ordinary year. */
constexpr uint8_t MONTH_DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/* What a tile shows: a day's name, a month's name, or a number. */
enum class Label : uint8_t { Day, Month, Number };

struct Level {
    const char* name;
    uint8_t pool;
};

constexpr uint8_t DAY_LEVELS = 4;
constexpr uint8_t MONTH_LEVELS = 5;

/* Pools: days 12 + 14 + 28 + 98, months 22 + 24 + 24 + 19 + 240. The test
 * checks each is exactly the number of different questions. */
constexpr Level LEVELS_DAYS[DAY_LEVELS] = {
    {"Next day", 12},           // after Monday..Saturday, before Tuesday..Sunday
    {"Round the week", 14},     // and across the join: after Sunday
    {"Two or three days", 28},  // 2 days after Friday
    {"Weeks on", 98},           // 10 days after Wednesday
};
constexpr Level LEVELS_MONTHS[MONTH_LEVELS] = {
    {"Next month", 22},
    {"Round the year", 24},
    {"Month numbers", 24},      // which month is number 9
    {"How many days", 19},      // days in April, days in a year
    {"Months on", 240},         // 5 months after October
};

inline uint8_t levelCount(uint8_t track) { return track == DAYS ? DAY_LEVELS : MONTH_LEVELS; }

inline uint8_t clampLevel(uint8_t track, uint8_t level) {
    const uint8_t n = levelCount(track);
    return level == 0 ? 1 : (level > n ? n : level);
}

inline const Level& levelOf(uint8_t track, uint8_t level) {
    level = clampLevel(track, level);
    return track == DAYS ? LEVELS_DAYS[level - 1] : LEVELS_MONTHS[level - 1];
}

inline uint16_t poolSize(uint8_t track, uint8_t level) { return levelOf(track, level).pool; }
inline const char* levelName(uint8_t track, uint8_t level) { return levelOf(track, level).name; }

/* "Level 2: Round the week". */
constexpr uint8_t LEVEL_LINE_MAX = 28;
inline int levelLine(uint8_t track, uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(track, level)),
                    levelName(track, level));
}

/* "How many months have" is the longest prompt and "February, leap year"
 * the longest subject. */
constexpr uint8_t PROMPT_MAX = 24;
constexpr uint8_t SUBJECT_MAX = 24;

struct Question {
    uint8_t track = DAYS;
    uint8_t level = 1;
    char prompt[PROMPT_MAX + 1] = {};
    char subject[SUBJECT_MAX + 1] = {};
    Label label = Label::Day;
    int16_t answer = 0;
    int16_t options[4] = {};
    uint8_t correct = 0;
};

/* One tile's text. */
inline int format(Label label, int16_t v, char* out, size_t len) {
    switch (label) {
        case Label::Day: return snprintf(out, len, "%s", DAY_NAMES[((v % 7) + 7) % 7]);
        case Label::Month: return snprintf(out, len, "%s", MONTH_NAMES[((v % 12) + 12) % 12]);
        case Label::Number:
        default: return snprintf(out, len, "%d", v);
    }
}

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

inline int wrap(int v, int n) { return ((v % n) + n) % n; }

struct Options {
    int16_t v[4] = {};
    uint8_t n = 0;
    int cycle = 0;      // 7 or 12 for names; 0 for numbers
    int lo = 0;
    int hi = 999;

    bool add(int value) {
        if (n >= 4) return false;
        if (cycle > 0) {
            value = wrap(value, cycle);
        } else if (value < lo || value > hi) {
            return false;
        }
        for (uint8_t i = 0; i < n; ++i) {
            if (v[i] == value) return false;
        }
        v[n++] = static_cast<int16_t>(value);
        return true;
    }
};

/* A count of `n` either way, said for the prompt. */
inline void stepPrompt(char* out, size_t len, int n, bool after, const char* unit) {
    if (n == 1) {
        snprintf(out, len, "What comes %s:", after ? "AFTER" : "BEFORE");
    } else {
        snprintf(out, len, "%d %s %s:", n, unit, after ? "AFTER" : "BEFORE");
    }
}

/* The six counting facts of "How many days", each with the three wrong
 * answers players give: 5 school days for a week, 52 weeks for twelve
 * months, 364 for the 52 weeks of a year, the other kind of month. */
struct Fact {
    const char* prompt;
    const char* subject;
    int16_t answer;
    int16_t wrong[3];
};
constexpr Fact FACTS[6] = {
    {"How many days in", "a week", 7, {5, 10, 6}},
    {"How many months in", "a year", 12, {10, 52, 7}},
    {"How many days in", "a year", 365, {366, 364, 360}},
    {"How many days in", "a leap year", 366, {365, 364, 367}},
    {"How many months have", "31 days", 7, {4, 6, 12}},
    {"How many months have", "30 days", 4, {7, 5, 1}},
};

}   // namespace detail

/* Question `item` of `level` on `track`, with its options; false for an item
 * outside the pool. The question depends only on the item, the order of the
 * tiles on `rnd`. */
inline bool make(uint8_t track, uint8_t level, uint16_t item, uint32_t rnd, Question& q) {
    track = track == DAYS ? DAYS : MONTHS;
    level = clampLevel(track, level);
    if (item >= poolSize(track, level)) return false;

    uint32_t s = rnd ^ 0x51ED270BUL;
    if (s == 0) s = 1;
    detail::stir(s);

    q = Question{};
    q.track = track;
    q.level = level;
    const int n = track == DAYS ? 7 : 12;
    detail::Options o;
    o.cycle = n;
    q.label = track == DAYS ? Label::Day : Label::Month;
    const char* unit = track == DAYS ? "days" : "months";

    int subject = 0;
    int step = 1;
    bool after = true;
    int key = 0;
    int more[3] = {};
    bool counting = false;      // the question is a step from a name

    if (track == DAYS) {
        switch (level) {
            case 1:
                after = item < 6;
                subject = after ? item : item - 5;
                counting = true;
                break;
            case 2:
                after = item < 7;
                subject = item % 7;
                counting = true;
                break;
            case 3: {
                static constexpr int8_t OFFSET[4] = {2, 3, -2, -3};
                const int off = OFFSET[item / 7];
                subject = item % 7;
                after = off > 0;
                step = off > 0 ? off : -off;
                counting = true;
                break;
            }
            default: {
                static constexpr uint8_t N[7] = {5, 6, 7, 8, 9, 10, 14};
                subject = item % 7;
                step = N[(item / 7) % 7];
                after = item < 49;
                counting = true;
                break;
            }
        }
    } else {
        switch (level) {
            case 1:
                after = item < 11;
                subject = after ? item : item - 10;
                counting = true;
                break;
            case 2:
                after = item < 12;
                subject = item % 12;
                counting = true;
                break;
            case 3:
                if (item < 12) {
                    /* What number is April: 4. One out either way, and
                     * counted from nought. */
                    snprintf(q.prompt, sizeof(q.prompt), "What number is");
                    snprintf(q.subject, sizeof(q.subject), "%s", MONTH_NAMES[item]);
                    q.label = Label::Number;
                    q.answer = static_cast<int16_t>(item + 1);
                    o.cycle = 0;
                    o.lo = 1;
                    o.hi = 12;
                    key = item;
                    more[0] = item + 2;
                    more[1] = item + 3;
                    more[2] = item - 1;
                } else {
                    const int m = item - 12;
                    snprintf(q.prompt, sizeof(q.prompt), "Which month is number");
                    snprintf(q.subject, sizeof(q.subject), "%d", m + 1);
                    q.answer = static_cast<int16_t>(m);
                    key = m + 1;            // counted from nought
                    more[0] = m - 1;
                    more[1] = m + 2;
                    more[2] = m - 2;
                }
                break;
            case 4:
                q.label = Label::Number;
                o.cycle = 0;
                o.lo = 1;
                if (item < 13) {
                    /* Every month's tiles are the same four, 28 to 31, so
                     * the question is which, not which is nearest. */
                    const int m = item < 12 ? item : 1;
                    const bool leap = item == 12;
                    snprintf(q.prompt, sizeof(q.prompt), "How many days in");
                    snprintf(q.subject, sizeof(q.subject), "%s",
                             leap ? "February, leap year" : MONTH_NAMES[m]);
                    q.answer = static_cast<int16_t>(leap ? 29 : MONTH_DAYS[m]);
                    key = q.answer == 31 ? 30 : (q.answer == 30 ? 31 : (leap ? 28 : 29));
                    int k = 0;
                    for (int d = 28; d <= 31; ++d) {
                        if (d != q.answer && d != key) more[k++] = d;
                    }
                } else {
                    const detail::Fact& f = detail::FACTS[item - 13];
                    snprintf(q.prompt, sizeof(q.prompt), "%s", f.prompt);
                    snprintf(q.subject, sizeof(q.subject), "%s", f.subject);
                    q.answer = f.answer;
                    key = f.wrong[0];
                    more[0] = f.wrong[1];
                    more[1] = f.wrong[2];
                    more[2] = f.wrong[1];
                }
                break;
            default:
                subject = item % 12;
                step = 2 + (item / 12) % 10;
                after = item < 120;
                counting = true;
                break;
        }
    }

    if (counting) {
        const int sign = after ? 1 : -1;
        detail::stepPrompt(q.prompt, sizeof(q.prompt), step, after, unit);
        snprintf(q.subject, sizeof(q.subject), "%s",
                 track == DAYS ? DAY_NAMES[subject] : MONTH_NAMES[subject]);
        q.answer = static_cast<int16_t>(detail::wrap(subject + sign * step, n));
        if (step == 1) {
            /* Before given for after; the name itself; one too far. */
            key = subject - sign;
            more[0] = subject;
            more[1] = subject + 2 * sign;
            more[2] = subject - 2 * sign;
        } else {
            /* The starting name counted as the first; the wrong way; one too
             * far. A whole week brings the same day back, and that is the
             * thing to learn, so the name itself stays a fair answer. */
            key = subject + sign * (step - 1);
            more[0] = subject - sign * step;
            more[1] = subject + sign * (step + 1);
            more[2] = subject;
        }
    }

    o.add(q.answer);
    o.add(key);
    for (uint8_t i = 3; i > 1; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % i);
        const int t = more[i - 1];
        more[i - 1] = more[j];
        more[j] = t;
    }
    for (uint8_t i = 0; i < 3 && o.n < 4; ++i) o.add(more[i]);
    const bool upFirst = (detail::stir(s) & 1U) != 0;
    for (int k = 1; o.n < 4 && k < 400; ++k) {
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
    return true;
}

}   // namespace CalendarCourse
