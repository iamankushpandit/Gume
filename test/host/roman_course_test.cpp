// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/RomanCourse.h: the numerals, the levels of
// Roman and the questions each one asks. The header is pure C++, and this file
// is what proves it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/roman_course_test.cpp -o roman_course_test && ./roman_course_test
//
// Every numeral 1..3999 reads back to its value; every question is inside its
// level's range and uses only the signs the level's key shows; four different
// options that fit a button, exactly one right, the additive misreading or the
// no-subtraction spelling among them when it differs; on Spot the mistake every
// wrong option is the same number written wrongly; the right answer in each
// slot about a quarter of the time; the lesson digits 4 and 9 come up more
// often than chance; and the ladder climbs to the top.
#include "RomanCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace RomanCourse;

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

/* The signs each level's key shows. */
static const char* const KEYS[LEVEL_COUNT] = {"IVX", "IVX", "IVXL", "IVXLC", "IVXLCD",
                                              "IVXLCDM", "IVXLCDM", "IVXLCDM"};

static bool onlySigns(const char* numeral, const char* allowed) {
    for (const char* p = numeral; *p != '\0'; ++p) {
        if (std::strchr(allowed, *p) == nullptr) return false;
    }
    return true;
}

static void checkOne(const Question& q, int* slot, int& total, int& lesson) {
    const uint8_t level = q.level;
    char right[TEXT_MAX];
    toRoman(q.value, right, sizeof(right));
    if (level < 7) {
        CHECK(q.value >= levelOf(level).lo && q.value <= levelOf(level).hi, "level %u range", level);
    }
    CHECK(onlySigns(right, KEYS[level - 1]), "level %u %s uses a sign not on the key", level, right);
    if (level == 8) CHECK(!q.askNumber, "spot the mistake writes");
    bool four = false;
    for (uint16_t v = q.value; v > 0; v = static_cast<uint16_t>(v / 10)) four = four || v % 10 == 4 || v % 10 == 9;
    if (four) ++lesson;
    int rightCount = 0;
    for (int i = 0; i < 4; ++i) {
        const char* o = q.options[i];
        CHECK(std::strlen(o) >= 1 && std::strlen(o) <= OPTION_MAX, "fits: %s", o);
        for (int j = i + 1; j < 4; ++j) CHECK(std::strcmp(o, q.options[j]) != 0, "distinct");
        const bool isRight = q.askNumber ? std::atoi(o) == q.value : std::strcmp(o, right) == 0;
        if (isRight) ++rightCount;
        if (level == 8 && !isRight) {
            CHECK(readValue(o) == q.value, "level 8 %s is %u written wrongly", o, q.value);
        }
    }
    CHECK(rightCount == 1, "level %u one right", level);
    CHECK(q.askNumber ? std::atoi(q.options[q.correct]) == q.value
                      : std::strcmp(q.options[q.correct], right) == 0, "correct slot");
    if (level < 8) {
        char key[TEXT_MAX];
        if (q.askNumber) {
            snprintf(key, sizeof(key), "%u", static_cast<unsigned>(readAdditively(right)));
        } else {
            toPlain(q.value, key, sizeof(key));
        }
        bool has = std::strlen(key) > OPTION_MAX;
        for (int i = 0; i < 4; ++i) has = has || std::strcmp(q.options[i], key) == 0;
        CHECK(has, "level %u the misreading %s for %u", level, key, q.value);
    }
    ++slot[q.correct];
    ++total;
}

int main() {
    for (uint16_t v = 1; v <= 3999; ++v) {
        char r[TEXT_MAX];
        toRoman(v, r, sizeof(r));
        CHECK(readValue(r) == v, "%u reads back", v);
        CHECK(std::strlen(r) <= OPTION_MAX, "%s fits", r);
    }
    char r[TEXT_MAX];
    toRoman(14, r, sizeof(r));
    CHECK(std::strcmp(r, "XIV") == 0 && readAdditively(r) == 16, "XIV");
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "line fits: %s", line);
        int slot[4] = {};
        int total = 0;
        int lesson = 0;
        Rng rng(level * 7919U);
        if (numbered(level)) {
            CHECK(poolSize(level) <= Course::QuestionDeck::MAX_ITEMS, "pool fits");
            for (uint16_t item = 0; item < poolSize(level); ++item) {
                for (int k = 0; k < 80; ++k) {
                    Question q;
                    CHECK(make(level, item, rng.next(), q), "item");
                    checkOne(q, slot, total, lesson);
                }
            }
        } else {
            for (int k = 0; k < 30000; ++k) {
                Question q;
                makeDrawn(level, rng.next(), q);
                checkOne(q, slot, total, lesson);
            }
            if (level < 8) {
                CHECK(lesson * 100 > total * 55, "level %u the fours and nines %d of %d", level, lesson, total);
            }
        }
        for (int i = 0; i < 4; ++i) {
            CHECK(slot[i] * 100 > total * 18 && slot[i] * 100 < total * 32, "level %u slot %d %d of %d",
                  level, i, slot[i], total);
        }
        std::printf("level %u %-20s pool %3u  fours and nines %d of %d\n", level, levelName(level),
                    poolSize(level), lesson, total);
    }
    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 300; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
