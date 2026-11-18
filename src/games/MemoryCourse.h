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

#include "engine/Course.h"

/* WHAT MEMORY TEACHES, LEVEL BY LEVEL.
 *
 * The game dealt one board, 24 cards, with the same twelve letters A to L on
 * every board for ever. Nothing started small and nothing grew.
 *
 * This course starts with six cards and grows a size at a time to the full
 * board of 24; then the full board with faces chosen to be confused (O and Q,
 * 6 and 9, S and 5); then pairs whose two cards are not the same picture but
 * belong together -- a capital and its small letter, a number and a sum that
 * makes it.
 *
 * A board is the answer. Cleared in no more than two moves a pair is right
 * (remembering every card seen averages about 1.6); more than three a pair is
 * wrong; in between moves the ladder neither way.
 *
 * Faces come from a Course::QuestionDeck over the level's symbols, so the
 * next boards use faces not yet seen before any comes back.
 *
 * Pure C++, no Arduino and no heap: test/host/memory_course_test.cpp
 * compiles this file as it is. Randomness comes in as a number. */
namespace MemoryCourse {

constexpr uint8_t LEVEL_COUNT = 8;
constexpr uint8_t MAX_PAIRS = 12;
constexpr uint8_t MAX_CARDS = MAX_PAIRS * 2;
/* The longest face: "4+5". */
constexpr uint8_t FACE_MAX = 4;

struct Level {
    const char* name;
    uint8_t rows;
    uint8_t cols;
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Six cards", 2, 3},     {"Eight cards", 2, 4}, {"Twelve cards", 3, 4},
    {"Sixteen cards", 4, 4}, {"Twenty cards", 4, 5}, {"Full board", 4, 6},
    {"Lookalikes", 4, 6},    {"Belong together", 4, 6},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const Level& levelOf(uint8_t level) { return LEVELS[clampLevel(level) - 1]; }
inline const char* levelName(uint8_t level) { return levelOf(level).name; }
inline uint8_t pairsFor(uint8_t level) {
    return static_cast<uint8_t>(levelOf(level).rows * levelOf(level).cols / 2);
}

constexpr uint8_t LEVEL_LINE_MAX = 30;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* The best (fewest moves) is a number about one board size; only the full
 * board keeps it, or it would be 3 for everybody after level 1. */
inline bool keepsBest(uint8_t level) { return levelOf(level).rows * levelOf(level).cols == 24; }

/* Levels 1-6: any letter or digit. */
constexpr const char* PLAIN = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
constexpr uint8_t PLAIN_COUNT = 36;
/* Level 7: families that are confused with one another -- O Q C G D, E F,
 * M W N, B 8, S 5, Z 2, 6 9, I 1. */
constexpr const char* LOOKALIKE = "OQCGDEFMWNB8S5Z269I1";
constexpr uint8_t LOOKALIKE_COUNT = 20;
/* Level 8: capitals whose small letter is not the same shape made smaller
 * (c, o, s, u, v, w, x, z are left out: they would be ordinary pairs), then
 * the numbers 2 to 9, each matched with a sum. */
constexpr const char* BELONG_UPPER = "ABDEFGHIJLMNQRTY";
constexpr const char* BELONG_LOWER = "abdefghijlmnqrty";
constexpr uint8_t BELONG_LETTERS = 16;
constexpr uint8_t BELONG_COUNT = BELONG_LETTERS + 8;

inline uint16_t poolSize(uint8_t level) {
    switch (clampLevel(level)) {
        case 7: return LOOKALIKE_COUNT;
        case 8: return BELONG_COUNT;
        default: return PLAIN_COUNT;
    }
}

/* What card `half` (0 or 1) of the pair dealt as `item` shows. `variant`
 * picks the sum on level 8 and is ignored elsewhere. */
inline void face(uint8_t level, uint8_t item, uint8_t half, uint8_t variant, char* out,
                 size_t len) {
    if (len == 0) return;
    level = clampLevel(level);
    if (level <= 6) {
        snprintf(out, len, "%c", PLAIN[item % PLAIN_COUNT]);
    } else if (level == 7) {
        snprintf(out, len, "%c", LOOKALIKE[item % LOOKALIKE_COUNT]);
    } else if (item < BELONG_LETTERS) {
        snprintf(out, len, "%c", half == 0 ? BELONG_UPPER[item] : BELONG_LOWER[item]);
    } else {
        const unsigned n = static_cast<unsigned>(item - BELONG_LETTERS) + 2;   // 2..9
        if (half == 0) {
            snprintf(out, len, "%u", n);
        } else {
            const unsigned a = 1 + variant % (n - 1);                           // 1..n-1
            snprintf(out, len, "%u+%u", a, n - a);
        }
    }
}

struct Board {
    uint8_t level = 1;
    uint8_t rows = 2;
    uint8_t cols = 3;
    uint8_t pairs = 3;
    uint8_t item[MAX_PAIRS] = {};
    uint8_t variant[MAX_PAIRS] = {};
    /* card i shows half (cards[i] & 1) of pair (cards[i] >> 1). */
    uint8_t cards[MAX_CARDS] = {};

    uint8_t total() const { return static_cast<uint8_t>(rows * cols); }
    uint8_t pairOf(uint8_t card) const { return static_cast<uint8_t>(cards[card] >> 1); }
    void faceOf(uint8_t card, char* out, size_t len) const {
        const uint8_t p = pairOf(card);
        face(level, item[p], static_cast<uint8_t>(cards[card] & 1), variant[p], out, len);
    }
};

namespace detail {
inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
}  // namespace detail

/* A board for `level`, faces from `deck` (reset to poolSize(level) by the
 * caller whenever the level changes). */
inline void deal(uint8_t level, Course::QuestionDeck& deck, uint32_t seed, Board& b) {
    uint32_t s = seed == 0 ? 0x9E3779B9u : seed;
    b.level = clampLevel(level);
    b.rows = levelOf(b.level).rows;
    b.cols = levelOf(b.level).cols;
    b.pairs = pairsFor(b.level);
    for (uint8_t p = 0; p < b.pairs; ++p) {
        uint8_t it = 0;
        /* The deck can hand back a face already on this board when a pass
         * turns over mid-board; take another. */
        for (uint8_t tries = 0; tries < 40; ++tries) {
            it = static_cast<uint8_t>(deck.next(detail::stir(s)));
            bool clash = false;
            for (uint8_t q = 0; q < p; ++q) clash = clash || b.item[q] == it;
            if (!clash) break;
        }
        b.item[p] = it;
        b.variant[p] = static_cast<uint8_t>(detail::stir(s) & 0xFF);
    }
    const uint8_t total = b.total();
    for (uint8_t i = 0; i < total; ++i) b.cards[i] = i;
    for (int i = total - 1; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(detail::stir(s) % static_cast<uint32_t>(i + 1));
        const uint8_t t = b.cards[i];
        b.cards[i] = b.cards[j];
        b.cards[j] = t;
    }
}

/* What a cleared board says to the ladder: +1 right, -1 wrong, 0 neither. */
inline int8_t judge(uint8_t pairs, uint16_t moves) {
    if (moves <= static_cast<uint16_t>(pairs) * 2) return 1;
    if (moves > static_cast<uint16_t>(pairs) * 3) return -1;
    return 0;
}

}  // namespace MemoryCourse
