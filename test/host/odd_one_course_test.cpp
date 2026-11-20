// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/OddOneCourse.h: the levels of Odd One Out and
// the grids each one deals. The header is pure C++, and this file is what
// proves it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/odd_one_course_test.cpp -o odd_one_course_test && ./odd_one_course_test
//
// For every level, over many grids: exactly one card is alone in anything
// that can be seen -- colour, shape, size, or which way a triangle points --
// and it is the card the round says; the difference is the kind the level
// asks; the odd card is bigger and smaller, up and down, about equally often;
// it is in each of the nine places; sizes stay readable on a card; and on the
// ignore-the-rest levels the cards really do differ among themselves.
#include "OddOneCourse.h"
#include "engine/Course.h"

#include <cstdio>

using namespace OddOneCourse;

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

/* What can be seen of a card, one attribute at a time. */
static int seen(const Card& c, int attribute) {
    switch (attribute) {
        case 0: return c.colour;
        case 1: return static_cast<int>(c.shape);
        case 2: return c.size;
        default: return c.shape == Shape::Triangle ? (c.upsideDown ? 1 : 0) : 2;
    }
}

int main() {
    CHECK(shade(0xF9EA, true, 40) != 0xF9EA && shade(0xF9EA, false, 40) != 0xF9EA, "shade moves");
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "line fits: %s", line);
        Rng rng(level * 7919U);
        int place[CARDS] = {};
        int bigger = 0;
        int smaller = 0;
        int downOdd = 0;
        int upOdd = 0;
        int kinds[4] = {};
        int noisy = 0;
        const int rounds = 40000;
        for (int k = 0; k < rounds; ++k) {
            Round r;
            deal(level, rng.next(), r);
            CHECK(r.level == level && r.odd < CARDS, "round");
            ++place[r.odd];
            ++kinds[static_cast<int>(r.kind)];
            int alone = 0;
            bool oddAlone = false;
            bool oddAloneInKind = false;
            bool varied = false;
            for (int a = 0; a < 4; ++a) {
                int distinct = 0;
                for (uint8_t i = 0; i < CARDS; ++i) {
                    int count = 0;
                    for (uint8_t j = 0; j < CARDS; ++j) {
                        if (seen(r.cards[i], a) == seen(r.cards[j], a)) ++count;
                    }
                    if (count == 1) {
                        CHECK(i == r.odd, "level %u card %u alone in attribute %d, odd is %u", level, i,
                              a, r.odd);
                        if (i == r.odd) {
                            oddAlone = true;
                            if (a == static_cast<int>(r.kind)) oddAloneInKind = true;
                        }
                        ++alone;
                    } else {
                        CHECK(count >= 3, "level %u attribute %d value shared by only %d", level, a,
                              count);
                    }
                    if (i != r.odd) {
                        bool first = true;
                        for (uint8_t j = 0; j < i; ++j) {
                            if (j != r.odd && seen(r.cards[i], a) == seen(r.cards[j], a)) first = false;
                        }
                        if (first) ++distinct;
                    }
                }
                if (distinct > 1) varied = true;
            }
            CHECK(oddAlone && oddAloneInKind, "level %u the odd card is alone in its kind", level);
            for (uint8_t i = 0; i < CARDS; ++i) {
                CHECK(r.cards[i].size >= 6 && r.cards[i].size <= 19, "size readable");
            }
            const Card& odd = r.cards[r.odd];
            const Card& other = r.cards[(r.odd + 1) % CARDS];
            if (r.kind == Kind::Size) {
                if (odd.size > other.size) ++bigger; else ++smaller;
            }
            if (r.kind == Kind::Turn) {
                if (odd.upsideDown) ++downOdd; else ++upOdd;
            }
            if (varied) ++noisy;
            switch (level) {
                case 1: case 6: CHECK(r.kind == Kind::Colour, "level %u colour", level); break;
                case 2: CHECK(r.kind == Kind::Shape, "level 2 shape"); break;
                case 3: CHECK(r.kind == Kind::Size, "level 3 size"); break;
                case 4: CHECK(r.kind == Kind::Turn, "level 4 turn"); break;
                default: break;
            }
            if (level <= 6) CHECK(!varied, "level %u the other cards are alike", level);
            if (level == 7) CHECK(varied, "level 7 the other cards vary");
        }
        for (uint8_t i = 0; i < CARDS; ++i) {
            CHECK(place[i] * 100 > rounds * 9 && place[i] * 100 < rounds * 13, "level %u place %u %d",
                  level, i, place[i]);
        }
        if (bigger + smaller > 0) {
            CHECK(bigger * 100 > (bigger + smaller) * 40 && smaller * 100 > (bigger + smaller) * 40,
                  "level %u bigger %d smaller %d", level, bigger, smaller);
        }
        if (downOdd + upOdd > 0) {
            CHECK(downOdd * 100 > (downOdd + upOdd) * 40 && upOdd * 100 > (downOdd + upOdd) * 40,
                  "level %u down %d up %d", level, downOdd, upOdd);
        }
        std::printf("level %u %-17s kinds %d/%d/%d/%d  noisy %d\n", level, levelName(level), kinds[0],
                    kinds[1], kinds[2], kinds[3], noisy);
    }

    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 200; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
