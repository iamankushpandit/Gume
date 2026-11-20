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

/* WHAT COLOR MIX TEACHES, LEVEL BY LEVEL.
 *
 * The game had six questions and no levels. Its wrong buttons were drawn
 * from seven names, one of them "brown", which was never a right answer --
 * so a player learned to skip it -- and "pink" was a magenta swatch.
 *
 * This course mixes paint: the first six mixes; then lighter and darker,
 * white or black added; then the same mixes backwards, "what was added?";
 * the colours in between a primary and its neighbour; opposite colours,
 * which make brown; and last, mixing LIGHT, where red and green make yellow
 * and three colours make white -- the thing paint does not do.
 *
 * The wrong buttons are what a player confuses a mix with: the result of
 * another mix that shares a colour with this one, lighter for darker, and
 * for light, what the same two colours make as paint. No button is ever a
 * second right answer -- orange and black make brown, and so do orange and
 * blue, so the two are never offered together -- and no two buttons are
 * names for nearly the same swatch.
 *
 * Pure C++, no Arduino and no heap: test/host/color_mix_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace ColorMixCourse {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

enum Colour : uint8_t {
    RED, BLUE, YELLOW, WHITE, BLACK, PURPLE, GREEN, ORANGE, PINK, LIGHT_BLUE,
    GRAY, BROWN, LIGHT_GREEN, LILAC, PEACH, CREAM, TAN, MAROON, NAVY,
    DARK_GREEN, OLIVE, RED_ORANGE, YELLOW_ORANGE, YELLOW_GREEN, BLUE_GREEN,
    BLUE_PURPLE, RED_PURPLE, CYAN, MAGENTA,
    COLOUR_COUNT
};

struct Swatch {
    const char* name;
    uint16_t fill;
    /* Swatches near enough to be mistaken for each other share a group, and
     * never stand on two buttons of one question. */
    uint8_t group;
};

/* The swatch values are a first choice made on a computer screen; they want
 * a look on each panel before they are final. */
constexpr Swatch SWATCHES[COLOUR_COUNT] = {
    {"red", rgb565(225, 30, 30), 0},
    {"blue", rgb565(30, 80, 230), 0},
    {"yellow", rgb565(250, 220, 30), 0},
    {"white", rgb565(255, 255, 255), 0},
    {"black", rgb565(0, 0, 0), 0},
    {"purple", rgb565(130, 40, 170), 0},
    {"green", rgb565(40, 170, 60), 0},
    {"orange", rgb565(250, 140, 20), 0},
    {"pink", rgb565(250, 170, 190), 0},
    {"light blue", rgb565(140, 190, 250), 2},
    {"gray", rgb565(128, 128, 128), 0},
    {"brown", rgb565(130, 80, 30), 0},
    {"light green", rgb565(150, 225, 150), 0},
    {"lilac", rgb565(200, 160, 225), 0},
    {"peach", rgb565(250, 200, 160), 1},
    {"cream", rgb565(250, 240, 200), 1},
    {"tan", rgb565(210, 180, 140), 1},
    {"maroon", rgb565(128, 0, 32), 0},
    {"navy", rgb565(0, 0, 128), 0},
    {"dark green", rgb565(0, 90, 30), 0},
    {"olive", rgb565(128, 128, 0), 0},
    {"red-orange", rgb565(245, 85, 25), 3},
    {"yellow-orange", rgb565(250, 180, 25), 3},
    {"yellow-green", rgb565(160, 200, 40), 0},
    {"blue-green", rgb565(20, 150, 140), 0},
    {"blue-purple", rgb565(90, 60, 200), 0},
    {"red-purple", rgb565(180, 30, 110), 0},
    {"cyan", rgb565(0, 230, 230), 2},
    {"magenta", rgb565(230, 0, 230), 0},
};

/* One mix: two colours and what they make, as paint or as light. */
struct Mix {
    uint8_t a;
    uint8_t b;
    uint8_t result;
    uint8_t light;
};

/* Rows 0-5 the first mixes, 6-15 lighter and darker, 16-21 in between,
 * 22-24 opposites, 25-30 light. Levels name rows by range. */
constexpr Mix MIXES[] = {
    {RED, BLUE, PURPLE, 0}, {BLUE, YELLOW, GREEN, 0}, {RED, YELLOW, ORANGE, 0},
    {RED, WHITE, PINK, 0}, {BLUE, WHITE, LIGHT_BLUE, 0}, {BLACK, WHITE, GRAY, 0},

    {GREEN, WHITE, LIGHT_GREEN, 0}, {PURPLE, WHITE, LILAC, 0}, {ORANGE, WHITE, PEACH, 0},
    {YELLOW, WHITE, CREAM, 0}, {BROWN, WHITE, TAN, 0}, {RED, BLACK, MAROON, 0},
    {BLUE, BLACK, NAVY, 0}, {GREEN, BLACK, DARK_GREEN, 0}, {ORANGE, BLACK, BROWN, 0},
    {YELLOW, BLACK, OLIVE, 0},

    {RED, ORANGE, RED_ORANGE, 0}, {YELLOW, ORANGE, YELLOW_ORANGE, 0},
    {YELLOW, GREEN, YELLOW_GREEN, 0}, {BLUE, GREEN, BLUE_GREEN, 0},
    {BLUE, PURPLE, BLUE_PURPLE, 0}, {RED, PURPLE, RED_PURPLE, 0},

    {RED, GREEN, BROWN, 0}, {BLUE, ORANGE, BROWN, 0}, {YELLOW, PURPLE, BROWN, 0},

    {RED, GREEN, YELLOW, 1}, {GREEN, BLUE, CYAN, 1}, {RED, BLUE, MAGENTA, 1},
    {YELLOW, BLUE, WHITE, 1}, {CYAN, RED, WHITE, 1}, {MAGENTA, GREEN, WHITE, 1},
};
constexpr uint8_t MIX_COUNT = sizeof(MIXES) / sizeof(MIXES[0]);

constexpr uint8_t LEVEL_COUNT = 6;

struct Level {
    const char* name;
    uint8_t first;      // first row of MIXES
    uint8_t rows;
    /* 0 forwards only, 1 backwards only, 2 both. Level 3 is levels 1 and 2
     * backwards. */
    uint8_t ways;
};

/* Pools: 6 + 10 + 16 + 12 + 6 + 12 = 62. */
constexpr Level LEVELS[LEVEL_COUNT] = {
    {"First mixes", 0, 6, 0},
    {"Lighter and darker", 6, 10, 0},
    {"What was added?", 0, 16, 1},
    {"In-between colours", 16, 6, 2},
    {"Opposites make brown", 22, 3, 3},  // 3: backwards, either colour hidden
    {"Mixing light", 25, 6, 2},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}

inline uint16_t poolSize(uint8_t level) {
    const Level& l = LEVELS[clampLevel(level) - 1];
    return static_cast<uint16_t>(l.ways >= 2 ? l.rows * 2 : l.rows);
}

inline const char* levelName(uint8_t level) { return LEVELS[clampLevel(level) - 1].name; }

/* "Level 5: Opposites make brown". */
constexpr uint8_t LEVEL_LINE_MAX = 30;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

struct Question {
    /* Forwards: left + right = ?. Backwards: left + ? = right, where right
     * is the result. */
    uint8_t left = RED;
    uint8_t right = BLUE;
    bool backwards = false;
    bool light = false;
    uint8_t answer = PURPLE;
    uint8_t options[4] = {};
    uint8_t correct = 0;

    const char* prompt() const {
        if (light) return backwards ? "Mixing light: what was added?" : "Mixing light: what do you get?";
        return backwards ? "What was added?" : "What do you get?";
    }
};

/* What `x` and `y` make, of the given kind; COLOUR_COUNT when no row says. */
inline uint8_t mixOf(uint8_t x, uint8_t y, uint8_t light) {
    for (uint8_t i = 0; i < MIX_COUNT; ++i) {
        const Mix& m = MIXES[i];
        if (m.light != light) continue;
        if ((m.a == x && m.b == y) || (m.a == y && m.b == x)) return m.result;
    }
    return COLOUR_COUNT;
}

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

    bool add(uint8_t c, const Question& q) {
        if (n >= 4 || c >= COLOUR_COUNT) return false;
        for (uint8_t i = 0; i < n; ++i) {
            if (v[i] == c) return false;
            const uint8_t g = SWATCHES[c].group;
            if (g != 0 && SWATCHES[v[i]].group == g) return false;
        }
        /* Never a second right answer. */
        if (c != q.answer) {
            if (q.backwards && mixOf(q.left, c, q.light) == q.right) return false;
            if (!q.backwards && c == mixOf(q.left, q.right, q.light)) return false;
        }
        v[n++] = c;
        return true;
    }
};

}   // namespace detail

/* Question `item` of `level`, with its options; false outside the pool. */
inline bool make(uint8_t level, uint16_t item, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    if (item >= poolSize(level)) return false;
    const Level& l = LEVELS[level - 1];

    uint32_t s = rnd ^ 0x3C6EF372UL;
    if (s == 0) s = 1;
    detail::stir(s);

    q = Question{};
    const uint8_t row = static_cast<uint8_t>(l.first + item % l.rows);
    const Mix& m = MIXES[row];
    q.light = m.light != 0;
    q.backwards = l.ways == 1 || l.ways == 3 || (l.ways == 2 && item >= l.rows);
    if (!q.backwards) {
        q.left = m.a;
        q.right = m.b;
        q.answer = m.result;
    } else {
        /* The colour hidden is the second, so the one shown is the first --
         * except on opposites, where either may be hidden. */
        const bool hideFirst = l.ways == 3 && item >= l.rows;
        q.left = hideFirst ? m.b : m.a;
        q.answer = hideFirst ? m.a : m.b;
        q.right = m.result;
    }

    /* Candidates, the commonest mistake first. */
    uint8_t key = COLOUR_COUNT;
    uint8_t more[MIX_COUNT];
    uint8_t moreCount = 0;
    if (!q.backwards) {
        /* Light: what the same two colours make as paint. */
        if (q.light) key = mixOf(q.left, q.right, 0);
        /* The result of another mix of the same kind sharing a colour with
         * this one -- red and white is pink, red and black is maroon --
         * from this level or the next, never a colour the player has not
         * met yet: red-purple has no business on the first level. */
        const uint8_t next = level < LEVEL_COUNT ? level : static_cast<uint8_t>(LEVEL_COUNT - 1);
        const uint8_t seenRows = static_cast<uint8_t>(LEVELS[next].first + LEVELS[next].rows);
        const uint8_t upTo = seenRows > l.first + l.rows ? seenRows : static_cast<uint8_t>(l.first + l.rows);
        for (uint8_t i = 0; i < MIX_COUNT; ++i) {
            const Mix& o = MIXES[i];
            if (i == row || o.light != m.light) continue;
            if (!m.light && i >= upTo) continue;
            const bool shares = o.a == q.left || o.b == q.left || o.a == q.right || o.b == q.right;
            if (shares && o.result != q.answer) more[moreCount++] = o.result;
        }
        if (moreCount == 0) {
            for (uint8_t i = 0; i < l.rows; ++i) more[moreCount++] = MIXES[l.first + i].result;
        }
    } else {
        /* A colour that goes with the one shown, but makes something else,
         * and the colours the result is made of when it is a mix itself. */
        for (uint8_t i = 0; i < MIX_COUNT; ++i) {
            const Mix& o = MIXES[i];
            if (o.light != m.light) continue;
            if (o.a == q.left) more[moreCount++] = o.b;
            else if (o.b == q.left) more[moreCount++] = o.a;
        }
        /* The darker or lighter one: white for black and black for white. */
        if (q.answer == WHITE) key = BLACK;
        else if (q.answer == BLACK) key = WHITE;
    }

    detail::Options o;
    o.add(q.answer, q);
    o.add(key, q);
    for (uint8_t i = moreCount; i > 1; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % i);
        const uint8_t t = more[i - 1];
        more[i - 1] = more[j];
        more[j] = t;
    }
    for (uint8_t i = 0; i < moreCount && o.n < 4; ++i) o.add(more[i], q);
    /* Still short: the level's own colours, then any. */
    for (uint8_t i = 0; i < l.rows && o.n < 4; ++i) {
        const Mix& r = MIXES[l.first + (i + detail::stir(s)) % l.rows];
        o.add(q.backwards ? r.b : r.result, q);
        o.add(q.backwards ? r.a : r.result, q);
    }
    for (uint8_t c = 0; c < COLOUR_COUNT && o.n < 4; ++c) {
        o.add(static_cast<uint8_t>((c + detail::stir(s)) % COLOUR_COUNT), q);
    }

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

}   // namespace ColorMixCourse
