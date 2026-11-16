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

/* WHAT US STATES TEACHES, LEVEL BY LEVEL.
 *
 * The game asked a state's capital, or a capital's state, at random, from
 * three tiers climbed by six right in a row and never come down from. The
 * postal codes were in the table and never asked.
 *
 * This course asks the nine famous states' capitals, then the same nine the
 * other way round; the next eighteen; those twenty-seven both ways; the last
 * twenty-three; all fifty both ways; the postal codes, both ways, with wrong
 * answers sharing the code's first letter where they can (the eight M
 * states); and last everything mixed.
 *
 * Pure C++, no Arduino: test/host/states_course_test.cpp compiles this file
 * and the real state table. */
namespace StatesCourse {

constexpr uint8_t LEVEL_COUNT = 8;

/* Which way a question goes. */
enum class Dir : uint8_t {
    CapitalOf,     // Texas -> Austin
    WhichState,    // Austin -> Texas
    CodeOf,        // Texas -> TX
    StateOfCode,   // TX -> Texas
};

constexpr uint8_t CAPITALS = 0x03;   // both capital directions
constexpr uint8_t CODES = 0x0C;      // both code directions

struct Level {
    const char* name;
    uint8_t tiers;   // bit (tier - 1) set: asked at this level
    uint8_t dirs;    // bit Dir set
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Nine capitals", 0x1, 0x1},          {"Nine, the other way", 0x1, 0x2},
    {"Eighteen more", 0x2, 0x1},          {"Twenty-seven, both ways", 0x3, CAPITALS},
    {"The last 23", 0x4, 0x1},            {"All fifty, both ways", 0x7, CAPITALS},
    {"Postal codes", 0x7, CODES},         {"Everything", 0x7, CAPITALS | CODES},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const Level& levelOf(uint8_t level) { return LEVELS[clampLevel(level) - 1]; }
inline const char* levelName(uint8_t level) { return levelOf(level).name; }

constexpr uint8_t LEVEL_LINE_MAX = 32;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

inline bool asks(uint8_t level, uint8_t tier) {
    return tier >= 1 && tier <= 3 && (levelOf(level).tiers & (1u << (tier - 1))) != 0;
}

inline uint8_t dirCount(uint8_t level) {
    uint8_t n = 0;
    for (uint8_t d = 0; d < 4; ++d) n = static_cast<uint8_t>(n + ((levelOf(level).dirs >> d) & 1));
    return n;
}

/* The k-th direction of the level. */
inline Dir dirAt(uint8_t level, uint8_t k) {
    for (uint8_t d = 0; d < 4; ++d) {
        if ((levelOf(level).dirs >> d) & 1) {
            if (k == 0) return static_cast<Dir>(d);
            --k;
        }
    }
    return Dir::CapitalOf;
}

/* A deck item is (state of the level, direction): item = k * dirCount + d. */
inline uint16_t poolSize(uint8_t level, uint8_t statesAsked) {
    return static_cast<uint16_t>(statesAsked * dirCount(level));
}

/* May a state of this tier be a wrong answer? The first levels keep to what
 * the player has met; from level 6, anywhere. */
inline bool offers(uint8_t level, uint8_t tier) {
    switch (clampLevel(level)) {
        case 1: case 2: return tier == 1;
        case 3: case 4: return tier <= 2;
        case 5: return tier >= 2;
        default: return true;
    }
}

/* Close wrong answers: from level 7 a wrong answer sharing the first letter
 * of the code (or, for a capital, of the state) is tried first. */
inline bool prefersClose(uint8_t level) { return clampLevel(level) >= 7; }

/* The question under the subject. */
inline const char* question(Dir d) {
    switch (d) {
        case Dir::CapitalOf: return "What is its capital?";
        case Dir::WhichState: return "is the capital of which state?";
        case Dir::CodeOf: return "What is its postal code?";
        case Dir::StateOfCode: default: return "is the postal code of which state?";
    }
}

/* What is shown big, and what the buttons say, picked from a state's three
 * fields. */
inline const char* subject(Dir d, const char* name, const char* capital, const char* code) {
    switch (d) {
        case Dir::WhichState: return capital;
        case Dir::StateOfCode: return code;
        default: return name;
    }
}
inline const char* label(Dir d, const char* name, const char* capital, const char* code) {
    switch (d) {
        case Dir::CapitalOf: return capital;
        case Dir::CodeOf: return code;
        default: return name;
    }
}

}  // namespace StatesCourse
