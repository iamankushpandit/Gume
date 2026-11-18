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

/* WHAT TRACE AND CURSIVE TEACH, STAGE BY STAGE.
 *
 * Both games opened at the first letter of the first tab on every visit and
 * remembered nothing: a player who could write every capital started again
 * at A, with every arrow and every dot showing.
 *
 * A tracing course is STAGES, not a quiz ladder: there is no wrong answer in
 * tracing, so a stage moves on letters finished and never back, and no tab
 * is ever locked -- the stage only says which tab the game opens on and how
 * much help is drawn. The first stages work through the tabs; the last two
 * take the help away: first the numbered arrows and the guide arrow, then
 * the ghost line and the far dots, leaving the caption, the start ring and
 * the next dot.
 *
 * Pure C++: test/host/tracing_course_test.cpp compiles it. */
namespace TracingCourse {

/* How much help the tracer draws. */
enum Aids : uint8_t {
    ALL_AIDS = 0,     // ghost line, every dot, numbered arrows, the guide arrow
    NO_ARROWS = 1,    // the arrows are gone
    FROM_MEMORY = 2,  // the ghost line and the far dots are gone too
};

constexpr uint8_t ANY_SET = 0xFF;

struct Stage {
    const char* name;
    uint8_t set;      // the tab it opens on and counts, or ANY_SET
    uint8_t aids;
    uint8_t need;     // finished in that tab to move on; 0 for the last stage
};

/* Trace's tabs: 0 ABC, 1 abc, 2 123, 3 Words. */
constexpr Stage TRACE_STAGES[] = {
    {"Capitals", 0, ALL_AIDS, 26},    {"Small letters", 1, ALL_AIDS, 26},
    {"Numbers", 2, ALL_AIDS, 10},     {"Words", 3, ALL_AIDS, 20},
    {"Less help", ANY_SET, NO_ARROWS, 30}, {"From memory", ANY_SET, FROM_MEMORY, 0},
};
constexpr uint8_t TRACE_STAGE_COUNT = sizeof(TRACE_STAGES) / sizeof(TRACE_STAGES[0]);

/* Cursive's tabs: 0 ABC, 1 abc, 2 Words. Small letters come first: joined
 * writing is built from them. */
constexpr Stage CURSIVE_STAGES[] = {
    {"Small letters", 1, ALL_AIDS, 26}, {"Capitals", 0, ALL_AIDS, 26},
    {"Joined words", 2, ALL_AIDS, 20},  {"Less help", ANY_SET, NO_ARROWS, 30},
    {"From memory", ANY_SET, FROM_MEMORY, 0},
};
constexpr uint8_t CURSIVE_STAGE_COUNT = sizeof(CURSIVE_STAGES) / sizeof(CURSIVE_STAGES[0]);

/* A player's stage in one of the two games. The count is of this visit only;
 * the stage is what is kept. */
class Progress {
public:
    void start(const Stage* stages, uint8_t count, uint8_t stored) {
        stages_ = stages;
        count_ = count == 0 ? 1 : count;
        stage_ = stored == 0 ? 1 : (stored > count_ ? count_ : stored);
        done_ = 0;
    }

    uint8_t stage() const { return stage_; }
    const Stage& current() const { return stages_[stage_ - 1]; }
    bool atTop() const { return stage_ >= count_; }

    /* One letter, number or word finished in tab `set`. True when that
     * moved the player on a stage. */
    bool finished(uint8_t set) {
        const Stage& s = current();
        if (atTop() || s.need == 0) return false;
        if (s.set != ANY_SET && s.set != set) return false;
        if (++done_ < s.need) return false;
        ++stage_;
        done_ = 0;
        return true;
    }

    uint8_t done() const { return done_; }

private:
    const Stage* stages_ = nullptr;
    uint8_t count_ = 1;
    uint8_t stage_ = 1;
    uint8_t done_ = 0;
};

constexpr uint8_t NOTE_MAX = 24;
/* What the "Great job" badge says on the letter that moved the player on. */
inline int note(const Stage& next, char* out, size_t len) {
    return snprintf(out, len, "Next: %s", next.name);
}

}  // namespace TracingCourse
