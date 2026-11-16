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

/* WHAT SHAPES TEACHES, LEVEL BY LEVEL.
 *
 * The game matched a filled shape to its own outline -- the same picture
 * twice -- so the name printed on the row was never asked and the side
 * counts in its table were never read. Its level was the number of rounds
 * played this visit, so every visit dealt the same three rounds in the same
 * order: red circle, blue square, green triangle, yellow star, then the four
 * polygons, then the four lookalikes.
 *
 * This course asks what a shape IS: first by its outline, in any colour; then
 * among the lookalikes a player calls "circle" and "square" until somebody
 * makes them look twice; then by how many sides it has; then by its name
 * alone, with no name on the picture; then turned, because a triangle is
 * still a triangle upside down; then concave or convex; then the sides of
 * any shape, the stars and the cross included.
 *
 * A round is four shapes. A shape is dealt from the level's deck, so every
 * shape of a level comes up before any comes back, and one matched wrong
 * comes round again three shapes later.
 *
 * Pure C++, no Arduino and no heap: test/host/shapes_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace ShapesCourse {

enum class Shape : uint8_t {
    Circle,
    Square,
    Triangle,
    Star5,
    Pentagon,
    Hexagon,
    Heptagon,
    Octagon,
    Oval,
    Rectangle,
    Diamond,
    Trapezium,
    Cross,
    Arrow,
    Star4,
    Parallelogram,
    Kite,
    Count
};
constexpr uint8_t SHAPE_COUNT = static_cast<uint8_t>(Shape::Count);

/* What a shape is, beside how it is drawn. `turns` says which turns of it
 * look different from the shape standing as drawn: bit 1 a quarter turn,
 * bit 2 a half turn, bit 3 three quarters. A parallelogram turned half way
 * round is the same parallelogram; a rectangle a quarter round is not. */
struct Fact {
    const char* name;
    uint8_t sides;      // 0 for anything with a curve in it
    bool concave;
    uint8_t turns;
};

constexpr Fact FACTS[SHAPE_COUNT] = {
    {"circle", 0, false, 0},
    {"square", 4, false, 0},
    {"triangle", 3, false, 0x0E},
    {"star", 10, true, 0x0E},
    {"pentagon", 5, false, 0x0E},
    {"hexagon", 6, false, 0x0A},
    {"heptagon", 7, false, 0x0E},
    {"octagon", 8, false, 0},
    {"oval", 0, false, 0x0A},
    {"rectangle", 4, false, 0x0A},
    {"diamond", 4, false, 0x0A},
    {"trapezium", 4, false, 0x0E},
    {"cross", 12, true, 0},
    {"arrow", 7, true, 0x0E},
    {"pinwheel", 8, true, 0},
    {"parallelogram", 4, false, 0x0A},
    {"kite", 4, false, 0x0E},
};

inline const Fact& fact(Shape s) { return FACTS[static_cast<uint8_t>(s)]; }

/* How a round asks. */
enum class Target : uint8_t {
    Outline,    // the outline of the same shape
    Sides,      // "6 sides"
    Name,       // "hexagon", with the picture's own name left off
};

constexpr uint8_t LEVEL_COUNT = 7;

struct Level {
    const char* name;
    const Shape* pool;
    uint8_t poolSize;
    Target target;
    /* The picture's row carries its name. */
    bool named;
    /* The outlines are drawn turned. */
    bool turned;
    /* Two concave and two convex, with the word on the row. */
    bool concaveConvex;
};

constexpr Shape FIRST[] = {Shape::Circle, Shape::Square, Shape::Triangle, Shape::Star5};
constexpr Shape LOOKALIKES[] = {Shape::Circle, Shape::Oval, Shape::Square, Shape::Rectangle,
                                Shape::Diamond, Shape::Trapezium, Shape::Parallelogram, Shape::Kite};
constexpr Shape POLYGONS[] = {Shape::Triangle, Shape::Square, Shape::Pentagon,
                              Shape::Hexagon, Shape::Heptagon, Shape::Octagon};
constexpr Shape EVERY[] = {Shape::Circle, Shape::Square, Shape::Triangle, Shape::Star5,
                           Shape::Pentagon, Shape::Hexagon, Shape::Heptagon, Shape::Octagon,
                           Shape::Oval, Shape::Rectangle, Shape::Diamond, Shape::Trapezium,
                           Shape::Cross, Shape::Arrow, Shape::Star4, Shape::Parallelogram,
                           Shape::Kite};
constexpr Shape TURNABLE[] = {Shape::Triangle, Shape::Pentagon, Shape::Heptagon,
                              Shape::Trapezium, Shape::Arrow, Shape::Star5, Shape::Rectangle,
                              Shape::Oval, Shape::Kite, Shape::Parallelogram};
/* Concave and convex, four of each -- a round takes two of each, so a pool
 * with more of one kind leaves some of them waiting for ever -- each named in
 * eight characters at most: the word "concave" shares the row. */
constexpr Shape CONCAVE_CONVEX[] = {Shape::Cross, Shape::Arrow, Shape::Star5, Shape::Star4,
                                    Shape::Triangle, Shape::Pentagon, Shape::Hexagon,
                                    Shape::Kite};
/* Every count from three to twelve that a shape here has, at most two shapes
 * a count: a round takes one shape a count, so six four-sided shapes would
 * each wait six rounds and more. */
constexpr Shape ANY_SIDES[] = {Shape::Triangle, Shape::Square, Shape::Trapezium,
                               Shape::Pentagon, Shape::Hexagon, Shape::Heptagon, Shape::Arrow,
                               Shape::Octagon, Shape::Star4, Shape::Star5, Shape::Cross};

#define SHAPES_POOL(a) a, static_cast<uint8_t>(sizeof(a) / sizeof(a[0]))
constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Colour and shape", SHAPES_POOL(FIRST), Target::Outline, true, false, false},
    {"Lookalikes", SHAPES_POOL(LOOKALIKES), Target::Outline, true, false, false},
    {"Count the sides", SHAPES_POOL(POLYGONS), Target::Sides, true, false, false},
    {"Shape names", SHAPES_POOL(EVERY), Target::Name, false, false, false},
    {"Turned shapes", SHAPES_POOL(TURNABLE), Target::Outline, true, true, false},
    {"Concave or convex", SHAPES_POOL(CONCAVE_CONVEX), Target::Outline, true, false, true},
    {"Sides of any shape", SHAPES_POOL(ANY_SIDES), Target::Sides, true, false, false},
};
#undef SHAPES_POOL

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}

inline const Level& levelOf(uint8_t level) { return LEVELS[clampLevel(level) - 1]; }
inline uint16_t poolSize(uint8_t level) { return levelOf(level).poolSize; }
inline const char* levelName(uint8_t level) { return levelOf(level).name; }

/* "Level 7: Sides of any shape". */
constexpr uint8_t LEVEL_LINE_MAX = 28;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* The six round colours, by name: level 1 says the colour as well as the
 * shape. The game owns the RGB values, in the same order. */
constexpr uint8_t COLOUR_COUNT = 6;
constexpr const char* COLOUR_NAMES[COLOUR_COUNT] = {"red", "blue", "green", "yellow", "purple",
                                                    "orange"};

/* What a row says: "purple triangle" is the longest. */
constexpr uint8_t ROW_NAME_MAX = 15;

struct Round {
    uint8_t level = 1;
    Shape shapes[4] = {};
    uint8_t colours[4] = {};
    /* The deck item each shape was dealt as, for missed(). */
    uint8_t items[4] = {};
    /* Which shape stands on each target row. Never the rows' own order, or
     * the round is solved by position. */
    uint8_t targetOrder[4] = {};
    /* Quarter turns of each shape's outline, clockwise. */
    uint8_t turns[4] = {};
};

/* What the left-hand row says for shape `i`, or nothing at Shape names. */
inline int rowName(const Round& r, uint8_t i, char* out, size_t len) {
    const Level& l = levelOf(r.level);
    if (!l.named) return snprintf(out, len, "%s", "");
    if (r.level == 1) {
        return snprintf(out, len, "%s %s", COLOUR_NAMES[r.colours[i]], fact(r.shapes[i]).name);
    }
    return snprintf(out, len, "%s", fact(r.shapes[i]).name);
}

/* What a target row says when it is words: "6 sides", or the name. */
inline int targetText(const Round& r, uint8_t shapeIndex, char* out, size_t len) {
    const Level& l = levelOf(r.level);
    if (l.target == Target::Sides) {
        return snprintf(out, len, "%u sides", static_cast<unsigned>(fact(r.shapes[shapeIndex]).sides));
    }
    return snprintf(out, len, "%s", fact(r.shapes[shapeIndex]).name);
}

/* The line above the rows. */
inline const char* instruction(uint8_t level) {
    const Level& l = levelOf(level);
    if (l.target == Target::Sides) return "Tap a shape, then how many sides it has";
    if (l.target == Target::Name) return "Tap a shape, then its name";
    if (l.turned) return "Tap a shape, then its outline, turned";
    return "Tap a shape, then its matching outline";
}

namespace detail {

inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* Two shapes that may not share a round: on a sides level, two with the same
 * count would make two right answers. */
inline bool clash(const Level& l, Shape a, Shape b) {
    if (a == b) return true;
    return l.target == Target::Sides && fact(a).sides == fact(b).sides;
}

}   // namespace detail

/* Deal a round of `level` from `deck`, which holds the level's pool. A deal
 * that clashes with a shape already in the round is skipped and the next one
 * taken. */
inline void deal(uint8_t level, Course::QuestionDeck& deck, uint32_t rnd, Round& r) {
    level = clampLevel(level);
    const Level& l = levelOf(level);
    uint32_t s = rnd ^ 0x1B873593UL;
    if (s == 0) s = 1;
    detail::stir(s);

    r = Round{};
    r.level = level;
    uint8_t n = 0;
    uint8_t concave = 0;
    for (uint16_t tries = 0; n < 4 && tries < 400; ++tries) {
        const uint16_t item = deck.next(detail::stir(s));
        const Shape shape = l.pool[item];
        bool ok = true;
        for (uint8_t k = 0; k < n; ++k) ok = ok && !detail::clash(l, r.shapes[k], shape);
        if (ok && l.concaveConvex) {
            /* Two of each: the word on the row only teaches when it tells
             * two rows apart. */
            const bool isConcave = fact(shape).concave;
            const uint8_t sameKind = isConcave ? concave : static_cast<uint8_t>(n - concave);
            ok = sameKind < 2;
        }
        /* A deal that does not fit this round is skipped: the deck counts it
         * dealt, and the next pass deals it first. Putting it back as missed
         * instead filled the retry queue with the same refused shapes. */
        if (!ok) continue;
        if (fact(shape).concave) ++concave;
        r.items[n] = static_cast<uint8_t>(item);
        r.shapes[n] = shape;
        ++n;
    }

    /* Four different colours. */
    uint8_t colours[COLOUR_COUNT] = {0, 1, 2, 3, 4, 5};
    for (uint8_t i = COLOUR_COUNT - 1; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % (i + 1));
        const uint8_t t = colours[i];
        colours[i] = colours[j];
        colours[j] = t;
    }
    for (uint8_t i = 0; i < 4; ++i) r.colours[i] = colours[i];

    /* The targets in an order that is never the rows' own. */
    for (uint8_t i = 0; i < 4; ++i) r.targetOrder[i] = i;
    do {
        for (uint8_t i = 3; i > 0; --i) {
            const uint8_t j = static_cast<uint8_t>(detail::stir(s) % (i + 1));
            const uint8_t t = r.targetOrder[i];
            r.targetOrder[i] = r.targetOrder[j];
            r.targetOrder[j] = t;
        }
    } while (r.targetOrder[0] == 0 && r.targetOrder[1] == 1 && r.targetOrder[2] == 2);

    if (l.turned) {
        for (uint8_t i = 0; i < 4; ++i) {
            const uint8_t allowed = fact(r.shapes[i]).turns;
            uint8_t choice[3];
            uint8_t c = 0;
            for (uint8_t t = 1; t <= 3; ++t) {
                if ((allowed >> t) & 1U) choice[c++] = t;
            }
            r.turns[i] = c == 0 ? 0 : choice[detail::stir(s) % c];
        }
    }
}

/* How a finished round moves the ladder: right with no wrong match, wrong
 * with two or more, and one slip says nothing either way. */
enum class Verdict : uint8_t { Right, Wrong, Neither };
inline Verdict judge(uint8_t wrongMatches) {
    if (wrongMatches == 0) return Verdict::Right;
    return wrongMatches >= 2 ? Verdict::Wrong : Verdict::Neither;
}

}   // namespace ShapesCourse
