// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/SortCourse.h: the boards Sorting deals at each
// level. The header is pure C++, and this file is what proves it, since it
// will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/sort_course_test.cpp -o sort_course_test && ./sort_course_test
//
// For every level, over many boards dealt one after another as the game deals
// them: the right number of tiles, no two equal in value, every label fitting
// a tile, the tapping order really in order, each level's numbers of the kind
// it promises (look-alikes, the same three digits, a number and its negative,
// hundredths beside their tenth), both directions about equally from level 3
// and never three rounds the same way, and the ladder climbing to the top.
#include "SortCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace SortCourse;

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

int main() {
    char t[16];
    Tile x;
    x.n = 45;
    x.d = 100;
    x.kind = Kind::Decimal;
    text(x, t, sizeof(t));
    CHECK(std::strcmp(t, "0.45") == 0, "0.45: %s", t);
    x.n = 50;
    text(x, t, sizeof(t));
    CHECK(std::strcmp(t, "0.5") == 0, "0.5: %s", t);
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "line fits: %s", line);
        Rng rng(level * 7919U);
        uint8_t history = 0;
        int up = 0;
        int run = 0;
        bool lastUp = true;
        const int rounds = 30000;
        for (int k = 0; k < rounds; ++k) {
            Round r;
            deal(level, rng.next(), history, r);
            history = remember(history, r.ascending);
            CHECK(r.level == level && r.count >= 3 && r.count <= MAX_TILES, "count");
            if (r.ascending) ++up;
            run = (k > 0 && r.ascending == lastUp) ? run + 1 : 1;
            lastUp = r.ascending;
            if (level >= 3) CHECK(run <= 2, "level %u three the same way", level);
            if (level <= 2) CHECK(r.ascending, "levels 1-2 up only");
            bool seen[MAX_TILES] = {};
            for (uint8_t i = 0; i < r.count; ++i) {
                CHECK(r.order[i] < r.count && !seen[r.order[i]], "order a permutation");
                seen[r.order[i] < r.count ? r.order[i] : 0] = true;
                CHECK(text(r.tiles[i], t, sizeof(t)) <= 5, "label fits: %s", t);
                for (uint8_t j = i + 1; j < r.count; ++j) CHECK(!equal(r.tiles[i], r.tiles[j]), "distinct");
                if (i > 0) {
                    const Tile& a = r.tiles[r.order[i - 1]];
                    const Tile& b = r.tiles[r.order[i]];
                    CHECK(r.ascending ? less(a, b) : less(b, a), "level %u in order", level);
                }
            }
            switch (level) {
                case 1: case 2: case 3: case 4:
                    for (uint8_t i = 0; i < r.count; ++i) {
                        const int v = r.tiles[i].n;
                        CHECK(r.tiles[i].kind == Kind::Whole && v >= 0 && v <= (level == 4 ? 99 : 20),
                              "level %u range", level);
                        if (level == 1) CHECK(v >= 1 && v <= 10, "level 1 range");
                    }
                    break;
                case 5: {
                    bool reversed = false;
                    for (uint8_t i = 0; i < r.count; ++i) {
                        for (uint8_t j = 0; j < r.count; ++j) {
                            const int a = r.tiles[i].n;
                            const int b = r.tiles[j].n;
                            if (i != j && a / 10 == b % 10 && a % 10 == b / 10) reversed = true;
                        }
                    }
                    CHECK(reversed, "level 5 a reversed pair");
                    break;
                }
                case 6: {
                    int digits = 0;
                    for (uint8_t i = 0; i < r.count; ++i) {
                        int v = r.tiles[i].n;
                        CHECK(v >= 100 && v <= 999, "three digits");
                        int mask = 0;
                        for (; v > 0; v /= 10) mask |= 1 << (v % 10);
                        if (i == 0) digits = mask;
                        CHECK(mask == digits, "level 6 same digits");
                    }
                    break;
                }
                case 7: {
                    bool pair = false;
                    for (uint8_t i = 0; i < r.count; ++i) {
                        for (uint8_t j = 0; j < r.count; ++j) {
                            if (r.tiles[i].n != 0 && r.tiles[i].n == -r.tiles[j].n) pair = true;
                        }
                        CHECK(std::abs(r.tiles[i].n) <= 20, "level 7 range");
                    }
                    CHECK(pair, "level 7 a number and its negative");
                    break;
                }
                case 8: {
                    int tenths = 0;
                    int hundredths = 0;
                    for (uint8_t i = 0; i < r.count; ++i) {
                        CHECK(r.tiles[i].kind == Kind::Decimal && r.tiles[i].n % 100 != 0, "decimal");
                        if (r.tiles[i].n % 10 == 0) ++tenths; else ++hundredths;
                    }
                    CHECK(tenths >= 1 && hundredths >= 2, "level 8 mixed lengths");
                    break;
                }
                case 9:
                    for (uint8_t i = 0; i < r.count; ++i) {
                        CHECK(r.tiles[i].kind == Kind::Fraction && r.tiles[i].n < r.tiles[i].d, "proper");
                    }
                    break;
                case 10:
                    for (uint8_t i = 0; i < r.count; ++i) {
                        CHECK(r.tiles[i].kind != Kind::Whole && r.tiles[i].n > 0 &&
                                  r.tiles[i].n < static_cast<int>(r.tiles[i].d), "between 0 and 1");
                    }
                    break;
            }
        }
        if (level >= 3) CHECK(up * 100 > rounds * 40 && up * 100 < rounds * 60, "level %u up %d", level, up);
        std::printf("level %2u %-20s up %d of %d\n", level, levelName(level), up, rounds);
    }
    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 300; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
