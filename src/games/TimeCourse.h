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

/* WHAT TIME TEACHES, LEVEL BY LEVEL.
 *
 * The game asked "which time is shown?" at five levels worked out of the
 * visit's score, so every visit began at o'clock; and its wrong options were
 * noise round the answer: whole hours away on the first two levels, so the
 * minute hand was never tested, and one minute away on the last, which a dial
 * of radius 43 cannot show.
 *
 * This course reads an analogue clock: o'clock, half past, the quarters,
 * minutes past and minutes to in fives, any minute; then says the time in
 * words and in 24-hour time, and works with it -- later and earlier, and how
 * long until the hour. Every wrong option is a reading a player really
 * makes: the hands swapped, the hour hand read as the next number when it is
 * nearly there, the minute hand read as the numeral it points at (2:20 as
 * 2:04), "past" and "to" mixed up, 3 p.m. as 13:00, an hour not carried.
 *
 * A time is a number of minutes. On the clock it is 0..719 (0 is 12:00);
 * as a 24-hour time 0..1499 (an answer of 24:05 is a mistake worth
 * offering); as a span, minutes. Pure C++, no Arduino and no heap:
 * test/host/time_course_test.cpp compiles this file as it is. Randomness
 * comes in as a number. */
namespace TimeCourse {

constexpr uint8_t LEVEL_COUNT = 10;
constexpr uint16_t HALF_DAY = 12 * 60;

/* How an option is written on its button. */
enum class Label : uint8_t {
    Clock,      // 4:35
    Words,      // quarter to 5
    Day,        // 16:35, the 24-hour clock
    Minutes,    // 25 min
};

struct Level {
    const char* name;
    uint16_t pool;      // questions, numbered 0..pool-1; 0 = drawn at random
};

/* Pools: 12 + 24 + 48 + 72 + 60 + drawn + 144 + 144 + drawn + 132. The test
 * checks each one is exactly the number of different questions. */
constexpr Level LEVELS[LEVEL_COUNT] = {
    {"O'clock", 12},                // minute hand on 12
    {"Half past", 24},              // :00 and :30
    {"Quarter past and to", 48},    // :00 :15 :30 :45
    {"Minutes past", 72},           // :05 .. :30
    {"Minutes to", 60},             // :35 .. :55, hour hand near the next
    {"Any minute", 0},              // the 576 times not on a five
    {"Time in words", 144},         // quarter to 5, 20 past 3
    {"24-hour time", 144},          // p.m. and just after midnight
    {"Later and earlier", 0},       // 20 minutes later
    {"How long until", 132},        // minutes to the next o'clock
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

/* "Level 10: " and the longest name. */
constexpr uint8_t LEVEL_LINE_MAX = 10 + longestName();
/* "quarter past 12" is the longest button. */
constexpr uint8_t LABEL_MAX = 16;
/* "What time was it 15 minutes earlier?" is the longest question. */
constexpr uint8_t PROMPT_MAX = 40;

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}

inline uint16_t poolSize(uint8_t level) { return LEVELS[clampLevel(level) - 1].pool; }
inline bool numbered(uint8_t level) { return poolSize(level) > 0; }
inline const char* levelName(uint8_t level) { return LEVELS[clampLevel(level) - 1].name; }

inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* Later and earlier: the changes asked about, in minutes; negative is
 * earlier. Seven, so a start and a change are one question of 144 x 7. */
constexpr int16_t CHANGES[] = {10, 15, 20, 30, 45, -15, -30};
constexpr uint8_t CHANGE_COUNT = sizeof(CHANGES) / sizeof(CHANGES[0]);

struct Question {
    uint8_t level = 1;
    /* The time on the clock, 0..719. */
    uint16_t shown = 0;
    /* What is right, in the label's units. */
    uint16_t answer = 0;
    uint16_t options[4] = {};
    uint8_t correct = 0;
    Label label = Label::Clock;
    /* Sixty marks round the rim, for the levels that read any minute. */
    bool minuteMarks = false;
    /* 24-hour time: the clock is just after midnight rather than p.m. */
    bool afterMidnight = false;
    /* Later and earlier: minutes, negative for earlier. */
    int16_t change = 0;
};

/* 0..719 for any number of minutes, negative included. */
inline uint16_t onClock(int32_t minutes) {
    const int32_t m = minutes % HALF_DAY;
    return static_cast<uint16_t>(m < 0 ? m + HALF_DAY : m);
}

inline uint16_t at(int hour, int minute) { return onClock(hour * 60 + minute); }

/* The hour as a clock says it: 1..12. */
inline unsigned clockHour(uint16_t minutes) {
    const unsigned h = (minutes % HALF_DAY) / 60;
    return h == 0 ? 12 : h;
}

/* The two hands read the wrong way round: the hour from where the minute
 * hand points, the minutes from where the hour hand points. 4:00 read so is
 * 12:20. */
inline uint16_t swapped(uint16_t t) {
    const int h = t / 60;
    const int m = t % 60;
    const int minutes = ((300 * h + 5 * m + 30) / 60) % 60;
    return at(m / 5, minutes);
}

/* One option, written for its button. */
inline int format(Label label, uint16_t v, char* out, size_t len) {
    switch (label) {
        case Label::Words: {
            const uint16_t t = onClock(v);
            const unsigned m = t % 60;
            const unsigned h = clockHour(t);
            const unsigned next = clockHour(onClock(t + 60));
            if (m == 0) return snprintf(out, len, "%u o'clock", h);
            if (m == 15) return snprintf(out, len, "quarter past %u", h);
            if (m == 30) return snprintf(out, len, "half past %u", h);
            if (m == 45) return snprintf(out, len, "quarter to %u", next);
            if (m < 30) return snprintf(out, len, "%u past %u", m, h);
            return snprintf(out, len, "%u to %u", 60 - m, next);
        }
        case Label::Day:
            return snprintf(out, len, "%02u:%02u", static_cast<unsigned>(v / 60),
                            static_cast<unsigned>(v % 60));
        case Label::Minutes:
            return snprintf(out, len, "%u min", static_cast<unsigned>(v));
        case Label::Clock:
        default:
            return snprintf(out, len, "%u:%02u", clockHour(v), static_cast<unsigned>(v % 60));
    }
}

/* The line above the buttons. */
inline int prompt(const Question& q, char* out, size_t len) {
    switch (q.label) {
        case Label::Words:
            return snprintf(out, len, "Which words say this time?");
        case Label::Day:
            return snprintf(out, len, q.afterMidnight ? "Just after midnight. 24-hour time?"
                                                      : "It is p.m. Which 24-hour time?");
        case Label::Minutes:
            return snprintf(out, len, "How many minutes until %u o'clock?",
                            clockHour(onClock(q.shown + 60)));
        case Label::Clock:
        default:
            break;
    }
    if (q.change > 0) {
        return snprintf(out, len, "What time will it be %d minutes later?", q.change);
    }
    if (q.change < 0) {
        return snprintf(out, len, "What time was it %d minutes earlier?", -q.change);
    }
    return snprintf(out, len, "Which time is shown?");
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
    /* How close two clock options may be: three minutes where the dial has
     * to be read to the minute, so 4:37 is never set against 4:38. */
    uint8_t gap = 0;
    bool onDial = true;     // values wrap at 12 hours
    uint16_t low = 0;
    uint16_t limit = HALF_DAY;

    uint16_t distance(uint16_t a, uint16_t b) const {
        uint16_t d = a > b ? a - b : b - a;
        if (onDial && d > HALF_DAY / 2) d = static_cast<uint16_t>(HALF_DAY - d);
        return d;
    }

    bool add(int32_t value) {
        if (n >= 4) return false;
        if (onDial) value = onClock(value);
        if (value < low || value >= limit) return false;
        const uint16_t u = static_cast<uint16_t>(value);
        for (uint8_t i = 0; i < n; ++i) {
            if (v[i] == u) return false;
            if (gap > 0 && distance(v[i], u) < gap) return false;
        }
        v[n++] = u;
        return true;
    }
};

inline void shuffle(int32_t* a, uint8_t n, uint32_t& s) {
    for (uint8_t i = n; i > 1; --i) {
        const uint8_t j = static_cast<uint8_t>(stir(s) % i);
        const int32_t t = a[i - 1];
        a[i - 1] = a[j];
        a[j] = t;
    }
}

}   // namespace detail

/* The four options: the answer, the level's commonest mistake, two more of
 * its mistakes chosen at random, then -- only where those collide -- the
 * nearest times a step away. Placed at random. */
inline void makeOptions(Question& q, uint32_t rnd) {
    uint32_t s = rnd ^ 0x7F4A7C15UL;
    if (s == 0) s = 1;
    detail::stir(s);

    const uint16_t t = q.shown;
    const int h = t / 60;
    const int m = t % 60;
    detail::Options o;
    int32_t key = -1;
    int32_t more[6];
    uint8_t moreCount = 0;
    int16_t step = 5;
    auto also = [&](int32_t v) {
        if (moreCount < 6) more[moreCount++] = v;
    };

    switch (q.level) {
        case 1:
            key = swapped(t);
            also(at(h + 1, 0));
            also(at(h - 1, 0));
            also(at(h + 2, 0));
            also(at(h - 2, 0));
            step = 60;
            break;
        case 2:
        case 3:
            if (m == 30) {
                key = at(h + 1, 30);        // the hour hand is past the hour
                also(at(h, 0));
                also(at(h + 1, 0));
                also(at(h - 1, 30));
            } else if (m == 45) {
                key = at(h + 1, 45);        // nearly at the next number
                also(at(h, 15));
                also(at(h + 1, 15));        // "quarter to 9" heard as 9:15
                also(at(h - 1, 45));
            } else if (m == 15) {
                key = at(h, 45);            // past and to mixed up
                also(at(h + 1, 15));
                also(at(h - 1, 15));
                also(at(h - 1, 45));
            } else {
                key = at(h, 30);
                also(at(h + 1, 0));
                also(at(h - 1, 0));
                if (q.level == 3) also(at(h, 15));
                if (q.level == 3) also(at(h - 1, 45));
            }
            step = q.level == 2 ? 30 : 15;
            break;
        case 4:
            key = at(h, m / 5);             // 2:20 read as 2:04
            also(at(h, 60 - m));            // the dial read the wrong way round
            also(at(h + 1, m));
            also(t + 5);
            also(t - 5);
            break;
        case 5:
            key = at(h + 1, m);             // the hour hand nearly at the next
            also(at(h, m / 5));             // 9:50 read as 9:10
            also(at(h + 1, m / 5));
            also(t + 5);
            also(t - 5);
            break;
        case 6: {
            /* Counted on from the wrong numeral, by one or by two. Both on
             * one side: five either side would put the answer in the middle
             * of three, which is a way to answer without reading the dial. */
            const int side = (detail::stir(s) & 1U) != 0 ? 5 : -5;
            key = t + side;
            also(t + 2 * side);
            also(at(h + 1, m));
            also(swapped(t));
            o.gap = 3;
            break;
        }
        case 7:
            if (m == 0) {
                key = at(h, 30);
                also(at(h + 1, 0));
                also(at(h - 1, 0));
            } else if (m <= 30) {
                key = at(h - 1, 60 - m);    // "past" said as "to"
                also(at(h, 60 - m));
                also(at(h + 1, m));
            } else {
                key = at(h + 1, 60 - m);    // "to" said as "past"
                also(at(h, 60 - m));
                also(at(h - 1, m));
            }
            break;
        case 8:
            o.onDial = false;
            o.limit = 25 * 60;
            if (q.afterMidnight) {
                key = 12 * 60 + m;          // 12:05, the clock's own reading
                also(24 * 60 + m);
                also(60 + m);
                also(23 * 60 + m);
            } else {
                key = h * 60 + m;           // the 12-hour reading
                also((h + 10) * 60 + m);    // 3 p.m. as 13
                also((h + 11) * 60 + m);
                also((h + 13) * 60 + m);
            }
            step = 60;
            break;
        case 9: {
            const int32_t raw = static_cast<int32_t>(t) + q.change;
            const int32_t rawHour = raw >= 0 ? raw / 60 : -((59 - raw) / 60);
            const bool crosses = rawHour != h;
            key = static_cast<int32_t>(t) - q.change;   // the wrong way
            /* The hour: not carried when it should be, or carried when it
             * should not. */
            also(crosses ? (q.change > 0 ? q.answer - 60 : q.answer + 60)
                         : (q.change > 0 ? q.answer + 60 : q.answer - 60));
            also(q.answer + 5);
            also(q.answer - 5);
            break;
        }
        case 10:
        default:
            o.onDial = false;
            o.low = 1;
            o.limit = 60;
            key = m;                        // minutes past, not minutes to
            also(q.answer + 5);
            also(q.answer - 5);
            also(q.answer + 10);
            also(q.answer - 10);
            break;
    }

    o.add(q.answer);
    o.add(key);
    detail::shuffle(more, moreCount, s);
    for (uint8_t i = 0; i < moreCount && o.n < 4; ++i) o.add(more[i]);
    const bool upFirst = (detail::stir(s) & 1U) != 0;
    for (int32_t d = step; o.n < 4 && d < 2000; d += step) {
        o.add(static_cast<int32_t>(q.answer) + (upFirst ? d : -d));
        o.add(static_cast<int32_t>(q.answer) + (upFirst ? -d : d));
    }

    for (uint8_t i = 3; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % (i + 1));
        const uint16_t x = o.v[i];
        o.v[i] = o.v[j];
        o.v[j] = x;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        q.options[i] = o.v[i];
        if (o.v[i] == q.answer) q.correct = i;
    }
}

/* The question for `item` of a numbered level, options included. */
inline bool makeNumbered(uint8_t level, uint16_t item, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    if (!numbered(level) || item >= poolSize(level)) return false;
    q = Question{};
    q.level = level;
    q.minuteMarks = level >= 6;
    const int h = item % 12;
    switch (level) {
        case 1: q.shown = at(h, 0); break;
        case 2: q.shown = at(h, (item / 12) * 30); break;
        case 3: q.shown = at(h, (item / 12) * 15); break;
        case 4: q.shown = at(h, 5 * (1 + item / 12)); break;
        case 5: q.shown = at(h, 35 + 5 * (item / 12)); break;
        case 7:
            q.shown = at(h, 5 * (item / 12));
            q.label = Label::Words;
            break;
        case 8:
            q.label = Label::Day;
            if (item < 132) {
                /* 1 p.m. to 11 p.m.: twelve noon reads the same both ways. */
                const int hour = 1 + item % 11;
                const int minute = 5 * (item / 11);
                q.shown = at(hour, minute);
                q.answer = static_cast<uint16_t>((hour + 12) * 60 + minute);
            } else {
                q.afterMidnight = true;
                q.shown = at(0, 5 * (item - 132));
                q.answer = q.shown;
            }
            break;
        case 10:
        default:
            q.shown = at(h, 5 * (1 + item / 12));
            q.label = Label::Minutes;
            q.answer = static_cast<uint16_t>(60 - q.shown % 60);
            break;
    }
    if (q.label == Label::Clock || q.label == Label::Words) q.answer = q.shown;
    makeOptions(q, rnd);
    return true;
}

/* A question of a drawn level, from one random number, options included. */
inline void makeDrawn(uint8_t level, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    uint32_t s = rnd ^ 0x2545F491UL;
    if (s == 0) s = 1;
    detail::stir(s);
    q = Question{};
    q.level = level;
    q.minuteMarks = level >= 6;
    if (level == 9) {
        q.shown = static_cast<uint16_t>(5 * (detail::stir(s) % 144));
        q.change = CHANGES[detail::stir(s) % CHANGE_COUNT];
        q.answer = onClock(static_cast<int32_t>(q.shown) + q.change);
    } else {
        /* Any minute not on a five: the fives are the levels below. */
        uint16_t k = static_cast<uint16_t>(detail::stir(s) % 576);
        q.shown = static_cast<uint16_t>((k / 4) * 5 + 1 + k % 4);
        q.answer = q.shown;
        q.level = 6;
    }
    makeOptions(q, detail::stir(s));
}

/* What a drawn question is, for RecentQuestions: the same time, and on level
 * 9 the same change, is the same question. */
inline uint32_t token(const Question& q) {
    return static_cast<uint32_t>(q.shown) |
           (static_cast<uint32_t>(static_cast<uint16_t>(q.change)) << 16);
}

}   // namespace TimeCourse
