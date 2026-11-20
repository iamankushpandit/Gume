// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "GreWordTable.h"

/* WHAT GRE WORDS TEACHES, LEVEL BY LEVEL.
 *
 * The quiz asked one thing -- a word, pick its meaning -- with three other
 * meanings drawn from anywhere, so a verb's gloss could often be struck out
 * against an adjective, and there was no level at all.
 *
 * This course recognises a meaning with the wrong ones a different part of
 * speech where it can; tells meanings apart, all four the same part of
 * speech; recalls the word from its meaning; finds the word that fits the
 * table's own sentence, with the word blanked; tells a word from its
 * look-alikes, the nearest words in the alphabet; and mixes all of them.
 *
 * Pure C++, no Arduino: test/host/gre_course_test.cpp compiles this file and
 * the word table. Randomness comes in as a number. */
namespace GreCourse {

constexpr uint8_t LEVEL_COUNT = 6;

enum class Kind : uint8_t {
    Meaning,    // the word; pick its meaning
    Recall,     // the meaning; pick the word
    Sentence,   // the sentence with a gap; pick the word
};

struct Level {
    const char* name;
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Recognise"}, {"Tell apart"}, {"Recall"}, {"In a sentence"}, {"Look-alikes"}, {"Mastery mix"},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const char* levelName(uint8_t level) { return LEVELS[clampLevel(level) - 1].name; }

constexpr uint8_t LEVEL_LINE_MAX = 24;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* The sentence with the word taken out: the whole token that starts with the
 * word's first four letters (so "acquiesced" goes too, and the ending gives
 * nothing away). False when the sentence does not use the word. */
inline bool blank(const char* example, const char* word, char* out, size_t len) {
    if (example == nullptr || word == nullptr || len == 0) return false;
    const size_t stem = strlen(word) < 4 ? strlen(word) : 4;
    const size_t n = strlen(example);
    for (size_t i = 0; i + stem <= n; ++i) {
        if (i > 0 && isalpha(static_cast<unsigned char>(example[i - 1]))) continue;
        bool same = true;
        for (size_t k = 0; k < stem && same; ++k) {
            same = tolower(static_cast<unsigned char>(example[i + k])) ==
                   tolower(static_cast<unsigned char>(word[k]));
        }
        if (!same) continue;
        size_t end = i;
        while (end < n && (isalpha(static_cast<unsigned char>(example[end])) || example[end] == '-')) ++end;
        snprintf(out, len, "%.*s____%s", static_cast<int>(i), example, example + end);
        return true;
    }
    return false;
}

inline bool hasSentence(uint16_t word) {
    char tmp[160];
    return blank(GRE_WORDS[word].example, GRE_WORDS[word].word, tmp, sizeof(tmp));
}

/* Which kind of question `word` gets at `level`. Level 6 draws among the
 * three; a word whose sentence does not use it is asked for its meaning. */
inline Kind kindFor(uint8_t level, uint16_t word, uint32_t rnd) {
    Kind k;
    switch (clampLevel(level)) {
        case 3: k = Kind::Recall; break;
        case 4: k = Kind::Sentence; break;
        case 6: k = static_cast<Kind>(rnd % 3); break;
        default: k = Kind::Meaning; break;
    }
    if (k == Kind::Sentence && !hasSentence(word)) k = Kind::Meaning;
    return k;
}

namespace detail {
inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
inline bool samePos(uint16_t a, uint16_t b) { return strcmp(GRE_WORDS[a].pos, GRE_WORDS[b].pos) == 0; }
inline int dist(uint16_t a, uint16_t b) { return a > b ? a - b : b - a; }
}  // namespace detail

/* Rules for a wrong option at this level. Strength 0 is the level's rule;
 * the weaker strengths are used only if the stronger find nothing. */
inline bool fits(uint8_t level, uint16_t answer, uint16_t cand, uint8_t strength) {
    level = clampLevel(level);
    if (strength >= 2) return true;
    if (level == 1) return strength == 1 || !detail::samePos(answer, cand);
    if (level == 5 || level == 6) {
        if (strength == 0) return detail::dist(answer, cand) <= 8 && detail::samePos(answer, cand);
        return detail::dist(answer, cand) <= 8;
    }
    return detail::samePos(answer, cand);
}

/* Four words for the buttons, the answer at `correct`. */
inline void options(uint8_t level, uint16_t answer, uint32_t seed, uint16_t out[4], uint8_t& correct) {
    uint32_t s = seed == 0 ? 0x6C8E9CF5u : seed;
    correct = static_cast<uint8_t>(detail::stir(s) % 4);
    for (uint8_t i = 0; i < 4; ++i) out[i] = answer;
    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correct) continue;
        bool placed = false;
        for (uint8_t strength = 0; strength < 3 && !placed; ++strength) {
            for (uint8_t tries = 0; tries < 80 && !placed; ++tries) {
                const uint16_t c = static_cast<uint16_t>(detail::stir(s) % GRE_WORD_COUNT);
                if (c == answer || !fits(level, answer, c, strength)) continue;
                bool taken = false;
                for (uint8_t j = 0; j < 4; ++j) taken = taken || (j != i && out[j] == c);
                if (taken) continue;
                /* Two words with the same gloss would make two right answers. */
                if (strcmp(GRE_WORDS[c].meaning, GRE_WORDS[answer].meaning) == 0) continue;
                out[i] = c;
                placed = true;
            }
        }
    }
}

}  // namespace GreCourse
