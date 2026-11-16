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

/* WHAT MONEY TEACHES, LEVEL BY LEVEL.
 *
 * The game worked its level out of the visit's score and took its kind of
 * question from the score too -- Total, Make, More, Change in that order for
 * ever -- and a wrong tap could be tried again without limit, so the level
 * measured rounds played. Its wrong amounts were noise round the answer: in
 * Change the answer was the only one ending in 0 or 5.
 *
 * This course counts coins: ones and fives; tens, and making an amount;
 * quarters, and which of two piles is worth more -- built so the pile with
 * more coins is often worth less; a whole dollar; change in fives; change to
 * the cent; making an amount with the fewest coins; and change from two
 * prices. The coins are labelled by value, not by name: the names belong to
 * one country.
 *
 * A level's questions are numbered across its kinds, so the deck mixes them
 * and every one comes up before any comes back. Wrong amounts are the slips
 * players make: a coin missed, a coin counted twice, every coin counted as
 * one, the price given back as the change, ten out.
 *
 * Pure C++, no Arduino and no heap: test/host/money_course_test.cpp compiles
 * this file as it is. Randomness comes in as a number. */
namespace MoneyCourse {

constexpr uint8_t COINS[5] = {1, 5, 10, 25, 50};
constexpr uint8_t LEVEL_COUNT = 8;
constexpr uint8_t MAX_PILE = 12;
/* Make holds up to two rows of eight. */
constexpr uint8_t MAX_BUILT = 16;

enum class Mode : uint8_t { Total, Make, More, Change };

struct Level {
    const char* name;
    uint8_t coinKinds;      // the first n of COINS are in play
    uint16_t lo;            // amounts asked run lo..hi cents
    uint16_t hi;
    uint8_t totals;         // numbered items of each kind; 0 = none
    uint8_t makes;
    uint8_t mores;
    uint8_t changes;
    bool drawn;             // too many to number: change drawn at random
};

/* Pools: 13, 67, 207, 225, 32, drawn, 99, drawn. The test checks each item
 * is a question of the kind its range says. */
constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Ones and fives", 2, 3, 15, 13, 0, 0, 0, false},
    {"Tens", 3, 3, 35, 33, 34, 0, 0, false},
    {"Quarters", 4, 4, 75, 72, 74, 61, 0, false},
    {"A whole dollar", 5, 26, 100, 75, 75, 75, 0, false},
    {"Change in fives", 5, 5, 95, 0, 0, 0, 32, false},
    {"Change to the cent", 5, 1, 149, 0, 0, 0, 0, true},
    {"Fewest coins", 5, 1, 99, 0, 99, 0, 0, false},
    {"At the shop", 5, 5, 195, 0, 0, 0, 0, true},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const Level& levelOf(uint8_t level) { return LEVELS[clampLevel(level) - 1]; }
inline uint16_t poolSize(uint8_t level) {
    const Level& l = levelOf(level);
    return static_cast<uint16_t>(l.totals + l.makes + l.mores + l.changes);
}
inline bool numbered(uint8_t level) { return !levelOf(level).drawn; }
inline const char* levelName(uint8_t level) { return levelOf(level).name; }

constexpr uint8_t LEVEL_LINE_MAX = 28;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* "35c", or "$1.25" from a dollar up. */
inline int cents(uint16_t c, char* out, size_t len) {
    if (c < 100) return snprintf(out, len, "%uc", static_cast<unsigned>(c));
    return snprintf(out, len, "$%u.%02u", static_cast<unsigned>(c / 100),
                    static_cast<unsigned>(c % 100));
}

struct Round {
    uint8_t level = 1;
    Mode mode = Mode::Total;
    /* Total: the pile. More: the left pile and the right. */
    uint8_t a[MAX_PILE] = {};
    uint8_t aCount = 0;
    uint8_t b[MAX_PILE] = {};
    uint8_t bCount = 0;
    /* Total and Make: the amount. Change: the change. */
    uint16_t target = 0;
    /* Change: one price, or two added; and what is paid. */
    uint16_t price = 0;
    uint16_t price2 = 0;
    uint16_t paid = 0;
    /* Make on Fewest coins: Done is right only with the fewest coins. */
    bool fewest = false;
    /* More: 0 if the left pile is worth more, 1 the right. */
    uint8_t more = 0;
    uint16_t options[4] = {};
    uint8_t correct = 0;
};

inline uint16_t value(const uint8_t* coins, uint8_t n) {
    uint16_t t = 0;
    for (uint8_t i = 0; i < n; ++i) t = static_cast<uint16_t>(t + coins[i]);
    return t;
}

/* The fewest coins that make `c`: largest first is the least for these five. */
inline uint8_t fewestCoins(uint16_t c) {
    uint8_t n = 0;
    for (int8_t k = 4; k >= 0; --k) {
        while (c >= COINS[k]) {
            c = static_cast<uint16_t>(c - COINS[k]);
            ++n;
        }
    }
    return n;
}

/* The question line. */
inline const char* prompt(const Round& r) {
    switch (r.mode) {
        case Mode::Make: return r.fewest ? "Make it with the fewest coins" : "Make this amount";
        case Mode::More: return "Which is worth more?";
        case Mode::Change: return "How much change?";
        case Mode::Total:
        default: return "How much is this?";
    }
}

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* A pile worth exactly `total`, of between `minN` and `maxN` coins from the
 * first `kinds`, largest coins chosen at random first. False when none was
 * found -- the caller tries again with other numbers. */
inline bool pileOf(uint16_t total, uint8_t kinds, uint8_t minN, uint8_t maxN, uint32_t& s,
                   uint8_t* out, uint8_t& n) {
    for (uint8_t attempt = 0; attempt < 60; ++attempt) {
        n = 0;
        uint16_t left = total;
        while (left > 0 && n < MAX_PILE) {
            uint8_t fit[5];
            uint8_t f = 0;
            for (uint8_t k = 0; k < kinds; ++k) {
                if (COINS[k] <= left) fit[f++] = COINS[k];
            }
            /* One of the three largest that fit: small coins only, and a
             * pile of 75c is a fistful nobody counts. */
            const uint8_t c = fit[f - 1 - stir(s) % (f < 3 ? f : 3)];
            out[n++] = c;
            left = static_cast<uint16_t>(left - c);
        }
        if (left == 0 && n >= minN && n <= maxN) {
            /* Largest first, as a player would lay them out. */
            for (uint8_t i = 1; i < n; ++i) {
                for (uint8_t j = i; j > 0 && out[j] > out[j - 1]; --j) {
                    const uint8_t t = out[j];
                    out[j] = out[j - 1];
                    out[j - 1] = t;
                }
            }
            return true;
        }
    }
    return false;
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

inline void place(Round& r, Options& o, int key, const int* more, uint8_t moreCount, uint32_t& s) {
    const int answer = r.target;
    o.add(answer);
    o.add(key);
    int pick[6];
    for (uint8_t i = 0; i < moreCount; ++i) pick[i] = more[i];
    for (uint8_t i = moreCount; i > 1; --i) {
        const uint8_t j = static_cast<uint8_t>(stir(s) % i);
        const int t = pick[i - 1];
        pick[i - 1] = pick[j];
        pick[j] = t;
    }
    for (uint8_t i = 0; i < moreCount && o.n < 4; ++i) o.add(pick[i]);
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
        r.options[i] = o.v[i];
        if (o.v[i] == r.target) r.correct = i;
    }
}

inline void totalOptions(Round& r, uint32_t& s) {
    /* A coin missed -- the biggest is the one most often skipped -- a coin
     * counted twice, every coin counted as one cent. */
    Options o;
    const int t = r.target;
    int more[5];
    uint8_t m = 0;
    more[m++] = t + r.a[r.aCount - 1];
    more[m++] = r.aCount;
    more[m++] = t - r.a[r.aCount - 1];
    more[m++] = t + r.a[0];
    place(r, o, t - r.a[0], more, m, s);
}

inline void changeOptions(Round& r, uint32_t& s, bool fives) {
    /* The price given back, ten out either way, one out; from two prices,
     * one of them left out. */
    Options o;
    o.step = fives ? 5 : 1;
    const int t = r.target;
    int more[5];
    uint8_t m = 0;
    more[m++] = t + 10;
    more[m++] = t - 10;
    if (!fives) {
        more[m++] = t + 1;
        more[m++] = t - 1;
    }
    int key = r.price;
    if (r.price2 > 0) {
        key = r.paid - r.price;
        more[m++] = r.price + r.price2;
    }
    place(r, o, key, more, m, s);
}

}   // namespace detail

/* Question `item` of a numbered level. */
inline bool make(uint8_t level, uint16_t item, uint32_t rnd, Round& r) {
    level = clampLevel(level);
    const Level& l = levelOf(level);
    if (l.drawn || item >= poolSize(level)) return false;
    uint32_t s = rnd ^ 0x2C1B3C6DUL;
    if (s == 0) s = 1;
    detail::stir(s);
    r = Round{};
    r.level = level;

    if (item < l.totals) {
        r.mode = Mode::Total;
        r.target = static_cast<uint16_t>(l.lo + item);
        const uint8_t minN = level <= 2 ? 3 : 4;
        const uint8_t maxN = level <= 3 ? 6 : 8;
        if (!detail::pileOf(r.target, l.coinKinds, minN, maxN, s, r.a, r.aCount)) {
            /* Largest first: the fewest coins, which always fits. */
            r.aCount = 0;
            uint16_t left = r.target;
            for (int8_t k = static_cast<int8_t>(l.coinKinds - 1); k >= 0; --k) {
                while (left >= COINS[k] && r.aCount < MAX_PILE) {
                    r.a[r.aCount++] = COINS[k];
                    left = static_cast<uint16_t>(left - COINS[k]);
                }
            }
        }
        detail::totalOptions(r, s);
        return true;
    }
    item = static_cast<uint16_t>(item - l.totals);
    if (item < l.makes) {
        r.mode = Mode::Make;
        r.target = static_cast<uint16_t>((level == 7 ? 1 : 2) + item);
        if (level == 4) r.target = static_cast<uint16_t>(l.lo + item);
        r.fewest = level == 7;
        return true;
    }
    item = static_cast<uint16_t>(item - l.makes);
    if (item < l.mores) {
        /* Two piles a little apart, and half the time the one with more
         * coins is worth less: "more coins is more money" is the trap. */
        r.mode = Mode::More;
        const uint16_t left = static_cast<uint16_t>((level == 3 ? 14 : l.lo) + item);
        for (uint8_t attempt = 0; attempt < 80; ++attempt) {
            const int delta = 1 + static_cast<int>(detail::stir(s) % 10);
            const int right = (detail::stir(s) & 1U) ? left + delta : left - delta;
            if (right < 2 || right > l.hi + 10) continue;
            uint8_t na = 0;
            uint8_t nb = 0;
            if (!detail::pileOf(left, l.coinKinds, 2, 9, s, r.a, na)) continue;
            if (!detail::pileOf(static_cast<uint16_t>(right), l.coinKinds, 2, 9, s, r.b, nb)) continue;
            r.aCount = na;
            r.bCount = nb;
            const bool leftMore = left > right;
            const bool trap = (detail::stir(s) & 1U) != 0;
            const bool moreCoinsWins = leftMore ? na > nb : nb > na;
            if (trap && moreCoinsWins && attempt < 60) continue;
            r.more = leftMore ? 0 : 1;
            r.target = left;
            return true;
        }
        return false;
    }
    item = static_cast<uint16_t>(item - l.mores);
    /* Change in fives: paid 25c, 50c or $1.00, any price in fives below. */
    r.mode = Mode::Change;
    static constexpr uint16_t PAID[3] = {25, 50, 100};
    uint8_t row = 0;
    while (row < 2 && item >= PAID[row] / 5 - 1) {
        item = static_cast<uint16_t>(item - (PAID[row] / 5 - 1));
        ++row;
    }
    r.paid = PAID[row];
    r.price = static_cast<uint16_t>(5 * (1 + item));
    r.target = static_cast<uint16_t>(r.paid - r.price);
    detail::changeOptions(r, s, true);
    return true;
}

/* A question of a drawn level: change to the cent, or from two prices. */
inline void makeDrawn(uint8_t level, uint32_t rnd, Round& r) {
    level = clampLevel(level);
    uint32_t s = rnd ^ 0x19E3779BUL;
    if (s == 0) s = 1;
    detail::stir(s);
    r = Round{};
    r.level = level;
    r.mode = Mode::Change;
    if (level == 8) {
        static constexpr uint16_t PAID[3] = {100, 150, 200};
        r.paid = PAID[detail::stir(s) % 3];
        do {
            r.price = static_cast<uint16_t>(5 * (1 + detail::stir(s) % 16));
            r.price2 = static_cast<uint16_t>(5 * (1 + detail::stir(s) % 16));
        } while (r.price + r.price2 >= r.paid);
        r.target = static_cast<uint16_t>(r.paid - r.price - r.price2);
        detail::changeOptions(r, s, true);
    } else {
        static constexpr uint16_t PAID[3] = {50, 100, 150};
        r.paid = PAID[detail::stir(s) % 3];
        r.price = static_cast<uint16_t>(1 + detail::stir(s) % (r.paid - 1));
        r.target = static_cast<uint16_t>(r.paid - r.price);
        detail::changeOptions(r, s, false);
    }
}

inline uint32_t token(const Round& r) {
    return (static_cast<uint32_t>(r.paid) << 16) | (static_cast<uint32_t>(r.price) << 8) ^ r.price2;
}

}   // namespace MoneyCourse
