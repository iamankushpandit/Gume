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

/* WHAT CINNAMON SAYS TEACHES, LEVEL BY LEVEL.
 *
 * Four pads light in an order and the player repeats it. The game started
 * every run at one step at one pace, so the first rounds of every visit were
 * trivial for a practised player, and nothing was harder for them than for a
 * beginner; at 32 steps it quietly stopped growing.
 *
 * This course grows the sequence as before, then shows it quicker; then
 * deals a new sequence every round, so the tune cannot be learned by heart;
 * then asks for it backwards, last pad first.
 *
 * A round repeated correctly at four steps or more is right. A run that ends
 * before it reaches four steps, or on the level's first round, is wrong. A run that ends later says nothing:
 * everybody's run ends somewhere. Thirty-two steps ends the run as a win.
 *
 * The pace never reaches three flashes a second: a pad is about a twelfth of
 * the panel and the screen is for young players.
 *
 * Pure C++, no Arduino: test/host/cinnamon_course_test.cpp compiles this
 * file as it is. */
namespace CinnamonCourse {

constexpr uint8_t LEVEL_COUNT = 6;
constexpr uint8_t MAX_STEPS = 32;
/* A round shorter than this is too easy to say anything about the player. */
constexpr uint8_t COUNTS_FROM = 4;

struct Level {
    const char* name;
    uint8_t startLength;
    uint16_t onMs;
    uint16_t offMs;
    bool fresh;       // a new sequence every round, rather than one more step
    bool backwards;   // repeat it last pad first
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Follow", 1, 600, 450, false, false},
    {"Quicker", 2, 450, 300, false, false},
    {"New every time", 3, 500, 350, true, false},
    {"New and quick", 4, 350, 250, true, false},
    {"Backwards", 2, 600, 450, false, true},
    {"Backwards, new", 3, 450, 300, true, true},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const Level& levelOf(uint8_t level) { return LEVELS[clampLevel(level) - 1]; }
inline const char* levelName(uint8_t level) { return levelOf(level).name; }

constexpr uint8_t LEVEL_LINE_MAX = 30;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* The pad the player owes for tap `index` of a sequence `length` long. */
inline uint8_t expectedPad(uint8_t level, const uint8_t* sequence, uint8_t length,
                           uint8_t index) {
    if (length == 0) return 0;
    if (index >= length) index = static_cast<uint8_t>(length - 1);
    return levelOf(level).backwards ? sequence[length - 1 - index] : sequence[index];
}

/* A round repeated correctly at `length` steps: +1 when it counts, 0 when it
 * is too short to. */
inline int8_t judgeRepeated(uint8_t length) { return length >= COUNTS_FROM ? 1 : 0; }

/* A run that ended on a mistake in a round `length` steps long: wrong when
 * it was short, or when it was the level's very first round -- otherwise a
 * level starting at four steps could never send anybody down. */
inline int8_t judgeFailed(uint8_t level, uint8_t length) {
    return (length < COUNTS_FROM || length <= levelOf(level).startLength) ? -1 : 0;
}

/* The best length is a forwards span; a backwards five is not the same feat
 * as a forwards five, so the backwards levels do not keep it. */
inline bool keepsBest(uint8_t level) { return !levelOf(level).backwards; }

constexpr uint8_t STATUS_MAX = 32;
/* "Repeat 3/5", "Repeat backwards 3/5". */
inline int repeatLine(uint8_t level, uint8_t done, uint8_t length, char* out, size_t len) {
    return snprintf(out, len, "Repeat%s %u/%u", levelOf(level).backwards ? " backwards" : "",
                    static_cast<unsigned>(done + 1), static_cast<unsigned>(length));
}

}  // namespace CinnamonCourse
