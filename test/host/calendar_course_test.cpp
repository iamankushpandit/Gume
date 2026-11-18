// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/CalendarCourse.h: the two tracks of Calendar,
// their levels and the questions each asks. The header is pure C++, and this
// file is what proves it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/calendar_course_test.cpp -o calendar_course_test && ./calendar_course_test
//
// For every track, level and item, under many random streams: the answer is
// right, worked out here a second way from the prompt and the subject; four
// different tiles, exactly one right, the question's commonest mistake among
// them, and the right one in each slot about a quarter of the time. The
// level's questions are all different, the join of the week and the year is
// asked, and the ladder climbs to the top of both tracks.
#include "CalendarCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

using namespace CalendarCourse;

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            if (failures < 40) {                                              \
                std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
                std::printf(__VA_ARGS__);                                     \
                std::printf("\n");                                            \
            }                                                                 \
        }                                                                     \
    } while (0)

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

static int indexOf(const char* const* names, int n, const char* name) {
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(names[i], name) == 0) return i;
    }
    return -1;
}

static bool has(const Question& q, int v) {
    for (int i = 0; i < 4; ++i) {
        if (q.options[i] == v) return true;
    }
    return false;
}

/* The answer to a "N days AFTER:" question, worked out from the words. */
static int stepAnswer(const Question& q, int n, int& subject, int& step, bool& after) {
    const char* const* names = q.track == DAYS ? DAY_NAMES : MONTH_NAMES;
    subject = indexOf(names, n, q.subject);
    after = std::strstr(q.prompt, "AFTER") != nullptr;
    step = std::strncmp(q.prompt, "What comes", 10) == 0 ? 1 : std::atoi(q.prompt);
    return (((subject + (after ? step : -step)) % n) + n) % n;
}

int main() {
    CHECK(clampLevel(DAYS, 0) == 1 && clampLevel(DAYS, 99) == DAY_LEVELS, "clamp days");
    CHECK(clampLevel(MONTHS, 99) == MONTH_LEVELS, "clamp months");
    bool joinDays = false;
    bool joinMonths = false;
    for (uint8_t track = 0; track < TRACKS; ++track) {
        const int n = track == DAYS ? 7 : 12;
        for (uint8_t level = 1; level <= levelCount(track); ++level) {
            const uint16_t pool = poolSize(track, level);
            CHECK(pool > 0 && pool <= Course::QuestionDeck::MAX_ITEMS, "pool");
            char line[64];
            CHECK(levelLine(track, level, line, sizeof(line)) <= LEVEL_LINE_MAX, "line fits: %s", line);
            std::set<std::string> seen;
            int slot[4] = {};
            int total = 0;
            Rng rng(track * 1000003U + level * 7919U);
            for (uint16_t item = 0; item < pool; ++item) {
                Question first;
                CHECK(make(track, level, item, rng.next(), first), "%u/%u item %u", track, level, item);
                seen.insert(std::string(first.prompt) + "|" + first.subject);
                for (int k = 0; k < 48; ++k) {
                    Question q;
                    make(track, level, item, rng.next(), q);
                    CHECK(q.answer == first.answer && std::strcmp(q.subject, first.subject) == 0,
                          "stable");
                    CHECK(std::strlen(q.prompt) <= PROMPT_MAX && std::strlen(q.subject) <= SUBJECT_MAX,
                          "fits: %s / %s", q.prompt, q.subject);
                    const bool counting = std::strstr(q.prompt, "AFTER") || std::strstr(q.prompt, "BEFORE");
                    if (counting) {
                        int subject = 0;
                        int step = 0;
                        bool after = true;
                        const int want = stepAnswer(q, n, subject, step, after);
                        CHECK(subject >= 0 && q.answer == want, "%u/%u %s %s", track, level, q.prompt,
                              q.subject);
                        const int sign = after ? 1 : -1;
                        if (step == 1) {
                            CHECK(has(q, ((subject - sign) % n + n) % n), "the other neighbour");
                        } else {
                            const int counted = ((subject + sign * (step - 1)) % n + n) % n;
                            if (counted != q.answer) CHECK(has(q, counted), "the start counted");
                        }
                        const bool wraps = after ? subject + step >= n : subject - step < 0;
                        if (level == 1) CHECK(!wraps, "level 1 does not wrap");
                        if (wraps && track == DAYS) joinDays = true;
                        if (wraps && track == MONTHS) joinMonths = true;
                        if (track == DAYS && level == 3) CHECK(step == 2 || step == 3, "two or three");
                    } else if (std::strcmp(q.prompt, "What number is") == 0) {
                        CHECK(q.answer == indexOf(MONTH_NAMES, 12, q.subject) + 1, "month number");
                        CHECK(has(q, q.answer - 1) || has(q, q.answer + 1), "one out");
                    } else if (std::strcmp(q.prompt, "Which month is number") == 0) {
                        CHECK(q.answer == std::atoi(q.subject) - 1, "which month");
                    } else if (level == 4 && track == MONTHS) {
                        const int m = indexOf(MONTH_NAMES, 12, q.subject);
                        if (m >= 0) {
                            CHECK(q.answer == MONTH_DAYS[m], "days in %s", q.subject);
                        }
                        if (std::strstr(q.subject, "leap year") && std::strstr(q.subject, "February")) {
                            CHECK(q.answer == 29, "leap February");
                        }
                        if (q.answer >= 28 && q.answer <= 31 && std::strstr(q.prompt, "days in") &&
                            std::strncmp(q.subject, "a ", 2) != 0) {
                            for (int i = 0; i < 4; ++i) {
                                CHECK(q.options[i] >= 28 && q.options[i] <= 31, "always 28 to 31");
                            }
                        }
                    } else {
                        CHECK(false, "unknown prompt %s", q.prompt);
                    }

                    int right = 0;
                    for (int i = 0; i < 4; ++i) {
                        char a[32];
                        const int len = format(q.label, q.options[i], a, sizeof(a));
                        /* Tiles are 152px at font 2: "September" is the longest. */
                        CHECK(len > 0 && len <= 9, "tile fits: %s", a);
                        for (int j = i + 1; j < 4; ++j) {
                            char b[32];
                            format(q.label, q.options[j], b, sizeof(b));
                            CHECK(std::strcmp(a, b) != 0, "%u/%u distinct %s", track, level, a);
                        }
                        if (q.options[i] == q.answer) ++right;
                    }
                    CHECK(right == 1 && q.options[q.correct] == q.answer, "one right");
                    ++slot[q.correct];
                    ++total;
                }
            }
            CHECK(static_cast<int>(seen.size()) == pool, "%u/%u: %zu different of %u", track, level,
                  seen.size(), pool);
            Question none;
            CHECK(!make(track, level, pool, 1, none), "past the pool");
            for (int i = 0; i < 4; ++i) {
                CHECK(slot[i] * 100 > total * 18 && slot[i] * 100 < total * 32,
                      "%u/%u slot %d holds %d of %d", track, level, i, slot[i], total);
            }
            std::printf("%-6s level %u %-18s pool %3u\n", track == DAYS ? "days" : "months", level,
                        levelName(track, level), pool);
        }
        Course::Ladder ladder;
        ladder.start(levelCount(track), 0);
        for (int i = 0; i < 200; ++i) ladder.answer(true);
        CHECK(ladder.level() == levelCount(track), "climbs to the top");
    }
    CHECK(joinDays && joinMonths, "the join is asked");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
