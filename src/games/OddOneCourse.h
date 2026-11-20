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

/* WHAT ODD ONE OUT TEACHES, LEVEL BY LEVEL.
 *
 * Seeing a difference, with no words. The game asked the colour first and
 * added shape, size and a turned triangle as the visit's score grew, so every
 * visit began with colour again; the odd size was always the bigger one, the
 * odd turn always the upside-down one, and the colours never got closer.
 *
 * This course asks one kind of difference at a time -- colour, shape, size,
 * a turn -- then any of them without saying which, then a colour only a
 * shade away, then a difference hidden among cards that differ in a way that
 * does not matter, and last the smallest steps. The odd card can be bigger
 * or smaller, upside down or the right way up.
 *
 * A round is nine cards, each with its own colour, shape, size and turn, so
 * the "ignore the rest" levels can vary the cards among themselves. Exactly
 * one card is alone in some way: every other value on the grid is shared by
 * at least three cards, which the test checks for every round it deals.
 *
 * Pure C++, no Arduino and no heap: test/host/odd_one_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace OddOneCourse {

constexpr uint8_t CARDS = 9;
constexpr uint8_t LEVEL_COUNT = 8;

enum class Shape : uint8_t { Circle, Square, Triangle };
constexpr uint8_t SHAPE_COUNT = 3;

enum class Kind : uint8_t { Colour, Shape, Size, Turn, Count };

/* Six well-separated hues: what the game had. */
constexpr uint8_t COLOUR_COUNT = 6;
constexpr uint16_t COLOURS[COLOUR_COUNT] = {0xF9EA, 0x5D9F, 0x37F0, 0xFFE6, 0xF81F, 0xFC00};

/* Radius of a card's shape: the cards are 66x40, so 19 at most, and under
 * about 6 nothing reads. */
constexpr int8_t BASE_SIZE = 13;

struct Level {
    const char* name;
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Colour"},
    {"Shape"},
    {"Size"},
    {"Turned"},
    {"Any one thing"},
    {"Close colours"},
    {"Ignore the rest"},
    {"Fine differences"},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const char* levelName(uint8_t level) { return LEVELS[clampLevel(level) - 1].name; }

constexpr uint8_t LEVEL_LINE_MAX = 26;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* A colour moved towards white or black by `percent`: the close colours.
 * The step wants a look on each panel, in more than one theme -- a step that
 * cannot be seen is a question with no answer. */
constexpr uint16_t shade(uint16_t c, bool lighter, uint8_t percent) {
    const int r = (c >> 11) & 0x1F;
    const int g = (c >> 5) & 0x3F;
    const int b = c & 0x1F;
    const int tr = lighter ? 0x1F : 0;
    const int tg = lighter ? 0x3F : 0;
    const int tb = lighter ? 0x1F : 0;
    const int nr = r + (tr - r) * percent / 100;
    const int ng = g + (tg - g) * percent / 100;
    const int nb = b + (tb - b) * percent / 100;
    return static_cast<uint16_t>((nr << 11) | (ng << 5) | nb);
}

/* The steps, by level: Close colours and Fine differences. */
constexpr uint8_t SHADE_STEP = 40;
constexpr uint8_t FINE_SHADE_STEP = 25;

struct Card {
    uint16_t colour = 0;
    Shape shape = Shape::Circle;
    int8_t size = BASE_SIZE;
    bool upsideDown = false;
};

struct Round {
    uint8_t level = 1;
    Card cards[CARDS];
    uint8_t odd = 0;
    Kind kind = Kind::Colour;
};

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* A colour that is not `c`, and on the shade levels not anything near it. */
inline uint16_t otherColour(uint16_t c, uint32_t& s) {
    for (;;) {
        const uint16_t o = COLOURS[stir(s) % COLOUR_COUNT];
        if (o != c) return o;
    }
}

}   // namespace detail

/* One round of `level`. */
inline void deal(uint8_t level, uint32_t rnd, Round& r) {
    level = clampLevel(level);
    uint32_t s = rnd ^ 0x68E31DA4UL;
    if (s == 0) s = 1;
    detail::stir(s);

    r = Round{};
    r.level = level;
    r.odd = static_cast<uint8_t>(detail::stir(s) % CARDS);
    Card base;
    base.colour = COLOURS[detail::stir(s) % COLOUR_COUNT];
    base.shape = static_cast<Shape>(detail::stir(s) % SHAPE_COUNT);

    switch (level) {
        case 1: r.kind = Kind::Colour; break;
        case 2: r.kind = Kind::Shape; break;
        case 3: r.kind = Kind::Size; break;
        case 4: r.kind = Kind::Turn; break;
        case 6: r.kind = Kind::Colour; break;
        default: r.kind = static_cast<Kind>(detail::stir(s) % static_cast<uint8_t>(Kind::Count)); break;
    }

    /* A turn is only seen on a triangle; the base may point either way. */
    if (r.kind == Kind::Turn) {
        base.shape = Shape::Triangle;
        base.upsideDown = (detail::stir(s) & 1U) != 0;
    }

    for (uint8_t i = 0; i < CARDS; ++i) r.cards[i] = base;

    /* Ignore the rest, and Fine differences one round in two: the cards
     * differ among themselves in a way that does not matter -- two values
     * split four and five, so neither is alone -- and the odd card differs
     * in another. */
    const bool noisy = level == 7 || (level == 8 && (detail::stir(s) & 1U) != 0);
    if (noisy) {
        Kind noise;
        do {
            noise = static_cast<Kind>(detail::stir(s) % static_cast<uint8_t>(Kind::Count));
        } while (noise == r.kind || noise == Kind::Size || (noise == Kind::Turn && r.kind != Kind::Turn &&
                                                            base.shape != Shape::Triangle));
        uint8_t order[CARDS];
        for (uint8_t i = 0; i < CARDS; ++i) order[i] = i;
        for (uint8_t i = CARDS - 1; i > 0; --i) {
            const uint8_t j = static_cast<uint8_t>(detail::stir(s) % (i + 1));
            const uint8_t t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
        const uint16_t second = detail::otherColour(base.colour, s);
        Shape otherShape = base.shape;
        while (otherShape == base.shape) {
            otherShape = static_cast<Shape>(detail::stir(s) % SHAPE_COUNT);
        }
        /* The noise can't be shape on a turn round, since a turn is only
         * seen on a triangle: then it is colour. */
        if (r.kind == Kind::Turn && noise == Kind::Shape) noise = Kind::Colour;
        const uint8_t split = static_cast<uint8_t>(4 + (detail::stir(s) & 1U));
        for (uint8_t k = 0; k < split; ++k) {
            Card& c = r.cards[order[k]];
            if (noise == Kind::Colour) c.colour = second;
            else if (noise == Kind::Shape) c.shape = otherShape;
            else c.upsideDown = !c.upsideDown;
        }
    }

    /* The odd card. */
    Card& odd = r.cards[r.odd];
    switch (r.kind) {
        case Kind::Colour:
            if (level == 6 || level == 8) {
                const bool lighter = (detail::stir(s) & 1U) != 0;
                odd.colour = shade(odd.colour, lighter, level == 6 ? SHADE_STEP : FINE_SHADE_STEP);
            } else {
                /* Different from every colour on the grid, not just its own. */
                uint16_t c;
                bool clash;
                do {
                    c = COLOURS[detail::stir(s) % COLOUR_COUNT];
                    clash = false;
                    for (uint8_t i = 0; i < CARDS; ++i) clash = clash || r.cards[i].colour == c;
                } while (clash);
                odd.colour = c;
            }
            break;
        case Kind::Shape: {
            Shape c;
            bool clash;
            do {
                c = static_cast<Shape>(detail::stir(s) % SHAPE_COUNT);
                clash = false;
                for (uint8_t i = 0; i < CARDS; ++i) clash = clash || r.cards[i].shape == c;
            } while (clash);
            odd.shape = c;
            break;
        }
        case Kind::Size: {
            const bool bigger = (detail::stir(s) & 1U) != 0;
            const int8_t step = level <= 3 ? (bigger ? 5 : 4) : (level == 8 ? 2 : 3);
            odd.size = static_cast<int8_t>(BASE_SIZE + (bigger ? step : -step));
            break;
        }
        case Kind::Turn:
        default:
            odd.upsideDown = !odd.upsideDown;
            break;
    }
}

/* A round's sameness, for RecentQuestions: the same kind, base and place is
 * the same question. */
inline uint32_t token(const Round& r) {
    const Card& b = r.cards[(r.odd + 1) % CARDS];
    return (static_cast<uint32_t>(r.level) << 24) | (static_cast<uint32_t>(r.kind) << 20) |
           (static_cast<uint32_t>(b.shape) << 16) | (static_cast<uint32_t>(b.colour & 0x0FFF) << 4) |
           r.odd;
}

}   // namespace OddOneCourse
