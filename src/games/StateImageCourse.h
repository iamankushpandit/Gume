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

/* WHAT STATE FLAGS AND STATE MAPS TEACH, LEVEL BY LEVEL.
 *
 * Both games show a picture of a state -- its flag, or its outline -- and
 * ask which state it is, with the capital as a bonus after a right answer.
 * Both had three tiers climbed by six right in a row and never come down
 * from, and dealt from the pool's POSITIONS, so Easy was the same nine
 * questions in a loop.
 *
 * This course asks the nine famous states, then the next eighteen, then the
 * last twenty-three on their own, then all fifty, then all fifty with the
 * capital counting as much as the picture.
 *
 * Pure C++, no Arduino: test/host/state_image_course_test.cpp compiles this
 * file and the real state table. */
namespace StateImageCourse {

constexpr uint8_t LEVEL_COUNT = 5;

struct Level {
    const char* name;
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Nine famous states"}, {"Eighteen more"}, {"The last 23"}, {"All fifty"},
    {"Fifty and capitals"},
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

/* Is a state of this tier one the level asks about? */
inline bool asks(uint8_t level, uint8_t tier) {
    level = clampLevel(level);
    return level <= 3 ? tier == level : true;
}

/* May a state of this tier be a wrong answer at this level? The first level
 * keeps to the famous nine, the second takes them too, the third the less
 * famous half; from the fourth, anywhere. */
inline bool offers(uint8_t level, uint8_t tier) {
    switch (clampLevel(level)) {
        case 1: return tier == 1;
        case 2: return tier <= 2;
        case 3: return tier >= 2;
        default: return true;
    }
}

/* On the last level the capital is what moves the ladder (after a right
 * picture); before it, the picture does and the capital is a bonus. */
inline bool capitalCounts(uint8_t level) { return clampLevel(level) == LEVEL_COUNT; }

/* Pictures that cannot be told apart at this size, by postal code. Outlines:
 * Colorado and Wyoming are both a plain rectangle. */
constexpr const char* OUTLINE_LOOKALIKES[][2] = {{"CO", "WY"}};
constexpr uint8_t OUTLINE_LOOKALIKE_COUNT = 1;

inline bool outlineLookalike(const char* a, const char* b) {
    for (uint8_t i = 0; i < OUTLINE_LOOKALIKE_COUNT; ++i) {
        if ((strcmp(a, OUTLINE_LOOKALIKES[i][0]) == 0 && strcmp(b, OUTLINE_LOOKALIKES[i][1]) == 0) ||
            (strcmp(a, OUTLINE_LOOKALIKES[i][1]) == 0 && strcmp(b, OUTLINE_LOOKALIKES[i][0]) == 0)) {
            return true;
        }
    }
    return false;
}

}  // namespace StateImageCourse
