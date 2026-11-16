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

/* WHAT WHACK A MOLE TEACHES, LEVEL BY LEVEL.
 *
 * The mole's speed came from the hits of this run, so every run -- every
 * visit, every restart -- began at the same pace and only ever got faster
 * within it. A practised player waited through the slow part every time,
 * and a young one met level 10 if their run went well.
 *
 * Here the speed is the player's: one answer per mole, a hit right and a
 * miss or a mole let go wrong, so it settles where the player hits about two
 * in three and is still there next visit. From level 6 one mole in five is a
 * decoy that frowns, and must be left alone: pressing it is wrong, letting
 * it go is right -- looking before pressing is the skill those levels add.
 *
 * Pure C++: test/host/whack_course_test.cpp compiles it. */
namespace WhackCourse {

constexpr uint8_t LEVEL_COUNT = 10;

struct Level {
    const char* name;
    uint16_t visibleMs;   // how long a mole stays up
    uint8_t decoyOneIn;   // 0: no decoys; n: one mole in n frowns
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Slow", 1500, 0},          {"Steady", 1300, 0},       {"Quick", 1150, 0},
    {"Quicker", 1000, 0},       {"Fast", 880, 0},          {"Decoys", 880, 5},
    {"Decoys, quicker", 760, 5}, {"Decoys, fast", 660, 5}, {"Decoys, faster", 560, 5},
    {"Fastest", 480, 5},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const Level& levelOf(uint8_t level) { return LEVELS[clampLevel(level) - 1]; }
inline uint16_t visibleMs(uint8_t level) { return levelOf(level).visibleMs; }

constexpr uint8_t LEVEL_LINE_MAX = 25;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelOf(level).name);
}

/* Is the mole about to come up a decoy? `rnd` is any random number. */
inline bool decoy(uint8_t level, uint32_t rnd) {
    const uint8_t n = levelOf(level).decoyOneIn;
    return n != 0 && rnd % n == 0;
}

/* What a mole says to the ladder: +1 right, -1 wrong. */
enum class Outcome : uint8_t { Hit, Missed, WrongCell, DecoyPressed, DecoyLeft };
inline bool right(Outcome o) { return o == Outcome::Hit || o == Outcome::DecoyLeft; }

}  // namespace WhackCourse
