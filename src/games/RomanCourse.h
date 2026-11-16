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

/* WHAT ROMAN TEACHES, LEVEL BY LEVEL.
 *
 * The game was already a teacher -- the key on the screen, the numeral taken
 * apart after every answer, the additive misreading offered as a wrong
 * answer -- but its level was the visit's score, so every visit began with
 * I, V and X and reached D and M only after twenty right answers in one
 * sitting. Its level 3 asked 90 to 99, which need a C its key did not show,
 * and its last level threw D, CD, CM and M in at once.
 *
 * This course adds one sign a level -- I V X, then tens, L (to 89, since 90
 * needs C), C, D, M -- then the whole notation, with one question in four
 * from a lower band chosen BY BAND, so small numbers really do come back;
 * and last, spotting the numeral written wrongly: IC for 99, XXXX for 40, VV
 * for 10. Within a level, numbers with a 4 or a 9 in them -- the subtractive
 * pairs, which are the lesson -- come up more often.
 *
 * Pure C++, no Arduino and no heap: test/host/roman_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace RomanCourse {

constexpr uint8_t TEXT_MAX = 24;
/* The answer buttons are 132px; at font 2 a numeral wider than about fifteen
 * signs is cut short by the button and looks broken. MMMDCCCLXXXVIII, the
 * longest right answer, is fifteen. */
constexpr uint8_t OPTION_MAX = 15;

struct Piece {
    uint16_t value;
    const char* symbol;
};

constexpr Piece PIECES[13] = {
    {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"}, {50, "L"},
    {40, "XL"},  {10, "X"},   {9, "IX"},  {5, "V"},    {4, "IV"},  {1, "I"},
};
/* With the subtractive pairs taken out: what a player writes before anyone
 * has said IV is not IIII. */
constexpr Piece PLAIN[7] = {
    {1000, "M"}, {500, "D"}, {100, "C"}, {50, "L"}, {10, "X"}, {5, "V"}, {1, "I"},
};

inline void write(uint16_t value, const Piece* table, uint8_t count, char* out, size_t len) {
    size_t at = 0;
    for (uint8_t i = 0; i < count; ++i) {
        while (value >= table[i].value) {
            for (const char* s = table[i].symbol; *s != '\0' && at + 1 < len; ++s) out[at++] = *s;
            value = static_cast<uint16_t>(value - table[i].value);
        }
    }
    out[at] = '\0';
}

inline void toRoman(uint16_t value, char* out, size_t len) { write(value, PIECES, 13, out, len); }
inline void toPlain(uint16_t value, char* out, size_t len) { write(value, PLAIN, 7, out, len); }

inline uint16_t letterValue(char c) {
    switch (c) {
        case 'I': return 1;
        case 'V': return 5;
        case 'X': return 10;
        case 'L': return 50;
        case 'C': return 100;
        case 'D': return 500;
        case 'M': return 1000;
        default: return 0;
    }
}

/* Every letter added, nothing taken away: XIV read as 16. */
inline uint16_t readAdditively(const char* roman) {
    uint32_t total = 0;
    for (const char* p = roman; *p != '\0'; ++p) total += letterValue(*p);
    return static_cast<uint16_t>(total);
}

/* A numeral's value by the rules -- a smaller sign before a bigger one is
 * taken away -- whether or not it is written properly. */
inline uint16_t readValue(const char* roman) {
    int32_t total = 0;
    for (const char* p = roman; *p != '\0'; ++p) {
        const int32_t v = letterValue(*p);
        const int32_t next = p[1] != '\0' ? letterValue(p[1]) : 0;
        total += v < next ? -v : v;
    }
    return static_cast<uint16_t>(total);
}

constexpr uint8_t LEVEL_COUNT = 8;

struct Level {
    const char* name;
    uint16_t lo;
    uint16_t hi;
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"I, V and X", 1, 10},
    {"Tens and ones", 11, 39},
    {"L", 40, 89},
    {"C", 90, 399},
    {"D", 400, 899},
    {"M", 900, 1999},
    {"The whole notation", 2000, 3999},
    {"Spot the mistake", 1, 3999},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const Level& levelOf(uint8_t level) { return LEVELS[clampLevel(level) - 1]; }
inline const char* levelName(uint8_t level) { return levelOf(level).name; }

/* Levels 1 to 3 are small enough to number: each value asked both ways. */
inline uint16_t poolSize(uint8_t level) {
    level = clampLevel(level);
    if (level > 3) return 0;
    return static_cast<uint16_t>(2 * (levelOf(level).hi - levelOf(level).lo + 1));
}
inline bool numbered(uint8_t level) { return poolSize(level) > 0; }

constexpr uint8_t LEVEL_LINE_MAX = 30;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

struct Question {
    uint8_t level = 1;
    uint16_t value = 1;
    /* The numeral is shown and the answer is a number; or the other way. */
    bool askNumber = true;
    char subject[TEXT_MAX] = {};
    char options[4][TEXT_MAX] = {};
    uint8_t correct = 0;
};

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

inline bool hasFourOrNine(uint16_t v) {
    for (; v > 0; v = static_cast<uint16_t>(v / 10)) {
        if (v % 10 == 4 || v % 10 == 9) return true;
    }
    return false;
}

struct Options {
    char v[4][TEXT_MAX] = {};
    uint8_t n = 0;

    bool add(const char* text) {
        if (n >= 4 || text[0] == '\0' || strlen(text) > OPTION_MAX) return false;
        for (uint8_t i = 0; i < n; ++i) {
            if (strcmp(v[i], text) == 0) return false;
        }
        snprintf(v[n++], TEXT_MAX, "%s", text);
        return true;
    }
};

/* Replace the first `from` in `in` with `to`. */
inline bool swapIn(const char* in, const char* from, const char* to, char* out, size_t len) {
    const char* at = strstr(in, from);
    if (at == nullptr) return false;
    const size_t head = static_cast<size_t>(at - in);
    snprintf(out, len, "%.*s%s%s", static_cast<int>(head), in, to, at + strlen(from));
    return true;
}

/* Spot the mistake: numerals of the same value written wrongly. The
 * shortcut pairs (IC for 99), a five-sign doubled (VV for X), and four of
 * one sign in a row (XXXX for XL). */
inline uint8_t formationErrors(uint16_t v, const char* right, char out[][TEXT_MAX], uint8_t max) {
    struct Shortcut {
        const char* proper;
        const char* wrong;
    };
    static constexpr Shortcut SHORT[10] = {
        {"CMXCIX", "IM"}, {"CDXCIX", "ID"}, {"XCIX", "IC"}, {"XLIX", "IL"}, {"CMXC", "XM"},
        {"CDXC", "XD"},   {"XCV", "VC"},    {"XLV", "VL"},  {"IX", "VIV"},  {"XC", "LXL"},
    };
    uint8_t n = 0;
    char buf[TEXT_MAX];
    for (uint8_t i = 0; i < 10 && n < max; ++i) {
        if (swapIn(right, SHORT[i].proper, SHORT[i].wrong, buf, sizeof(buf)) && readValue(buf) == v &&
            strlen(buf) <= OPTION_MAX) {
            snprintf(out[n++], TEXT_MAX, "%s", buf);
            break;
        }
    }
    toPlain(v, buf, sizeof(buf));
    if (n < max && strcmp(buf, right) != 0 && strlen(buf) <= OPTION_MAX) {
        snprintf(out[n++], TEXT_MAX, "%s", buf);
    }
    /* A sign that stands on its own -- not the X of XC -- written as two of
     * the five-sign below it. */
    static constexpr char WHOLE[3] = {'X', 'C', 'M'};
    static constexpr char HALF[3] = {'V', 'L', 'D'};
    for (uint8_t i = 0; i < 3 && n < max; ++i) {
        for (size_t at = 0; right[at] != '\0'; ++at) {
            if (right[at] != WHOLE[i]) continue;
            if (right[at + 1] != '\0' && letterValue(right[at + 1]) > letterValue(WHOLE[i])) continue;
            snprintf(buf, sizeof(buf), "%.*s%c%c%s", static_cast<int>(at), right, HALF[i], HALF[i],
                     right + at + 1);
            if (readValue(buf) == v && strlen(buf) <= OPTION_MAX) {
                snprintf(out[n++], TEXT_MAX, "%s", buf);
                i = 3;
            }
            break;
        }
    }
    return n;
}

}   // namespace detail

/* The four answers: the right one; the lesson's own mistake -- the numeral
 * read with every letter added, or written with no subtraction; and near
 * values. Spot the mistake offers only wrongly written numerals of the same
 * value, so the number cannot be what gives it away. */
inline void makeOptions(Question& q, uint32_t& s) {
    char right[TEXT_MAX];
    toRoman(q.value, right, sizeof(right));
    detail::Options o;
    char answer[TEXT_MAX];
    if (q.askNumber) {
        snprintf(answer, sizeof(answer), "%u", static_cast<unsigned>(q.value));
    } else {
        snprintf(answer, sizeof(answer), "%s", right);
    }
    o.add(answer);
    char buf[TEXT_MAX];
    if (q.level == 8) {
        char errors[3][TEXT_MAX];
        const uint8_t e = detail::formationErrors(q.value, right, errors, 3);
        for (uint8_t i = 0; i < e; ++i) o.add(errors[i]);
    } else if (q.askNumber) {
        const uint16_t misread = readAdditively(right);
        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(misread));
        if (misread != q.value) o.add(buf);
    } else {
        toPlain(q.value, buf, sizeof(buf));
        o.add(buf);
    }
    static constexpr int16_t SPREAD[LEVEL_COUNT] = {3, 3, 6, 12, 30, 60, 60, 60};
    const int16_t spread = SPREAD[q.level - 1];
    for (uint8_t attempt = 0; o.n < 4 && attempt < 200; ++attempt) {
        int32_t other = static_cast<int32_t>(q.value) +
                        static_cast<int32_t>(detail::stir(s) % (2 * spread + 1)) - spread;
        if (attempt > 100) other = static_cast<int32_t>(q.value) + (attempt - 100);
        if (other < 1 || other > 3999 || other == q.value) continue;
        if (q.askNumber) {
            snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(other));
        } else {
            toRoman(static_cast<uint16_t>(other), buf, sizeof(buf));
        }
        o.add(buf);
    }
    for (uint8_t i = 3; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % (i + 1));
        char t[TEXT_MAX];
        snprintf(t, sizeof(t), "%s", o.v[i]);
        snprintf(o.v[i], TEXT_MAX, "%s", o.v[j]);
        snprintf(o.v[j], TEXT_MAX, "%s", t);
    }
    for (uint8_t i = 0; i < 4; ++i) {
        snprintf(q.options[i], TEXT_MAX, "%s", o.v[i]);
        if (strcmp(o.v[i], answer) == 0) q.correct = i;
    }
}

inline void finish(Question& q, uint32_t& s) {
    if (q.askNumber) {
        toRoman(q.value, q.subject, sizeof(q.subject));
    } else {
        snprintf(q.subject, sizeof(q.subject), "%u", static_cast<unsigned>(q.value));
    }
    makeOptions(q, s);
}

/* Question `item` of a numbered level: each value read and written. */
inline bool make(uint8_t level, uint16_t item, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    if (!numbered(level) || item >= poolSize(level)) return false;
    uint32_t s = rnd ^ 0x4F1BBCDCUL;
    if (s == 0) s = 1;
    detail::stir(s);
    q = Question{};
    q.level = level;
    q.value = static_cast<uint16_t>(levelOf(level).lo + item / 2);
    q.askNumber = (item & 1U) == 0;
    finish(q, s);
    return true;
}

/* A question of a drawn level. Two in three of the numbers have a 4 or a 9
 * in them. The whole notation asks one question in four from a lower band,
 * the band chosen first, so 7 is as likely as 700. Spot the mistake only
 * asks numbers that can be written wrongly three ways. */
inline void makeDrawn(uint8_t level, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    uint32_t s = rnd ^ 0x2B992DDFUL;
    if (s == 0) s = 1;
    detail::stir(s);
    q = Question{};
    q.level = level;
    const bool lesson = detail::stir(s) % 3 != 0;
    for (uint16_t attempt = 0; attempt < 400; ++attempt) {
        const Level* band = &levelOf(level);
        if (level == 7 && detail::stir(s) % 4 == 0) band = &LEVELS[detail::stir(s) % 6];
        const uint16_t span = static_cast<uint16_t>(band->hi - band->lo + 1);
        q.value = static_cast<uint16_t>(band->lo + detail::stir(s) % span);
        if (level == 8) {
            char right[TEXT_MAX];
            char errors[3][TEXT_MAX];
            toRoman(q.value, right, sizeof(right));
            if (detail::formationErrors(q.value, right, errors, 3) == 3) break;
            continue;
        }
        if (!lesson || detail::hasFourOrNine(q.value)) break;
    }
    q.askNumber = level == 8 ? false : (detail::stir(s) & 1U) != 0;
    finish(q, s);
}

inline uint32_t token(const Question& q) {
    return static_cast<uint32_t>(q.value) * 2 + (q.askNumber ? 1 : 0);
}

}   // namespace RomanCourse
