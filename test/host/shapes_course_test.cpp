// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of src/games/ShapesCourse.h: the levels of Shapes and the
// rounds each one deals. The header is pure C++, and this file is what proves
// it, since it will not compile otherwise.
//
// Not run by CI and not picked up by pio test. Build and run it with any host
// C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/shapes_course_test.cpp -o shapes_course_test && ./shapes_course_test
//
// For every level, over thousands of rounds dealt from one deck as the game
// deals them: four different shapes from the level's pool, never two with one
// side count where the targets are side counts, two concave and two convex
// where the row says which, four different colours, targets never in the rows'
// own order, every turn one that looks different, every name and target fitting
// its row; every shape of the level dealt within a few rounds; and the ladder's
// verdict on a round.
#include "ShapesCourse.h"

#include <cstdio>
#include <cstring>

using namespace ShapesCourse;

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
    CHECK(clampLevel(0) == 1 && clampLevel(99) == LEVEL_COUNT, "clamp");
    CHECK(judge(0) == Verdict::Right && judge(1) == Verdict::Neither && judge(2) == Verdict::Wrong,
          "verdict");
    for (uint8_t i = 0; i < SHAPE_COUNT; ++i) {
        CHECK(std::strlen(FACTS[i].name) <= 13, "name fits a target row: %s", FACTS[i].name);
    }
    for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
        const Level& l = levelOf(level);
        char line[64];
        CHECK(levelLine(level, line, sizeof(line)) <= LEVEL_LINE_MAX, "line fits: %s", line);
        CHECK(std::strlen(instruction(level)) <= 42, "instruction fits");
        CHECK(l.poolSize >= 4, "level %u has four shapes to deal", level);

        Course::QuestionDeck deck;
        deck.reset(l.poolSize);
        Rng rng(level * 7919U);
        int lastSeen[SHAPE_COUNT];
        for (int i = 0; i < SHAPE_COUNT; ++i) lastSeen[i] = -1;
        int worstGap = 0;
        int turnedCount[4] = {};
        const int rounds = 6000;
        for (int round = 0; round < rounds; ++round) {
            Round r;
            deal(level, deck, rng.next(), r);
            CHECK(r.level == level, "level");
            int concave = 0;
            bool identity = true;
            for (uint8_t i = 0; i < 4; ++i) {
                const Shape s = r.shapes[i];
                bool inPool = false;
                for (uint8_t k = 0; k < l.poolSize; ++k) inPool = inPool || l.pool[k] == s;
                CHECK(inPool, "level %u deals %s from its pool", level, fact(s).name);
                CHECK(l.pool[r.items[i]] == s, "item matches shape");
                for (uint8_t j = i + 1; j < 4; ++j) {
                    CHECK(r.shapes[i] != r.shapes[j], "level %u four different shapes", level);
                    CHECK(r.colours[i] != r.colours[j], "four different colours");
                    if (l.target == Target::Sides) {
                        CHECK(fact(r.shapes[i]).sides != fact(r.shapes[j]).sides,
                              "level %u two with %u sides", level, fact(r.shapes[i]).sides);
                    }
                }
                if (l.target == Target::Sides) CHECK(fact(s).sides >= 3, "a side count to ask");
                if (fact(s).concave) ++concave;
                if (r.targetOrder[i] != i) identity = false;
                char name[32];
                rowName(r, i, name, sizeof(name));
                CHECK(std::strlen(name) <= ROW_NAME_MAX, "row name fits: %s", name);
                if (l.concaveConvex) CHECK(std::strlen(name) <= 8, "fits beside the word: %s", name);
                if (!l.named) CHECK(name[0] == '\0', "level %u names no picture", level);
                char target[32];
                targetText(r, i, target, sizeof(target));
                CHECK(std::strlen(target) <= 13, "target fits: %s", target);
                if (l.turned) {
                    CHECK(r.turns[i] >= 1 && r.turns[i] <= 3 && ((fact(s).turns >> r.turns[i]) & 1U),
                          "%s turned %u looks different", fact(s).name, r.turns[i]);
                    ++turnedCount[r.turns[i]];
                } else {
                    CHECK(r.turns[i] == 0, "level %u not turned", level);
                }
                const int item = static_cast<int>(s);
                if (lastSeen[item] >= 0 && round - lastSeen[item] > worstGap) {
                    worstGap = round - lastSeen[item];
                }
                lastSeen[item] = round;
            }
            if (l.concaveConvex) CHECK(concave == 2, "level %u two concave, got %d", level, concave);
            CHECK(!identity, "targets in the rows' order");
            bool seenTarget[4] = {};
            for (uint8_t i = 0; i < 4; ++i) {
                CHECK(r.targetOrder[i] < 4 && !seenTarget[r.targetOrder[i]], "a permutation");
                seenTarget[r.targetOrder[i] < 4 ? r.targetOrder[i] : 0] = true;
            }
            /* A slip on a round, as the game reports one. */
            if (round % 7 == 3) deck.missed(r.items[0]);
        }
        for (uint8_t k = 0; k < l.poolSize; ++k) {
            CHECK(lastSeen[static_cast<int>(l.pool[k])] >= 0, "level %u never deals %s", level,
                  fact(l.pool[k]).name);
        }
        /* Every shape comes back within a few passes of the pool. */
        int bound = 3 * ((l.poolSize + 3) / 4) + 4;
        /* On a sides level two shapes with one count compete for one place
         * in a round, so each comes up about half as often. */
        bool sharedCount = false;
        if (l.target == Target::Sides) {
            for (uint8_t a = 0; a < l.poolSize; ++a) {
                for (uint8_t b = a + 1; b < l.poolSize; ++b) {
                    sharedCount = sharedCount || fact(l.pool[a]).sides == fact(l.pool[b]).sides;
                }
            }
        }
        if (sharedCount) bound *= 2;
        CHECK(worstGap <= bound, "level %u: a shape waited %d rounds (bound %d)", level, worstGap,
              bound);
        std::printf("level %u %-20s pool %2u  longest wait %2d rounds", level, levelName(level),
                    l.poolSize, worstGap);
        if (l.turned) std::printf("  turns %d/%d/%d", turnedCount[1], turnedCount[2], turnedCount[3]);
        std::printf("\n");
    }

    Course::Ladder ladder;
    ladder.start(LEVEL_COUNT, 0);
    for (int i = 0; i < 200; ++i) ladder.answer(true);
    CHECK(ladder.level() == LEVEL_COUNT, "climbs to the top");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
