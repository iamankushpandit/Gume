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

/* WHAT SORTING TEACHES, LEVEL BY LEVEL.
 *
 * The game put four to six whole numbers on the board and asked for them in
 * order. Its difficulty was the rounds finished this visit, so every visit
 * began again; the direction ran four rounds up and two down, always in that
 * order; it ended at two-digit numbers -- never 0, never three digits, a
 * negative, a decimal or a fraction -- and a round could not be lost.
 *
 * This course orders three small numbers, then to twenty, then both ways
 * round; two digits; look-alikes (36 and 63, 38 and 83); three digits made of
 * the same three digits, so place value alone decides; below zero; decimals,
 * where 0.45 is less than 0.5 although it is longer; fractions; and decimals
 * and fractions together. From level 3 the direction is drawn at random,
 * never three rounds the same way.
 *
 * Every tile is a fraction underneath -- 7 is 7/1, 0.45 is 45/100 -- so any
 * two compare exactly by cross-multiplying, and no two tiles on a board are
 * ever equal in value.
 *
 * Pure C++, no Arduino and no heap: test/host/sort_course_test.cpp compiles
 * this file as it is. Randomness comes in as a number. */
namespace SortCourse {

constexpr uint8_t MAX_TILES = 6;
constexpr uint8_t LEVEL_COUNT = 10;

enum class Kind : uint8_t { Whole, Decimal, Fraction };

struct Tile {
    int16_t n = 0;
    uint16_t d = 1;
    Kind kind = Kind::Whole;
};

inline bool less(const Tile& a, const Tile& b) {
    return static_cast<int32_t>(a.n) * b.d < static_cast<int32_t>(b.n) * a.d;
}
inline bool equal(const Tile& a, const Tile& b) {
    return static_cast<int32_t>(a.n) * b.d == static_cast<int32_t>(b.n) * a.d;
}

/* "7", "-15", "0.45", "0.5", "3/4". */
inline int text(const Tile& t, char* out, size_t len) {
    switch (t.kind) {
        case Kind::Fraction:
            return snprintf(out, len, "%d/%u", t.n, static_cast<unsigned>(t.d));
        case Kind::Decimal: {
            const unsigned whole = static_cast<unsigned>(t.n / 100);
            const unsigned part = static_cast<unsigned>(t.n % 100);
            if (part % 10 == 0) return snprintf(out, len, "%u.%u", whole, part / 10);
            return snprintf(out, len, "%u.%02u", whole, part);
        }
        case Kind::Whole:
        default:
            return snprintf(out, len, "%d", t.n);
    }
}

struct Level {
    const char* name;
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Three small numbers"}, {"To twenty"}, {"Both ways"}, {"Two digits"}, {"Look-alikes"},
    {"Hundreds"},            {"Below zero"}, {"Decimals"}, {"Fractions"}, {"Mixed"},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const char* levelName(uint8_t level) { return LEVELS[clampLevel(level) - 1].name; }

constexpr uint8_t LEVEL_LINE_MAX = 30;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

struct Round {
    uint8_t level = 1;
    uint8_t count = 3;
    Tile tiles[MAX_TILES];
    /* tiles[order[k]] is the k-th to tap. */
    uint8_t order[MAX_TILES] = {};
    bool ascending = true;
};

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

inline int32_t between(uint32_t& s, int32_t lo, int32_t hi) {
    return lo + static_cast<int32_t>(stir(s) % static_cast<uint32_t>(hi - lo + 1));
}

struct Board {
    Tile t[MAX_TILES];
    uint8_t n = 0;
    uint8_t want = 0;

    bool add(const Tile& x) {
        if (n >= want) return false;
        for (uint8_t i = 0; i < n; ++i) {
            if (equal(t[i], x)) return false;
        }
        t[n++] = x;
        return true;
    }
};

inline Tile whole(int32_t v) {
    Tile t;
    t.n = static_cast<int16_t>(v);
    return t;
}
inline Tile hundredths(int32_t v) {
    Tile t;
    t.n = static_cast<int16_t>(v);
    t.d = 100;
    t.kind = Kind::Decimal;
    return t;
}
inline uint8_t gcd(uint8_t a, uint8_t b) {
    while (b != 0) {
        const uint8_t r = static_cast<uint8_t>(a % b);
        a = b;
        b = r;
    }
    return a;
}
/* A proper fraction in lowest terms, bottom 2..9. */
inline Tile fraction(uint32_t& s) {
    for (;;) {
        const uint8_t d = static_cast<uint8_t>(between(s, 2, 9));
        const uint8_t n = static_cast<uint8_t>(between(s, 1, d - 1));
        if (gcd(n, d) != 1) continue;
        Tile t;
        t.n = n;
        t.d = d;
        t.kind = Kind::Fraction;
        return t;
    }
}
/* Tenths and hundredths below one, never .0. */
inline Tile decimalBelowOne(uint32_t& s) {
    const bool tenths = (stir(s) & 1U) != 0;
    return hundredths(tenths ? 10 * between(s, 1, 9) : between(s, 1, 99));
}

}   // namespace detail

/* Deal a round of `level`. `lastTwo` is the direction of the last two rounds,
 * bit 0 the last (1 up), bit 1 the one before, and bit 2 set once there have
 * been two; it keeps the direction from running three the same. */
inline void deal(uint8_t level, uint32_t rnd, uint8_t lastTwo, Round& r) {
    level = clampLevel(level);
    uint32_t s = rnd ^ 0x7F4A7C15UL;
    if (s == 0) s = 1;
    detail::stir(s);
    r = Round{};
    r.level = level;

    detail::Board b;
    switch (level) {
        case 1: b.want = 3; break;
        case 2: b.want = 4; break;
        case 3: b.want = static_cast<uint8_t>(detail::between(s, 4, 5)); break;
        case 4:
        case 9: b.want = 5; break;
        case 8:
        case 10: b.want = static_cast<uint8_t>(detail::between(s, 5, 6)); break;
        default: b.want = 6; break;
    }

    switch (level) {
        case 1:
            while (b.n < b.want) b.add(detail::whole(detail::between(s, 1, 10)));
            break;
        case 2:
        case 3:
            while (b.n < b.want) b.add(detail::whole(detail::between(s, 0, 20)));
            break;
        case 4:
            while (b.n < b.want) b.add(detail::whole(detail::between(s, 1, 99)));
            break;
        case 5: {
            /* A reversed pair, and two more sharing a tens digit with it. */
            int32_t x = detail::between(s, 1, 9);
            int32_t y = detail::between(s, 1, 9);
            while (y == x) y = detail::between(s, 1, 9);
            b.add(detail::whole(10 * x + y));
            b.add(detail::whole(10 * y + x));
            while (b.n < 4) b.add(detail::whole(10 * x + detail::between(s, 0, 9)));
            while (b.n < b.want) b.add(detail::whole(detail::between(s, 10, 99)));
            break;
        }
        case 6: {
            /* The six orders of three different digits: place value is all
             * there is to go on. */
            int32_t d[3];
            d[0] = detail::between(s, 1, 9);
            do d[1] = detail::between(s, 1, 9); while (d[1] == d[0]);
            do d[2] = detail::between(s, 1, 9); while (d[2] == d[0] || d[2] == d[1]);
            static constexpr uint8_t P[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2},
                                                {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
            for (uint8_t i = 0; i < 6; ++i) {
                b.add(detail::whole(100 * d[P[i][0]] + 10 * d[P[i][1]] + d[P[i][2]]));
            }
            break;
        }
        case 7: {
            /* A number and its negative, and zero half the time. */
            const int32_t k = detail::between(s, 1, 20);
            b.add(detail::whole(k));
            b.add(detail::whole(-k));
            if (detail::stir(s) & 1U) b.add(detail::whole(0));
            while (b.n < b.want) b.add(detail::whole(detail::between(s, -20, 20)));
            break;
        }
        case 8: {
            /* A tenth, and the hundredths either side of it that a player
             * reads as bigger because they are longer: 0.5, 0.45, 0.54. */
            const int32_t whole = detail::between(s, 0, 9);
            const int32_t tenth = detail::between(s, 1, 9);
            b.add(detail::hundredths(100 * whole + 10 * tenth));
            b.add(detail::hundredths(100 * whole + 10 * (tenth - 1) + detail::between(s, 1, 9)));
            b.add(detail::hundredths(100 * whole + 10 * tenth + detail::between(s, 1, 9)));
            while (b.n < b.want) {
                const int32_t v = detail::between(s, 1, 999);
                if (v % 100 != 0) b.add(detail::hundredths(v));
            }
            break;
        }
        case 9:
            while (b.n < b.want) b.add(detail::fraction(s));
            break;
        case 10:
        default:
            while (b.n < b.want) {
                b.add((detail::stir(s) & 1U) ? detail::fraction(s) : detail::decimalBelowOne(s));
            }
            break;
    }

    /* Shuffle the board, then work out the order to tap it in. */
    for (uint8_t i = b.n; i > 1; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % i);
        const Tile t = b.t[i - 1];
        b.t[i - 1] = b.t[j];
        b.t[j] = t;
    }
    r.count = b.n;
    for (uint8_t i = 0; i < b.n; ++i) {
        r.tiles[i] = b.t[i];
        r.order[i] = i;
    }
    for (uint8_t i = 1; i < r.count; ++i) {
        for (uint8_t j = i; j > 0 && less(r.tiles[r.order[j]], r.tiles[r.order[j - 1]]); --j) {
            const uint8_t t = r.order[j];
            r.order[j] = r.order[j - 1];
            r.order[j - 1] = t;
        }
    }

    if (level <= 2) {
        r.ascending = true;
    } else {
        r.ascending = (detail::stir(s) & 1U) != 0;
        const bool two = (lastTwo & 4U) != 0;
        const bool last = (lastTwo & 1U) != 0;
        const bool before = (lastTwo & 2U) != 0;
        if (two && last == before && r.ascending == last) r.ascending = !last;
    }
    if (!r.ascending) {
        for (uint8_t i = 0; i < r.count / 2; ++i) {
            const uint8_t t = r.order[i];
            r.order[i] = r.order[r.count - 1 - i];
            r.order[r.count - 1 - i] = t;
        }
    }
}

/* The history after a round in direction `ascending`: bit 0 that round,
 * bit 1 the one before, bit 3 once one round is in, bit 2 once two are. */
inline uint8_t remember(uint8_t lastTwo, bool ascending) {
    const bool before = (lastTwo & 12U) != 0;
    return static_cast<uint8_t>((ascending ? 1U : 0U) | ((lastTwo & 1U) << 1) | (before ? 4U : 8U));
}

/* A board's sameness, for RecentQuestions. */
inline uint32_t token(const Round& r) {
    uint32_t h = 2166136261UL ^ r.level;
    for (uint8_t i = 0; i < r.count; ++i) {
        const Tile& t = r.tiles[r.order[i]];
        h = (h ^ static_cast<uint16_t>(t.n)) * 16777619UL;
        h = (h ^ t.d) * 16777619UL;
    }
    return h;
}

}   // namespace SortCourse
