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

/* WHAT FLAGS TEACHES, LEVEL BY LEVEL.
 *
 * The game had three tiers -- the thirty most familiar countries, then sixty,
 * then all 195 at once -- climbed by six right in a row and never come down
 * from. Its continent column was in flash and never read.
 *
 * This course takes the famous flags, then the well known, then one
 * continent at a time, with every wrong answer from the same continent, so a
 * flag is told from its neighbours rather than from a flag on the other side
 * of the world; then the whole world; then the whole world with the capital
 * counting as much as the flag.
 *
 * Flags that cannot be told apart at 80x60 -- Indonesia and Monaco, Chad and
 * Romania -- are never offered against each other.
 *
 * Pure C++, no Arduino: test/host/flag_course_test.cpp compiles this file
 * and the generated country table. The continent numbers are CountryData.h's
 * CONT_*, and FlagGame.cpp static_asserts that they agree. */
namespace FlagCourse {

constexpr uint8_t LEVEL_COUNT = 9;

constexpr uint8_t AFRICA = 0;
constexpr uint8_t ASIA = 1;
constexpr uint8_t EUROPE = 2;
constexpr uint8_t NORTH_AMERICA = 3;
constexpr uint8_t SOUTH_AMERICA = 4;
constexpr uint8_t OCEANIA = 5;

struct Level {
    const char* name;
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Famous flags"}, {"Well-known flags"}, {"Europe"},
    {"The Americas"}, {"Asia"},             {"Oceania"},
    {"Africa"},       {"The whole world"},  {"World and capitals"},
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

inline bool american(uint8_t continent) {
    return continent == NORTH_AMERICA || continent == SOUTH_AMERICA;
}

/* Is a country of this tier and continent one the level asks about? */
inline bool asks(uint8_t level, uint8_t tier, uint8_t continent) {
    switch (clampLevel(level)) {
        case 1: return tier == 1;
        case 2: return tier == 2;
        case 3: return continent == EUROPE;
        case 4: return american(continent);
        case 5: return continent == ASIA;
        case 6: return continent == OCEANIA;
        case 7: return continent == AFRICA;
        default: return true;
    }
}

/* May a country be offered as a wrong answer beside one from `answerContinent`?
 * Level 2 takes them from the famous flags too; level 9 from the answer's
 * own continent. */
inline bool offers(uint8_t level, uint8_t answerContinent, uint8_t tier, uint8_t continent) {
    switch (clampLevel(level)) {
        case 1: return tier == 1;
        case 2: return tier <= 2;
        case 8: return true;
        case 9:
            return american(answerContinent) ? american(continent) : continent == answerContinent;
        default: return asks(level, tier, continent);
    }
}

/* On level 9 the capital is what moves the ladder (after a right flag); on
 * every other level the flag does and the capital is a bonus. */
inline bool capitalCounts(uint8_t level) { return clampLevel(level) == LEVEL_COUNT; }

/* Flags that are the same picture at the size this screen draws them. */
constexpr const char* LOOKALIKES[][2] = {
    {"ID", "MC"}, {"ID", "PL"}, {"MC", "PL"}, {"TD", "RO"}, {"IE", "CI"},
    {"NL", "LU"}, {"AU", "NZ"}, {"GN", "ML"}, {"CO", "EC"}, {"SN", "ML"},
};
constexpr uint8_t LOOKALIKE_COUNT = sizeof(LOOKALIKES) / sizeof(LOOKALIKES[0]);

inline bool lookalike(const char* a, const char* b) {
    for (uint8_t i = 0; i < LOOKALIKE_COUNT; ++i) {
        if ((strcmp(a, LOOKALIKES[i][0]) == 0 && strcmp(b, LOOKALIKES[i][1]) == 0) ||
            (strcmp(a, LOOKALIKES[i][1]) == 0 && strcmp(b, LOOKALIKES[i][0]) == 0)) {
            return true;
        }
    }
    return false;
}

}  // namespace FlagCourse
