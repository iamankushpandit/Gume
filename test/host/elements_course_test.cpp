// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/ElementsCourse.h against the real element
// table, compiled through the PC build's Arduino shim.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games -I host/shim test/host/elements_course_test.cpp src/games/ElementData.cpp src/games/ElementDataTable.cpp -o elements_course_test && ./elements_course_test
//
// For every element of every level and every turn, with many seeds: the
// kind is one of the level's and applies; the options are different and
// all of the level's pool; the answer is right (the element after really
// has the next atomic number, the same column really is, the row really is,
// the family really is, nothing else offered is of the asked family); no
// fact names its element; no clashing fact is offered beside its twin; the
// prompt fits the strip; each level has every one of its kinds in use.
#include "ElementsCourse.h"
#include "engine/Course.h"

#include <cstdio>
#include <cstring>

using namespace ElementsCourse;

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            if (failures <= 25) {                                             \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                   \
                printf(__VA_ARGS__);                                          \
                printf("\n");                                                 \
            }                                                                 \
        }                                                                     \
    } while (0)

int main() {
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        char line[80];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "level line %d", level);
        const uint8_t tier = tierFor(level);
        const uint16_t pool = elementPoolSize(tier);
        CHECK(pool <= Course::QuestionDeck::MAX_ITEMS, "deck");
        uint16_t used = 0;
        for (uint16_t i = 0; i < pool; ++i) {
            const uint8_t e = elementFromPool(i, tier);
            CHECK(e < ELEMENT_COUNT && ELEMENTS[e].tier <= tier, "pool");
            for (uint8_t turn = 0; turn < 12; ++turn) {
                for (uint32_t seed = 1; seed < 40; seed += 3) {
                    Question q;
                    make(level, e, turn, seed * 2654435761u, q);
                    used |= kindBit(q.kind);
                    CHECK((levelOf(level).kinds & kindBit(q.kind)) != 0 || q.kind == Kind::SymbolToName,
                          "L%d kind %d not of the level", level, (int)q.kind);
                    CHECK(applies(q.kind, e, tier), "L%d %s kind %d does not apply", level,
                          ELEMENTS[e].symbol, (int)q.kind);
                    prompt(q, line, sizeof(line));
                    CHECK(strlen(line) > 0 && strlen(line) <= 49, "prompt '%s'", line);
                    if (q.kind == Kind::FindIt) {
                        CHECK(q.count == 0, "find it");
                        continue;
                    }
                    CHECK(q.count == (q.kind == Kind::StateOf ? 3 : 4), "count");
                    for (uint8_t a = 0; a < q.count; ++a) {
                        for (uint8_t b = 0; b < a; ++b)
                            CHECK(q.values[a] != q.values[b], "L%d %s kind %d option twice", level,
                                  ELEMENTS[e].symbol, (int)q.kind);
                        char lab[32];
                        label(q, q.values[a], lab, sizeof(lab));
                        CHECK(strlen(lab) >= 1 && strlen(lab) <= 23, "label '%s'", lab);
                    }
                    const uint8_t ans = q.values[0];
                    switch (q.kind) {
                        case Kind::NextAfter:
                            CHECK(ELEMENTS[ans].z == ELEMENTS[e].z + 1, "next after %s", ELEMENTS[e].name);
                            break;
                        case Kind::SameColumn:
                            CHECK(ans != e && ELEMENTS[ans].col == ELEMENTS[e].col && ELEMENTS[ans].row <= 7,
                                  "column of %s", ELEMENTS[e].name);
                            for (uint8_t a = 1; a < 4; ++a)
                                CHECK(ELEMENTS[q.values[a]].col != ELEMENTS[e].col, "other column");
                            break;
                        case Kind::RowOf:
                            CHECK(ans == tableRow(e) && ans >= 1 && ans <= 7, "row of %s", ELEMENTS[e].name);
                            for (uint8_t a = 1; a < 4; ++a) CHECK(q.values[a] >= 1 && q.values[a] <= 7, "row");
                            break;
                        case Kind::FamilyOf:
                            CHECK(ans == ELEMENTS[e].category && clearFamily(ans), "family");
                            for (uint8_t a = 1; a < 4; ++a) CHECK(clearFamily(q.values[a]), "family label");
                            break;
                        case Kind::WhichIsFamily:
                            for (uint8_t a = 1; a < 4; ++a)
                                CHECK(ELEMENTS[q.values[a]].category != ELEMENTS[e].category,
                                      "two of the family");
                            break;
                        case Kind::StateOf:
                            CHECK(ans == ELEMENTS[e].state && ELEMENTS[e].tier <= 2, "state");
                            break;
                        case Kind::FactToElement:
                            CHECK(!factGivesAway(e), "fact names %s", ELEMENTS[e].name);
                            for (uint8_t a = 1; a < 4; ++a) CHECK(!factClash(e, q.values[a]), "fact twin");
                            CHECK(ans == e, "fact answer");
                            break;
                        default:
                            CHECK(ans == e, "answer");
                            break;
                    }
                    if (q.kind != Kind::StateOf && q.kind != Kind::FamilyOf && q.kind != Kind::RowOf) {
                        for (uint8_t a = 0; a < 4; ++a) CHECK(ELEMENTS[q.values[a]].tier <= tier, "option tier");
                    }
                }
            }
        }
        CHECK((used & levelOf(level).kinds) == levelOf(level).kinds, "level %d uses %x of %x", level,
              used, levelOf(level).kinds);
    }
    int giveaways = 0;
    for (uint8_t e = 0; e < ELEMENT_COUNT; ++e) giveaways += factGivesAway(e);
    printf("%d facts name their own element and are not asked\n", giveaways);
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
