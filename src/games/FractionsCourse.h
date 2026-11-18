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

/* WHAT FRACTIONS TEACHES, LEVEL BY LEVEL.
 *
 * The game asked three kinds of question in an order taken from the visit's
 * score, so every visit began with halves and quarters in the same sequence.
 * Its level 4 was its level 3 again, equal fractions were excluded instead of
 * taught -- a pie of 2/4 could only ever be answered "2/4" -- and its wrong
 * answers included 0/4 and 4/4, which nobody picks. Any number of wrong taps
 * still scored.
 *
 * This course names and finds pies -- halves and quarters, thirds, then
 * fifths, sixths and eighths; compares them -- one piece against one piece,
 * the same bottom number, the same top number; finds equal fractions; then
 * compares any two, first with the pies and then without; and works with
 * fractions written down: a fraction of a number, adding and taking away
 * with the same bottom number, and more than one whole.
 *
 * Wrong answers are the misreadings: the empty slices counted instead of
 * the full ones, the full slices set against the empty ones (1 of 4 read as
 * 1/3), the bottom numbers added too (3/8 + 2/8 = 5/16), only the top halved
 * when simplifying, the whole number added instead of multiplied. No wrong
 * answer is equal to the right one -- 1/2 is never offered against 2/4.
 *
 * Pure C++, no Arduino and no heap: test/host/fractions_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace FractionsCourse {

/* A fraction, a mixed number (whole and n/d) or, with d == 1, a plain
 * number. */
struct Frac {
    uint8_t whole = 0;
    uint8_t n = 1;
    uint8_t d = 2;
};

/* Value comparisons by cross-multiplying, whole parts included. */
inline int32_t top(const Frac& f) { return static_cast<int32_t>(f.whole) * f.d + f.n; }
inline bool same(const Frac& a, const Frac& b) {
    return top(a) * b.d == top(b) * a.d;
}
inline bool greater(const Frac& a, const Frac& b) {
    return top(a) * b.d > top(b) * a.d;
}

inline uint8_t gcd(uint8_t a, uint8_t b) {
    while (b != 0) {
        const uint8_t t = static_cast<uint8_t>(a % b);
        a = b;
        b = t;
    }
    return a;
}

inline Frac frac(uint8_t n, uint8_t d) {
    Frac f;
    f.n = n;
    f.d = d;
    return f;
}

/* "3/4", "1  3/4", "9". */
inline int text(const Frac& f, char* out, size_t len) {
    if (f.d == 1) return snprintf(out, len, "%u", static_cast<unsigned>(f.n + f.whole));
    if (f.whole > 0 && f.n == 0) return snprintf(out, len, "%u", static_cast<unsigned>(f.whole));
    /* Two spaces: in font 4 one space is so narrow that 2 2/8 reads as
     * 22/8. */
    if (f.whole > 0) {
        return snprintf(out, len, "%u  %u/%u", static_cast<unsigned>(f.whole),
                        static_cast<unsigned>(f.n), static_cast<unsigned>(f.d));
    }
    return snprintf(out, len, "%u/%u", static_cast<unsigned>(f.n), static_cast<unsigned>(f.d));
}

enum class Mode : uint8_t {
    NameThePie,     // a pie: which fraction?
    FindThePie,     // a fraction: which pie?
    Compare,        // two pies: which is bigger?
    Written,        // words and numbers only: four written answers
};

constexpr uint8_t LEVEL_COUNT = 12;

struct Level {
    const char* name;
    uint8_t pool;   // 0 = drawn at random
};

/* Pools: 8, 12, 32, 56, 154, 68, 24, drawn, drawn, 114, 154, 88. */
constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Halves and quarters", 8},
    {"Thirds", 12},
    {"Fifths, sixths, eighths", 32},
    {"One piece each", 56},
    {"Same bottom number", 154},
    {"Same top number", 68},
    {"Equal fractions", 24},
    {"Compare any two", 0},
    {"Compare without pies", 0},
    {"A fraction of a number", 114},
    {"Add and take away", 154},
    {"More than one whole", 88},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline uint16_t poolSize(uint8_t level) { return LEVELS[clampLevel(level) - 1].pool; }
inline bool numbered(uint8_t level) { return poolSize(level) > 0; }
inline const char* levelName(uint8_t level) { return LEVELS[clampLevel(level) - 1].name; }

constexpr uint8_t LEVEL_LINE_MAX = 34;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

struct Question {
    uint8_t level = 1;
    Mode mode = Mode::NameThePie;
    /* The pie, the written fraction, or the left of two. */
    Frac a;
    /* The right of two; a fraction's whole number on "of a number"; the
     * second fraction of a sum. */
    Frac b;
    /* Written: 0 of, 1 plus, 2 minus, 3 to a mixed number, 4 to a fraction. */
    uint8_t op = 0;
    /* Compare without pies: they are drawn only once answered. */
    bool hidePies = false;
    Frac answer;
    Frac options[4];
    uint8_t correct = 0;     // Compare: 0 left, 1 right
};

/* The line above the question. */
inline const char* prompt(const Question& q) {
    switch (q.mode) {
        case Mode::NameThePie: return q.level == 7 ? "Pick the simplest form" : "Pick the matching fraction";
        case Mode::FindThePie: return q.level == 7 ? "Pick the same amount, cut smaller" : "Pick the matching pie";
        case Mode::Compare: return "Which fraction is bigger?";
        case Mode::Written:
        default: return "Work it out";
    }
}

/* A written question: "3/4 of 12", "3/8 + 2/8", "7/4 as a mixed number". */
inline int questionText(const Question& q, char* out, size_t len) {
    char a[12];
    char b[12];
    text(q.a, a, sizeof(a));
    text(q.b, b, sizeof(b));
    switch (q.op) {
        case 0: return snprintf(out, len, "%s of %s", a, b);
        case 1: return snprintf(out, len, "%s + %s", a, b);
        case 2: return snprintf(out, len, "%s - %s", a, b);
        default: return snprintf(out, len, "%s = ?", a);
    }
}

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

struct Options {
    Frac v[4];
    uint8_t n = 0;
    bool proper = true;     // every option between 0 and 1, exclusive
    bool numbers = false;   // plain numbers
    /* Pies drawn small are no finer than eighths: twelfths are 9px at the
     * rim of a radius-18 pie. 0 = no limit. */
    uint8_t maxDen = 0;

    bool add(const Frac& f) {
        if (n >= 4) return false;
        if (f.d == 0) return false;
        if (maxDen != 0 && f.d > maxDen) return false;
        if (numbers) {
            if (f.n == 0) return false;
        } else {
            if (f.n == 0 && f.whole == 0) return false;
            if (proper && (f.whole != 0 || f.n >= f.d)) return false;
            /* A mixed number's own fraction is proper; a fraction on its
             * own may be more than one. */
            if (!proper && f.whole > 0 && f.n >= f.d) return false;
        }
        for (uint8_t i = 0; i < n; ++i) {
            if (same(v[i], f)) return false;
        }
        v[n++] = f;
        return true;
    }
};

inline void place(Question& q, Options& o, const Frac* more, uint8_t m, uint32_t& s) {
    o.add(q.answer);
    Frac pick[8];
    for (uint8_t i = 0; i < m; ++i) pick[i] = more[i];
    /* The first candidate is the level's commonest slip and always offered;
     * the rest in a random order. */
    for (uint8_t i = m; i > 2; --i) {
        const uint8_t j = static_cast<uint8_t>(1 + stir(s) % (i - 1));
        const Frac t = pick[i - 1];
        pick[i - 1] = pick[j];
        pick[j] = t;
    }
    for (uint8_t i = 0; i < m && o.n < 4; ++i) o.add(pick[i]);
    /* Short: neighbours of the answer on its own bottom number, or numbers
     * either side. */
    for (uint8_t k = 1; o.n < 4 && k < 40; ++k) {
        Frac up = q.answer;
        Frac down = q.answer;
        up.n = static_cast<uint8_t>(up.n + k);
        if (down.n >= k) down.n = static_cast<uint8_t>(down.n - k);
        o.add(up);
        o.add(down);
        if (!o.numbers && o.n < 4) {
            Frac other = q.answer;
            other.d = static_cast<uint8_t>(q.answer.d + k);
            o.add(other);
        }
    }
    for (uint8_t i = 3; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(stir(s) % (i + 1));
        const Frac t = o.v[i];
        o.v[i] = o.v[j];
        o.v[j] = t;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        q.options[i] = o.v[i];
        if (same(o.v[i], q.answer)) q.correct = i;
    }
}

/* The k-th proper fraction n/d over the given bottom numbers, walked in
 * order; `reducedOnly` skips any that simplify. */
inline bool nthFraction(const uint8_t* dens, uint8_t count, uint16_t k, bool reducedOnly, Frac& out) {
    for (uint8_t i = 0; i < count; ++i) {
        for (uint8_t n = 1; n < dens[i]; ++n) {
            if (reducedOnly && gcd(n, dens[i]) != 1) continue;
            if (k == 0) {
                out = frac(n, dens[i]);
                return true;
            }
            --k;
        }
    }
    return false;
}

/* The pie misreadings: the empty slices for the full, full against empty,
 * one slice out either way. */
inline void pieOptions(Question& q, uint32_t& s) {
    const Frac& f = q.answer;
    Frac more[5];
    uint8_t m = 0;
    more[m++] = frac(static_cast<uint8_t>(f.d - f.n), f.d);
    if (f.d > f.n) more[m++] = frac(f.n, static_cast<uint8_t>(f.d - f.n));
    more[m++] = frac(static_cast<uint8_t>(f.n + 1), f.d);
    if (f.n > 1) more[m++] = frac(static_cast<uint8_t>(f.n - 1), f.d);
    more[m++] = frac(f.n, static_cast<uint8_t>(f.d + 1));
    Options o;
    if (q.mode == Mode::FindThePie) o.maxDen = 8;
    place(q, o, more, m, s);
}

constexpr uint8_t UNIT_DENS[8] = {2, 3, 4, 5, 6, 8, 10, 12};
constexpr uint8_t SAME_DENS[6] = {3, 4, 5, 6, 8, 10};

}   // namespace detail

/* Question `item` of a numbered level. */
inline bool make(uint8_t level, uint16_t item, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    if (!numbered(level) || item >= poolSize(level)) return false;
    uint32_t s = rnd ^ 0x7A3C9E15UL;
    if (s == 0) s = 1;
    detail::stir(s);
    q = Question{};
    q.level = level;

    switch (level) {
        case 1:
        case 2:
        case 3: {
            static constexpr uint8_t D1[2] = {2, 4};
            static constexpr uint8_t D2[3] = {2, 3, 4};
            static constexpr uint8_t D3[3] = {5, 6, 8};
            const uint8_t* dens = level == 1 ? D1 : (level == 2 ? D2 : D3);
            const uint8_t count = level == 1 ? 2 : 3;
            detail::nthFraction(dens, count, static_cast<uint16_t>(item / 2), false, q.a);
            q.mode = (item & 1U) ? Mode::FindThePie : Mode::NameThePie;
            q.answer = q.a;
            detail::pieOptions(q, s);
            return true;
        }
        case 4:
        case 5:
        case 6: {
            q.mode = Mode::Compare;
            uint16_t k = static_cast<uint16_t>(item / 2);
            Frac x;
            Frac y;
            bool found = false;
            if (level == 4) {
                for (uint8_t i = 0; i < 8 && !found; ++i) {
                    for (uint8_t j = static_cast<uint8_t>(i + 1); j < 8 && !found; ++j) {
                        if (k-- == 0) {
                            x = frac(1, detail::UNIT_DENS[i]);
                            y = frac(1, detail::UNIT_DENS[j]);
                            found = true;
                        }
                    }
                }
            } else if (level == 5) {
                for (uint8_t i = 0; i < 6 && !found; ++i) {
                    const uint8_t d = detail::SAME_DENS[i];
                    for (uint8_t a = 1; a < d && !found; ++a) {
                        for (uint8_t b = static_cast<uint8_t>(a + 1); b < d && !found; ++b) {
                            if (k-- == 0) {
                                x = frac(a, d);
                                y = frac(b, d);
                                found = true;
                            }
                        }
                    }
                }
            } else {
                for (uint8_t n = 2; n <= 5 && !found; ++n) {
                    for (uint8_t i = 0; i < 6 && !found; ++i) {
                        const uint8_t di = detail::SAME_DENS[i];
                        if (di <= n) continue;
                        for (uint8_t j = static_cast<uint8_t>(i + 1); j < 6 && !found; ++j) {
                            const uint8_t dj = detail::SAME_DENS[j];
                            if (k-- == 0) {
                                x = frac(n, di);
                                y = frac(n, dj);
                                found = true;
                            }
                        }
                    }
                }
            }
            if (!found) return false;
            const bool swap = (item & 1U) != 0;
            q.a = swap ? y : x;
            q.b = swap ? x : y;
            q.correct = greater(q.a, q.b) ? 0 : 1;
            q.answer = q.correct == 0 ? q.a : q.b;
            return true;
        }
        case 7: {
            if (item < 19) {
                /* A pie cut into more slices than it needs: name it simply. */
                static constexpr uint8_t DENS[5] = {4, 6, 8, 10, 12};
                uint16_t k = item;
                for (uint8_t i = 0; i < 5; ++i) {
                    for (uint8_t n = 1; n < DENS[i]; ++n) {
                        if (gcd(n, DENS[i]) == 1) continue;
                        if (k-- == 0) q.a = frac(n, DENS[i]);
                    }
                }
                const uint8_t g = gcd(q.a.n, q.a.d);
                q.mode = Mode::NameThePie;
                q.answer = frac(static_cast<uint8_t>(q.a.n / g), static_cast<uint8_t>(q.a.d / g));
                /* Only the top made smaller; the bottom only; a neighbour. */
                Frac more[4];
                uint8_t m = 0;
                more[m++] = frac(q.answer.n, q.a.d);
                more[m++] = frac(q.a.n, q.answer.d);
                more[m++] = frac(static_cast<uint8_t>(q.answer.n + 1), q.answer.d);
                more[m++] = frac(static_cast<uint8_t>(q.answer.n + 1), q.a.d);
                detail::Options o;
                detail::place(q, o, more, m, s);
            } else {
                /* A simple fraction written: the pie of the same amount,
                 * cut into more slices. */
                /* One question a written fraction: 1/2 asked as 2/4, 4/8
                 * and 3/6 would be one question three times. */
                static constexpr uint8_t EQ[5][4] = {{1, 2, 2, 4}, {1, 3, 2, 6}, {2, 3, 4, 6},
                                                     {1, 4, 2, 8}, {3, 4, 6, 8}};
                const uint8_t* e = EQ[item - 19];
                q.mode = Mode::FindThePie;
                q.a = frac(e[0], e[1]);
                q.answer = frac(e[2], e[3]);
                /* The same top number over more slices; one slice out. */
                Frac more[4];
                uint8_t m = 0;
                more[m++] = frac(e[0], e[3]);
                more[m++] = frac(static_cast<uint8_t>(e[2] + 1), e[3]);
                more[m++] = frac(static_cast<uint8_t>(e[2] - 1), e[3]);
                more[m++] = frac(e[2], static_cast<uint8_t>(e[3] + 2));
                detail::Options o;
                o.maxDen = 8;
                detail::place(q, o, more, m, s);
            }
            return true;
        }
        case 10: {
            /* n/d of k*d: the bottom number divides, the top multiplies. */
            static constexpr uint8_t DENS[7] = {2, 3, 4, 5, 6, 8, 10};
            Frac f;
            detail::nthFraction(DENS, 7, static_cast<uint16_t>(item / 6), true, f);
            const uint8_t k = static_cast<uint8_t>(1 + item % 6);
            q.mode = Mode::Written;
            q.op = 0;
            q.a = f;
            q.b = frac(static_cast<uint8_t>(k * f.d), 1);
            q.answer = frac(static_cast<uint8_t>(f.n * k), 1);
            /* Divided and not multiplied back; the part left over; one
             * slice too many or too few. */
            Frac more[4];
            uint8_t m = 0;
            more[m++] = frac(k, 1);
            more[m++] = frac(static_cast<uint8_t>(k * f.d - f.n * k), 1);
            more[m++] = frac(static_cast<uint8_t>((f.n + 1) * k), 1);
            if (f.n > 1) more[m++] = frac(static_cast<uint8_t>((f.n - 1) * k), 1);
            detail::Options o;
            o.numbers = true;
            detail::place(q, o, more, m, s);
            return true;
        }
        case 11: {
            const bool adding = item < 77;
            uint16_t k = static_cast<uint16_t>(item % 77);
            uint8_t d = 3;
            uint8_t a = 1;
            uint8_t b = 1;
            bool found = false;
            for (uint8_t i = 0; i < 6 && !found; ++i) {
                d = detail::SAME_DENS[i];
                for (uint8_t x = 1; x < d && !found; ++x) {
                    for (uint8_t y = 1; x + y < d && !found; ++y) {
                        if (k-- == 0) {
                            a = x;
                            b = y;
                            found = true;
                        }
                    }
                }
            }
            if (!found) return false;
            q.mode = Mode::Written;
            q.op = adding ? 1 : 2;
            if (adding) {
                q.a = frac(a, d);
                q.b = frac(b, d);
                q.answer = frac(static_cast<uint8_t>(a + b), d);
            } else {
                q.a = frac(static_cast<uint8_t>(a + b), d);
                q.b = frac(b, d);
                q.answer = frac(a, d);
            }
            /* The bottom numbers added (or taken away) too; the other sign;
             * one out. */
            Frac more[4];
            uint8_t m = 0;
            if (adding) {
                more[m++] = frac(static_cast<uint8_t>(a + b), static_cast<uint8_t>(2 * d));
                if (a > b) more[m++] = frac(static_cast<uint8_t>(a - b), d);
                if (b > a) more[m++] = frac(static_cast<uint8_t>(b - a), d);
            } else {
                more[m++] = frac(static_cast<uint8_t>(a + 2 * b), d);
                more[m++] = frac(a, static_cast<uint8_t>(2 * d));
            }
            more[m++] = frac(static_cast<uint8_t>(q.answer.n + 1), d);
            if (q.answer.n > 1) more[m++] = frac(static_cast<uint8_t>(q.answer.n - 1), d);
            detail::Options o;
            detail::place(q, o, more, m, s);
            return true;
        }
        case 12:
        default: {
            /* Between one and three wholes, never a whole number. */
            static constexpr uint8_t DENS[6] = {2, 3, 4, 5, 6, 8};
            uint16_t k = static_cast<uint16_t>(item / 2);
            Frac improper;
            bool found = false;
            for (uint8_t i = 0; i < 6 && !found; ++i) {
                const uint8_t d = DENS[i];
                for (uint8_t n = static_cast<uint8_t>(d + 1); n < 3 * d && !found; ++n) {
                    if (n % d == 0) continue;
                    if (k-- == 0) {
                        improper = frac(n, d);
                        found = true;
                    }
                }
            }
            if (!found) return false;
            const uint8_t d = improper.d;
            const uint8_t w = static_cast<uint8_t>(improper.n / d);
            const uint8_t r = static_cast<uint8_t>(improper.n % d);
            Frac mixed;
            mixed.whole = w;
            mixed.n = r;
            mixed.d = d;
            q.mode = Mode::Written;
            Frac more[4];
            uint8_t m = 0;
            if ((item & 1U) == 0) {
                q.op = 3;
                q.a = improper;
                q.answer = mixed;
                /* A whole out; the remainder one out; whole and remainder
                 * swapped. */
                Frac x = mixed;
                x.whole = static_cast<uint8_t>(w + 1);
                more[m++] = x;
                x = mixed;
                if (r + 1 < d) {
                    x.n = static_cast<uint8_t>(r + 1);
                    more[m++] = x;
                }
                x = mixed;
                if (r > 1) {
                    x.n = static_cast<uint8_t>(r - 1);
                    more[m++] = x;
                }
                if (w < d && r > 0) {
                    x.whole = r;
                    x.n = w;
                    more[m++] = x;
                }
            } else {
                q.op = 4;
                q.a = mixed;
                q.answer = improper;
                /* The whole added rather than multiplied; one out. */
                more[m++] = frac(static_cast<uint8_t>(w + r), d);
                more[m++] = frac(static_cast<uint8_t>(improper.n + 1), d);
                more[m++] = frac(static_cast<uint8_t>(improper.n - 1), d);
                more[m++] = frac(static_cast<uint8_t>(w * r + d), d);
            }
            detail::Options o;
            o.proper = false;
            detail::place(q, o, more, m, s);
            return true;
        }
    }
}

/* Compare any two, with pies or without: different bottom numbers, never
 * equal. */
inline void makeDrawn(uint8_t level, uint32_t rnd, Question& q) {
    level = clampLevel(level);
    uint32_t s = rnd ^ 0x5BD1E995UL;
    if (s == 0) s = 1;
    detail::stir(s);
    q = Question{};
    q.level = level;
    q.mode = Mode::Compare;
    q.hidePies = level == 9;
    for (;;) {
        const uint8_t d1 = detail::UNIT_DENS[detail::stir(s) % 8];
        const uint8_t d2 = detail::UNIT_DENS[detail::stir(s) % 8];
        if (d1 == d2) continue;
        const Frac x = frac(static_cast<uint8_t>(1 + detail::stir(s) % (d1 - 1)), d1);
        const Frac y = frac(static_cast<uint8_t>(1 + detail::stir(s) % (d2 - 1)), d2);
        if (same(x, y)) continue;
        q.a = x;
        q.b = y;
        break;
    }
    q.correct = greater(q.a, q.b) ? 0 : 1;
    q.answer = q.correct == 0 ? q.a : q.b;
}

inline uint32_t token(const Question& q) {
    return static_cast<uint32_t>(q.a.n) | (static_cast<uint32_t>(q.a.d) << 8) |
           (static_cast<uint32_t>(q.b.n) << 16) | (static_cast<uint32_t>(q.b.d) << 24);
}

}   // namespace FractionsCourse
