// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/FlagCourse.h against the real country table
// (src/games/CountryDataTable.cpp, compiled here through the PC build's
// Arduino shim).
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games -I host/shim test/host/flag_course_test.cpp -o flag_course_test && ./flag_course_test
//
// For every level: the countries it asks about (and that each continent
// level is that whole continent), at least three wrong answers for every one
// of them that are not look-alikes of it, the level line fitting, a deck that
// deals every country of the level before any comes back, and the continent
// numbers agreeing with CountryData.h.
#include "FlagCourse.h"
#include "engine/Course.h"
#include "CountryData.h"
#include "../src/games/CountryDataTable.cpp"

#include <cstdio>
#include <cstring>

using namespace FlagCourse;

static bool known(const char* iso2) {
    for (uint16_t i = 0; i < COUNTRY_FACT_COUNT; ++i) {
        if (strcmp(COUNTRY_FACTS[i].iso2, iso2) == 0) return true;
    }
    return false;
}

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            if (failures <= 20) {                                             \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                   \
                printf(__VA_ARGS__);                                          \
                printf("\n");                                                 \
            }                                                                 \
        }                                                                     \
    } while (0)

int main() {
    CHECK(AFRICA == CONT_AFRICA && ASIA == CONT_ASIA && EUROPE == CONT_EUROPE &&
              NORTH_AMERICA == CONT_NORTH_AMERICA && SOUTH_AMERICA == CONT_SOUTH_AMERICA &&
              OCEANIA == CONT_OCEANIA,
          "continent numbers");
    CHECK(COUNTRY_FACT_COUNT == 195, "195 countries");

    int perContinent[6] = {};
    for (uint16_t i = 0; i < COUNTRY_FACT_COUNT; ++i) ++perContinent[COUNTRY_FACTS[i].continent];

    uint16_t sizes[LEVEL_COUNT + 1] = {};
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "level line %d", level);
        uint16_t pool[256];
        uint16_t n = 0;
        for (uint16_t i = 0; i < COUNTRY_FACT_COUNT; ++i) {
            const CountryFact& f = COUNTRY_FACTS[i];
            if (asks(level, f.tier, f.continent)) pool[n++] = i;
        }
        sizes[level] = n;
        CHECK(n >= 10 && n <= Course::QuestionDeck::MAX_ITEMS, "level %d pool %d", level, n);

        /* Every question has three different wrong answers available. */
        for (uint16_t k = 0; k < n; ++k) {
            const CountryFact& a = COUNTRY_FACTS[pool[k]];
            int ok = 0;
            for (uint16_t i = 0; i < COUNTRY_FACT_COUNT; ++i) {
                const CountryFact& b = COUNTRY_FACTS[i];
                if (&b == &a) continue;
                if (!offers(level, a.continent, b.tier, b.continent)) continue;
                if (lookalike(a.iso2, b.iso2)) continue;
                if (strcmp(a.capital, b.capital) == 0) continue;
                ++ok;
            }
            CHECK(ok >= 3, "level %d %s has %d wrong answers", level, a.iso2, ok);
        }

        /* The deck covers the level before repeating more than an eighth. */
        Course::QuestionDeck deck;
        deck.reset(n);
        bool seen[256] = {};
        uint32_t s = 99u + level;
        uint16_t distinct = 0;
        for (uint16_t q = 0; q < n; ++q) {
            s = s * 1664525u + 1013904223u;
            const uint16_t d = deck.next(s);
            CHECK(d < n, "deck item");
            if (d < n && !seen[d]) {
                seen[d] = true;
                ++distinct;
            }
        }
        CHECK(distinct >= n - n / 8, "level %d: %d of %d in one pass", level, distinct, n);
    }
    CHECK(sizes[1] == 30 && sizes[2] == 32, "tier pools %d %d", sizes[1], sizes[2]);
    CHECK(sizes[3] == perContinent[CONT_EUROPE], "Europe whole");
    CHECK(sizes[4] == perContinent[CONT_NORTH_AMERICA] + perContinent[CONT_SOUTH_AMERICA],
          "Americas whole");
    CHECK(sizes[5] == perContinent[CONT_ASIA], "Asia whole");
    CHECK(sizes[6] == perContinent[CONT_OCEANIA], "Oceania whole");
    CHECK(sizes[7] == perContinent[CONT_AFRICA], "Africa whole");
    CHECK(sizes[8] == 195 && sizes[9] == 195, "world");

    /* The look-alike list names real countries and is symmetric. */
    for (uint8_t i = 0; i < LOOKALIKE_COUNT; ++i) {
        CHECK(known(LOOKALIKES[i][0]), "%s", LOOKALIKES[i][0]);
        CHECK(known(LOOKALIKES[i][1]), "%s", LOOKALIKES[i][1]);
        CHECK(lookalike(LOOKALIKES[i][1], LOOKALIKES[i][0]), "symmetric");
    }
    CHECK(!lookalike("FR", "DE"), "not every pair");
    CHECK(capitalCounts(9) && !capitalCounts(8), "capital counts on 9");

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
