// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/TracingCourse.h: Trace's and Cursive's stages.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/tracing_course_test.cpp -o tracing_course_test && ./tracing_course_test
//
// For both games: help only ever comes away stage by stage; every stage but
// the last can be finished; letters finished in another tab do not count
// towards a tab's stage; a player tracing the stage's own tab reaches the
// top; a stored stage is clamped; the note fits the badge.
#include "TracingCourse.h"

#include <cstdio>
#include <cstring>

using namespace TracingCourse;

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

static void game(const char* label, const Stage* stages, uint8_t count, uint8_t tabs) {
    for (uint8_t i = 0; i < count; ++i) {
        const Stage& s = stages[i];
        CHECK(s.set == ANY_SET || s.set < tabs, "%s stage %d tab", label, i + 1);
        CHECK((i + 1 == count) == (s.need == 0), "%s stage %d need", label, i + 1);
        if (i > 0) CHECK(s.aids >= stages[i - 1].aids, "%s help comes back at %d", label, i + 1);
        char buf[64];
        CHECK(note(s, buf, sizeof(buf)) < NOTE_MAX, "%s note '%s'", label, buf);
    }
    CHECK(stages[count - 1].aids == FROM_MEMORY, "%s ends from memory", label);

    /* Other tabs do not count; the stage's own tab does. */
    Progress p;
    p.start(stages, count, 0);
    CHECK(p.stage() == 1, "%s starts at 1", label);
    const uint8_t own = stages[0].set;
    for (int k = 0; k < 200; ++k) p.finished(static_cast<uint8_t>((own + 1) % tabs));
    CHECK(p.stage() == 1, "%s another tab moved stage 1", label);

    int letters = 0;
    while (!p.atTop() && letters < 2000) {
        const uint8_t set = p.current().set == ANY_SET ? 0 : p.current().set;
        const uint8_t before = p.stage();
        const bool moved = p.finished(set);
        ++letters;
        CHECK(moved == (p.stage() == before + 1), "%s moved", label);
    }
    CHECK(p.atTop() && p.stage() == count, "%s reaches the top after %d", label, letters);
    CHECK(!p.finished(0), "%s top stays", label);

    p.start(stages, count, 99);
    CHECK(p.stage() == count, "%s clamp", label);
}

int main() {
    game("Trace", TRACE_STAGES, TRACE_STAGE_COUNT, 4);
    game("Cursive", CURSIVE_STAGES, CURSIVE_STAGE_COUNT, 3);
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
