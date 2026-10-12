// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test for src/games/NumberNestRules.cpp and NumberNestSession.cpp.
//
// The rules of NumberNest include nothing but <stdint.h>, so they compile and
// run anywhere, and this file is what proves it: it will not build if an
// Arduino header ever leaks into them. It also holds every property the game
// promises about its puzzles -- one answer per group, numbers inside the
// level's limits, a final group made of exactly the five answers before it --
// and it checks them with its OWN brute-force arithmetic rather than by
// asking the rules whether the rules are right.
//
// It is not run by CI and not picked up by pio test (the folder name does
// not start with test_). Build and run it with any host C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src/games test/host/numbernest_rules_test.cpp src/games/NumberNestRules.cpp src/games/NumberNestSession.cpp -o numbernest_rules_test && ./numbernest_rules_test
//
// Exit code 0 means every check passed.

#include "NumberNestRules.h"

#include <cstdio>
#include <cstring>

using namespace Nest;

static long checks = 0;
static int failures = 0;

#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        ++checks;                                                         \
        if (!(cond)) {                                                    \
            ++failures;                                                   \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                     \
            std::printf("\n");                                            \
        }                                                                 \
    } while (0)

// combine() in a constant expression. If the operands were multiplied as int
// and widened afterwards, this would overflow a signed type -- which a
// compiler must refuse to evaluate, so the bug cannot get as far as a run.
static_assert(combine(Op::Mul, 65535, 65535) == 4294836225UL &&
                  combine(Op::Add, 65535, 65535) == 131070UL,
              "combine() must widen its operands before the arithmetic");

// The timings are part of the design, not tuning: a wrong mark has to be seen,
// an equation has to be read, and a lift is several 20 ms frames -- one or two
// is a contact dropout. A promotion is one clean nest.
static_assert(WRONG_MS >= 300 && WRONG_MS < REVEAL_MS, "a wrong mark must be up long enough to see");
static_assert(REVEAL_MS >= 800 && REVEAL_MS <= 3000, "an equation must be up long enough to read");
static_assert(RELEASE_MS >= 100 && RELEASE_MS <= SETTLE_MS, "a lift is several frames, not one");
static_assert(SETTLE_MS >= 200 && SETTLE_MS <= 500, "the settle time");
static_assert(PROMOTE_RUN == GROUP_SIZE + 1, "a promotion is one clean nest: six answers");

static const Op OPS[OP_COUNT] = {Op::Add, Op::Mul};
static const char* const OP_NAME[OP_COUNT] = {"add", "mul"};

// ---- an independent referee ---------------------------------------------
//
// Deliberately not Nest::countRelations(): 64-bit arithmetic, ORDERED pairs
// halved afterwards, and no shared code. If the rules and this disagree about
// a group, one of them is wrong and the test says so.

static unsigned long long bruteApply(Op op, unsigned long long a, unsigned long long b) {
    return op == Op::Mul ? a * b : a + b;
}

struct Verdict {
    int relations;   // unordered operand pairs, each with the answer it makes
    int first;
    int second;
    int answer;
};

static Verdict referee(Op op, const uint16_t* v) {
    Verdict out = {0, -1, -1, -1};
    int ordered = 0;
    for (int i = 0; i < GROUP_SIZE; ++i) {
        for (int j = 0; j < GROUP_SIZE; ++j) {
            if (i == j) continue;
            for (int k = 0; k < GROUP_SIZE; ++k) {
                if (k == i || k == j) continue;
                if (bruteApply(op, v[i], v[j]) == v[k]) {
                    ++ordered;
                    if (i < j) {
                        out.first = i;
                        out.second = j;
                        out.answer = k;
                    }
                }
            }
        }
    }
    out.relations = ordered / 2;   // (i, j) and (j, i) are one pair
    return out;
}

// Can `t` be made from two different operands inside the limits? Brute force.
static bool bruteEarnable(Op op, const Limits& lim, unsigned t) {
    for (unsigned a = lim.operandMin; a <= lim.operandMax; ++a) {
        for (unsigned b = a + 1; b <= lim.operandMax; ++b) {
            if (bruteApply(op, a, b) == t) return true;
        }
    }
    return false;
}

// Everything the game promises about one group, checked without the rules.
static bool groupHolds(const Group& g, Op op, const Limits& lim, const char* what,
                       uint32_t seed, uint8_t level) {
    bool ok = true;
    for (int i = 0; i < GROUP_SIZE; ++i) {
        if (g.value[i] < lim.operandMin || g.value[i] > lim.resultMax || g.value[i] > VALUE_CAP) {
            std::printf("  %s level %u seed %08x: %s value %u outside %u..%u\n", OP_NAME[opIndex(op)],
                        (unsigned)level, (unsigned)seed, what, (unsigned)g.value[i],
                        (unsigned)lim.operandMin, (unsigned)lim.resultMax);
            ok = false;
        }
        if (op == Op::Mul && g.value[i] < 2) ok = false;   // integers greater than one
        if (g.value[i] < 1) ok = false;                    // positive, always
        for (int j = i + 1; j < GROUP_SIZE; ++j) {
            if (g.value[i] == g.value[j]) {
                std::printf("  %s level %u seed %08x: %s repeats %u\n", OP_NAME[opIndex(op)],
                            (unsigned)level, (unsigned)seed, what, (unsigned)g.value[i]);
                ok = false;
            }
        }
    }
    const Verdict v = referee(op, g.value);
    if (v.relations != 1) {
        std::printf("  %s level %u seed %08x: %s has %d answers [%u %u %u %u %u]\n",
                    OP_NAME[opIndex(op)], (unsigned)level, (unsigned)seed, what, v.relations,
                    (unsigned)g.value[0], (unsigned)g.value[1], (unsigned)g.value[2],
                    (unsigned)g.value[3], (unsigned)g.value[4]);
        return false;
    }
    if (v.first != g.rel.first || v.second != g.rel.second || v.answer != g.rel.answer) {
        std::printf("  %s level %u seed %08x: %s records the wrong positions\n",
                    OP_NAME[opIndex(op)], (unsigned)level, (unsigned)seed, what);
        ok = false;
    }
    // Three distinct positions, and the answer larger than either operand.
    if (v.first == v.second || v.first == v.answer || v.second == v.answer) ok = false;
    if (g.value[v.answer] <= g.value[v.first] || g.value[v.answer] <= g.value[v.second]) ok = false;
    // The two operands obey the operand limits, not merely the display ones.
    if (g.value[v.first] > lim.operandMax || g.value[v.second] > lim.operandMax ||
        g.value[v.first] < lim.operandMin || g.value[v.second] < lim.operandMin) {
        std::printf("  %s level %u seed %08x: %s operand outside %u..%u\n", OP_NAME[opIndex(op)],
                    (unsigned)level, (unsigned)seed, what, (unsigned)lim.operandMin,
                    (unsigned)lim.operandMax);
        ok = false;
    }
    return ok;
}

static bool puzzleHolds(const Puzzle& p, Op op, uint8_t level, uint32_t seed) {
    const Limits& lim = LIMITS[opIndex(op)][level - 1];
    bool ok = p.op == op && p.level == level;
    ok = groupHolds(p.final, op, lim, "final group", seed, level) && ok;
    for (int i = 0; i < GROUP_SIZE; ++i) {
        ok = groupHolds(p.prelim[i], op, lim, "preliminary group", seed, level) && ok;
        // Carry-forward: group i's answer is the final group's value i.
        if (p.prelim[i].value[p.prelim[i].rel.answer] != p.final.value[i]) {
            std::printf("  %s level %u seed %08x: group %d answers %u but the nest holds %u\n",
                        OP_NAME[opIndex(op)], (unsigned)level, (unsigned)seed, i,
                        (unsigned)p.prelim[i].value[p.prelim[i].rel.answer],
                        (unsigned)p.final.value[i]);
            ok = false;
        }
        // And every value of the final group can be earned at this level.
        if (!bruteEarnable(op, lim, p.final.value[i])) ok = false;
    }
    return ok;
}

// The two operands of a group's one equation, smaller first, as one number.
static unsigned equationKey(const Group& g) {
    const unsigned a = g.value[g.rel.first];
    const unsigned b = g.value[g.rel.second];
    return a < b ? a * 1024u + b : b * 1024u + a;
}

// Does the nest ask its final equation twice -- once as a preliminary group?
static bool repeatsFinal(const Puzzle& p) {
    for (int i = 0; i < GROUP_SIZE; ++i) {
        if (equationKey(p.prelim[i]) == equationKey(p.final)) return true;
    }
    return false;
}

// Is some number on show, other than the answer, another shown number added
// to itself (or multiplied by itself)? "3 + 3 = 6" is not an answer under the
// rules, and tapping the 6 is still marked wrong. `betweenOperands` reports
// the unavoidable kind: the two operands of the equation themselves.
static bool showsDouble(Op op, const Group& g, bool* betweenOperands) {
    bool any = false;
    *betweenOperands = false;
    for (int i = 0; i < GROUP_SIZE; ++i) {
        for (int j = 0; j < GROUP_SIZE; ++j) {
            if (i == j || j == g.rel.answer) continue;
            if (bruteApply(op, g.value[i], g.value[i]) != g.value[j]) continue;
            any = true;
            const bool iOperand = i == g.rel.first || i == g.rel.second;
            const bool jOperand = j == g.rel.first || j == g.rel.second;
            if (iOperand && jOperand) *betweenOperands = true;
        }
    }
    return any;
}

static bool samePuzzle(const Puzzle& a, const Puzzle& b) {
    if (a.op != b.op || a.level != b.level || a.fallback != b.fallback) return false;
    for (int g = 0; g <= GROUP_SIZE; ++g) {
        const Group& x = g < GROUP_SIZE ? a.prelim[g] : a.final;
        const Group& y = g < GROUP_SIZE ? b.prelim[g] : b.final;
        for (int i = 0; i < GROUP_SIZE; ++i) {
            if (x.value[i] != y.value[i]) return false;
        }
        if (x.rel.first != y.rel.first || x.rel.second != y.rel.second ||
            x.rel.answer != y.rel.answer) {
            return false;
        }
    }
    return true;
}

// ---- arithmetic -----------------------------------------------------------

static void testArithmetic() {
    // The extremes of the type, not of the game: combine() must be exact for
    // every pair of uint16_t, which is what "widen first" buys.
    static const uint16_t EDGE[] = {0, 1, 2, 255, 256, 999, 1000, 32767, 32768, 65534, 65535};
    for (uint16_t a : EDGE) {
        for (uint16_t b : EDGE) {
            for (Op op : OPS) {
                const unsigned long long want = bruteApply(op, a, b);
                CHECK(combine(op, a, b) == want, "%s %u %u gave %lu", OP_NAME[opIndex(op)],
                      (unsigned)a, (unsigned)b, (unsigned long)combine(op, a, b));
            }
        }
    }
    CHECK(combine(Op::Mul, 65535, 65535) == 4294836225UL, "largest product");
    CHECK(combine(Op::Add, 65535, 65535) == 131070UL, "largest sum");
}

static void testExamples() {
    // The two groups the rules page shows.
    const uint16_t add[GROUP_SIZE] = {3, 7, 10, 12, 18};
    const uint16_t mul[GROUP_SIZE] = {2, 3, 6, 11, 17};
    Relation rel{9, 9, 9};
    CHECK(countRelations(Op::Add, add, GROUP_SIZE, &rel) == 1, "add example");
    CHECK(rel.first == 0 && rel.second == 1 && rel.answer == 2, "3 + 7 = 10");
    CHECK(countRelations(Op::Mul, mul, GROUP_SIZE, &rel) == 1, "mul example");
    CHECK(rel.first == 0 && rel.second == 1 && rel.answer == 2, "2 x 3 = 6");
    CHECK(referee(Op::Add, add).relations == 1 && referee(Op::Mul, mul).relations == 1,
          "the referee agrees about the examples");

    // Groups that must be refused: two answers, no answer, a repeated value.
    const uint16_t twoAnswers[GROUP_SIZE] = {2, 3, 5, 8, 20};      // 2+3=5 and 3+5=8
    const uint16_t twoPairs[GROUP_SIZE] = {3, 8, 4, 6, 24};        // 3x8 and 4x6 both make 24
    const uint16_t none[GROUP_SIZE] = {3, 7, 11, 13, 19};
    CHECK(countRelations(Op::Add, twoAnswers, GROUP_SIZE, nullptr) == 2, "two sums");
    CHECK(countRelations(Op::Mul, twoPairs, GROUP_SIZE, nullptr) == 2, "two products");
    CHECK(countRelations(Op::Add, none, GROUP_SIZE, nullptr) == 0, "no sum");
    Group bad{};
    std::memcpy(bad.value, twoAnswers, sizeof(twoAnswers));
    bad.rel = Relation{0, 1, 2};
    CHECK(!groupValid(bad, Op::Add, LIMITS[0][1]), "a group with two answers is not valid");
    const uint16_t repeat[GROUP_SIZE] = {3, 7, 10, 10, 18};
    std::memcpy(bad.value, repeat, sizeof(repeat));
    CHECK(!groupValid(bad, Op::Add, LIMITS[0][1]), "a repeated value is not valid");
    const uint16_t tooBig[GROUP_SIZE] = {3, 7, 10, 12, 41};
    std::memcpy(bad.value, tooBig, sizeof(tooBig));
    CHECK(!groupValid(bad, Op::Add, LIMITS[0][1]), "41 is over level 2's limit of 40");
    const uint16_t fine[GROUP_SIZE] = {3, 7, 10, 12, 18};
    std::memcpy(bad.value, fine, sizeof(fine));
    CHECK(groupValid(bad, Op::Add, LIMITS[0][1]), "the example itself is valid");
    bad.rel = Relation{0, 2, 1};
    CHECK(!groupValid(bad, Op::Add, LIMITS[0][1]), "the wrong recorded positions are not valid");
}

// ---- limits ---------------------------------------------------------------

static void testLimits() {
    for (Op op : OPS) {
        const uint8_t o = opIndex(op);
        for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
            const Limits& lim = limits(op, level);
            CHECK(lim.operandMin >= (op == Op::Mul ? 2u : 1u), "%s level %u operand floor",
                  OP_NAME[o], (unsigned)level);
            CHECK(lim.operandMax > lim.operandMin && lim.resultMax <= VALUE_CAP &&
                      lim.resultMax >= lim.operandMax,
                  "%s level %u limits out of order", OP_NAME[o], (unsigned)level);
            if (level > 1) {
                const Limits& easier = limits(op, static_cast<uint8_t>(level - 1));
                CHECK(lim.operandMax >= easier.operandMax && lim.resultMax >= easier.resultMax,
                      "%s level %u is easier than the one below it", OP_NAME[o], (unsigned)level);
            }
            // earnable() against brute force, over everything the level shows.
            int finalOperands = 0;
            for (unsigned t = 0; t <= lim.resultMax; ++t) {
                const bool want = bruteEarnable(op, lim, t);
                CHECK(earnable(op, lim, t) == want, "%s level %u earnable(%u)", OP_NAME[o],
                      (unsigned)level, t);
                if (want && t >= lim.operandMin && t <= lim.operandMax) ++finalOperands;
            }
            // A final group needs two operands that are themselves answers.
            CHECK(finalOperands >= 2, "%s level %u has %d possible final operands", OP_NAME[o],
                  (unsigned)level, finalOperands);
        }
    }
    // THE TABLE ITSELF, restated. A level's limits decide what a player is
    // asked; the stored nests, the README and the changelog describe these
    // numbers. Change one on purpose and change it here too.
    static const Limits EXPECT[OP_COUNT][LEVEL_COUNT] = {
        {{1, 9, 20}, {1, 20, 40}, {2, 50, 100}, {5, 100, 200}, {10, 250, 500}},
        {{2, 10, 100}, {2, 12, 144}, {2, 15, 225}, {2, 20, 400}, {2, 25, 625}},
    };
    for (Op op : OPS) {
        for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
            const Limits& have = limits(op, level);
            const Limits& want = EXPECT[opIndex(op)][level - 1];
            CHECK(have.operandMin == want.operandMin && have.operandMax == want.operandMax &&
                      have.resultMax == want.resultMax,
                  "%s level %u limits are %u..%u up to %u", OP_NAME[opIndex(op)], (unsigned)level,
                  (unsigned)have.operandMin, (unsigned)have.operandMax, (unsigned)have.resultMax);
        }
    }
    // The easiest multiplication stays inside the ten-times table.
    CHECK(limits(Op::Mul, 1).operandMax <= 10 && limits(Op::Mul, 1).resultMax <= 100,
          "level 1 multiplication has left the ten-times table");
    CHECK(limits(Op::Add, 1).operandMax <= 9 && limits(Op::Add, 1).resultMax <= 20,
          "level 1 addition is no longer single digits");
    // Out-of-range levels read as the easiest.
    CHECK(clampLevel(0) == 1 && clampLevel(6) == 1 && clampLevel(0xFFFFFFFFu) == 1 &&
              clampLevel(5) == 5 && clampLevel(1) == 1,
          "clampLevel");
    CHECK(&limits(Op::Add, 0) == &limits(Op::Add, 1) && &limits(Op::Mul, 200) == &limits(Op::Mul, 1),
          "limits() of a nonsense level is level 1");
}

// ---- generation -----------------------------------------------------------

// Floors for the two measures of variety and nearness below, set under
// what the generator measures today (the test prints the figures). They are
// there to catch a collapse -- always the first way of making a number,
// bystanders drawn from the whole range -- not to pin a distribution.
static const uint32_t PRELIM_FLOOR[OP_COUNT][LEVEL_COUNT] = {
    {25, 100, 600, 2000, 6000},   // measured 33, 181, 1153, 4179, 12209
    {34, 50, 85, 160, 250},       // measured 36, 55, 91, 171, 274: nearly every pair there is
};
static const uint32_t NEAR_PERCENT[OP_COUNT][LEVEL_COUNT] = {
    {90, 90, 90, 90, 90},         // measured 97 at the lowest
    {90, 90, 90, 90, 90},
};

static void testGeneration() {
    const uint32_t SEEDS = 4000;
    for (Op op : OPS) {
        const uint8_t o = opIndex(op);
        for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
            uint32_t fallbacks = 0;
            uint32_t worst = 0;
            uint32_t answerIsLargest = 0;
            uint32_t answerPosition[GROUP_SIZE] = {0};
            uint32_t distinctProbe = 0;
            uint32_t repeats = 0;          // nests that ask the final equation twice
            uint32_t doubleGroups = 0;     // groups showing a number beside its double
            uint32_t bystanderDoubles = 0; // ...where a bystander is involved
            uint32_t finalEquations = 0;   // different final equations dealt
            static uint8_t seenFinal[1024 * 1024 / 8];
            std::memset(seenFinal, 0, sizeof(seenFinal));
            static uint8_t seenPrelim[1024 * 1024 / 8];
            std::memset(seenPrelim, 0, sizeof(seenPrelim));
            uint32_t prelimEquations = 0;      // different preliminary equations dealt
            uint32_t prelimPosition[GROUP_SIZE] = {0};
            uint32_t bystanders = 0;           // in preliminary groups
            uint32_t nearBystanders = 0;       // ...inside the window near the answer
            uint32_t avoidableDoubles = 0;     // operand pairs that are a double, with a better pair to hand
            uint32_t finalOperandDoubles = 0;  // final equations whose operands are a double
            Puzzle previous{};
            for (uint32_t i = 0; i < SEEDS; ++i) {
                const uint32_t seed = mix(i * 2654435761u + level * 97u + o);
                Puzzle p;
                std::memset(&p, 0xAA, sizeof(p));   // garbage first: generate must set everything
                uint32_t spent = 0;
                const bool made = generateWithin(p, op, level, seed, SEARCH_BUDGET, &spent);
                if (!made) ++fallbacks;
                CHECK(made != p.fallback, "the fallback flag disagrees with the return value");
                CHECK(!made || spent > 0, "a nest was built without examining one candidate");
                CHECK(spent <= SEARCH_BUDGET, "spent %lu of a budget of %lu", (unsigned long)spent,
                      (unsigned long)SEARCH_BUDGET);
                if (spent > worst) worst = spent;

                CHECK(puzzleHolds(p, op, level, seed), "%s level %u seed %08x breaks a promise",
                      OP_NAME[o], (unsigned)level, (unsigned)seed);
                CHECK(puzzleValid(p), "the rules' own validator refuses its own puzzle");

                // Deterministic: the same three inputs, the same nest.
                Puzzle again;
                std::memset(&again, 0x55, sizeof(again));
                generate(again, op, level, seed);
                CHECK(samePuzzle(p, again), "%s level %u seed %08x is not repeatable", OP_NAME[o],
                      (unsigned)level, (unsigned)seed);

                if (i > 0 && !samePuzzle(p, previous)) ++distinctProbe;
                previous = p;

                uint16_t largest = 0;
                for (int k = 0; k < GROUP_SIZE; ++k) {
                    if (p.final.value[k] > largest) largest = p.final.value[k];
                }
                if (p.final.value[p.final.rel.answer] == largest) ++answerIsLargest;
                ++answerPosition[p.final.rel.answer];

                const Limits& lim = LIMITS[o][level - 1];
                const unsigned finalKey = equationKey(p.final);
                const unsigned long finalLow = finalKey / 1024u;
                if (bruteApply(op, finalLow, finalLow) == finalKey % 1024u) ++finalOperandDoubles;
                for (int g = 0; g < GROUP_SIZE; ++g) {
                    const Group& group = p.prelim[g];
                    ++prelimPosition[group.rel.answer];
                    const unsigned pkey = equationKey(group);
                    if (!(seenPrelim[pkey / 8] & (1u << (pkey % 8)))) {
                        seenPrelim[pkey / 8] |= static_cast<uint8_t>(1u << (pkey % 8));
                        ++prelimEquations;
                    }
                    // Bystanders are meant to be near the answer, where they
                    // can be mistaken for it: fillGroup()'s first window.
                    const unsigned anchor = group.value[group.rel.answer];
                    const unsigned reach = anchor + (anchor / 2 > 8 ? anchor / 2 : 8);
                    for (int b = 0; b < GROUP_SIZE; ++b) {
                        if (b == group.rel.first || b == group.rel.second || b == group.rel.answer) continue;
                        ++bystanders;
                        if (group.value[b] <= reach) ++nearBystanders;
                    }
                    // An operand pair in which one is the other doubled is
                    // used only when the target has no better pair: one that
                    // is not a double and not the final group's own pair.
                    const unsigned long low = pkey / 1024u;
                    const unsigned long high = pkey % 1024u;
                    if (bruteApply(op, low, low) == high) {
                        const bool isFinalAnswer = anchor == p.final.value[p.final.rel.answer];
                        for (unsigned long x = lim.operandMin; x <= lim.operandMax; ++x) {
                            for (unsigned long y = x + 1; y <= lim.operandMax; ++y) {
                                if (bruteApply(op, x, y) != anchor) continue;
                                if (bruteApply(op, x, x) == y) continue;
                                if (isFinalAnswer && x * 1024u + y == finalKey) continue;
                                ++avoidableDoubles;
                            }
                        }
                    }
                }

                if (repeatsFinal(p)) ++repeats;
                const unsigned key = equationKey(p.final);
                if (!(seenFinal[key / 8] & (1u << (key % 8)))) {
                    seenFinal[key / 8] |= static_cast<uint8_t>(1u << (key % 8));
                    ++finalEquations;
                }
                for (int g = 0; g <= GROUP_SIZE; ++g) {
                    bool operandsOnly = false;
                    const Group& group = g < GROUP_SIZE ? p.prelim[g] : p.final;
                    if (showsDouble(op, group, &operandsOnly)) {
                        ++doubleGroups;
                        // Recount with the operand pair set aside: anything
                        // left involves a number the search was free to choose.
                        bool other = false;
                        for (int a = 0; a < GROUP_SIZE && !other; ++a) {
                            for (int b = 0; b < GROUP_SIZE && !other; ++b) {
                                if (a == b || b == group.rel.answer) continue;
                                const bool aOp = a == group.rel.first || a == group.rel.second;
                                const bool bOp = b == group.rel.first || b == group.rel.second;
                                if (aOp && bOp) continue;
                                if (bruteApply(op, group.value[a], group.value[a]) == group.value[b]) {
                                    other = true;
                                }
                            }
                        }
                        if (other) ++bystanderDoubles;
                    }
                }
            }
            // The configured ranges really do permit complete puzzles: the
            // search never had to give up, and never came near its budget.
            CHECK(fallbacks == 0, "%s level %u fell back %lu times in %lu", OP_NAME[o],
                  (unsigned)level, (unsigned long)fallbacks, (unsigned long)SEEDS);
            CHECK(worst <= SEARCH_BUDGET / 2, "%s level %u needed %lu of %lu", OP_NAME[o],
                  (unsigned)level, (unsigned long)worst, (unsigned long)SEARCH_BUDGET);
            CHECK(distinctProbe >= SEEDS - 1 - SEEDS / 20, "%s level %u repeats itself: %lu changes",
                  OP_NAME[o], (unsigned)level, (unsigned long)distinctProbe);
            // "Tap the biggest number" must not be a strategy for the final
            // group, and the answer must turn up in every position.
            CHECK(answerIsLargest < SEEDS * 9 / 10, "%s level %u: the final answer is the largest %lu/%lu",
                  OP_NAME[o], (unsigned)level, (unsigned long)answerIsLargest, (unsigned long)SEEDS);
            for (int k = 0; k < GROUP_SIZE; ++k) {
                CHECK(answerPosition[k] > SEEDS / 10, "%s level %u: answer at position %d only %lu times",
                      OP_NAME[o], (unsigned)level, k, (unsigned long)answerPosition[k]);
            }
            // VARIETY, and the two things a group should not do to a player.
            // Each level deals a known number of different final equations;
            // a change to the limits that shrinks one is caught here.
            static const uint32_t FINAL_FLOOR[OP_COUNT][LEVEL_COUNT] = {
                {15, 100, 600, 1500, 2500},
                {3, 6, 12, 30, 50},
            };
            CHECK(finalEquations >= FINAL_FLOOR[o][level - 1],
                  "%s level %u deals only %lu different final equations", OP_NAME[o],
                  (unsigned)level, (unsigned long)finalEquations);
            if (op == Op::Mul && level == 1) {
                // THE ONE KNOWN EXCEPTION, stated so that it stays known. The
                // operands of a final group are themselves products of two
                // different numbers, so inside the ten-times table they are
                // 6, 8 and 10; the final equation is 6x8, 6x10 or 8x10; and
                // 48, 60 and 80 can each be earned in exactly one way there.
                // Every nest therefore meets its final equation twice. If
                // this check fails because the limits moved, that is good
                // news: delete it and correct the README and the CLAUDE.md.
                CHECK(finalEquations == 3 && repeats == SEEDS,
                      "level 1 multiplication no longer has exactly three final equations, "
                      "each asked twice (%lu, %lu) -- update the documents",
                      (unsigned long)finalEquations, (unsigned long)repeats);
            } else {
                CHECK(repeats * 8 <= SEEDS, "%s level %u asks its final equation twice in %lu of %lu",
                      OP_NAME[o], (unsigned)level, (unsigned long)repeats, (unsigned long)SEEDS);
            }
            // A number beside its own double: never by the search's choice of
            // bystander (the third pass of fillGroup may, as a last resort),
            // and seldom at all.
            CHECK(bystanderDoubles * 100 <= SEEDS * 6,
                  "%s level %u: a bystander makes a double in %lu groups", OP_NAME[o],
                  (unsigned)level, (unsigned long)bystanderDoubles);
            CHECK(doubleGroups * 5 <= SEEDS * 6, "%s level %u: %lu groups show a double",
                  OP_NAME[o], (unsigned)level, (unsigned long)doubleGroups);
            std::printf("  %s level %u: %lu nests, 0 fallbacks, worst search %lu of %lu, "
                        "final answer is the largest in %lu\n",
                        OP_NAME[o], (unsigned)level, (unsigned long)SEEDS, (unsigned long)worst,
                        (unsigned long)SEARCH_BUDGET, (unsigned long)answerIsLargest);
            std::printf("      %lu final equations, %lu nests repeat theirs, %lu of %lu groups "
                        "show a double (%lu with a bystander)\n",
                        (unsigned long)finalEquations, (unsigned long)repeats,
                        (unsigned long)doubleGroups, (unsigned long)(SEEDS * 6),
                        (unsigned long)bystanderDoubles);
            std::printf("      %lu preliminary equations, %lu of %lu bystanders near the answer, "
                        "%lu final equations on a doubled pair\n",
                        (unsigned long)prelimEquations, (unsigned long)nearBystanders,
                        (unsigned long)bystanders, (unsigned long)finalOperandDoubles);
            // The answer turns up in every position of a preliminary group
            // too, not only of the final one.
            for (int k = 0; k < GROUP_SIZE; ++k) {
                CHECK(prelimPosition[k] > SEEDS * GROUP_SIZE / 10,
                      "%s level %u: a preliminary answer at position %d only %lu times", OP_NAME[o],
                      (unsigned)level, k, (unsigned long)prelimPosition[k]);
            }
            CHECK(avoidableDoubles == 0,
                  "%s level %u: %lu operand pairs are a double although a better pair existed",
                  OP_NAME[o], (unsigned)level, (unsigned long)avoidableDoubles);
            CHECK(prelimEquations >= PRELIM_FLOOR[o][level - 1],
                  "%s level %u deals only %lu different preliminary equations", OP_NAME[o],
                  (unsigned)level, (unsigned long)prelimEquations);
            CHECK(nearBystanders * 100 >= bystanders * NEAR_PERCENT[o][level - 1],
                  "%s level %u: only %lu of %lu bystanders are near the answer", OP_NAME[o],
                  (unsigned)level, (unsigned long)nearBystanders, (unsigned long)bystanders);
            CHECK(finalOperandDoubles * 20 <= SEEDS,
                  "%s level %u: %lu final equations use a number and its double", OP_NAME[o],
                  (unsigned)level, (unsigned long)finalOperandDoubles);
        }
    }
    // Different seeds are different nests; a nonsense level plays as level 1.
    Puzzle a, b;
    generate(a, Op::Add, 3, 1);
    generate(b, Op::Add, 3, 2);
    CHECK(!samePuzzle(a, b), "two seeds, one nest");
    generate(a, Op::Mul, 0, 77);
    CHECK(a.level == 1 && puzzleHolds(a, Op::Mul, 1, 77), "level 0 is played as level 1");
    generate(a, Op::Mul, 99, 77);
    CHECK(a.level == 1, "level 99 is played as level 1");
}

static void testFallbacks() {
    for (Op op : OPS) {
        const uint8_t o = opIndex(op);
        for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
            const Puzzle& stored = fallbackPuzzle(op, level);
            CHECK(stored.fallback, "%s level %u stored nest is not marked", OP_NAME[o], (unsigned)level);
            CHECK(puzzleHolds(stored, op, level, 0), "%s level %u stored nest breaks a promise",
                  OP_NAME[o], (unsigned)level);
            CHECK(puzzleValid(stored), "%s level %u stored nest", OP_NAME[o], (unsigned)level);
            CHECK(repeatsFinal(stored) == (op == Op::Mul && level == 1),
                  "%s level %u stored nest asks its final equation twice", OP_NAME[o], (unsigned)level);
            for (int g = 0; g <= GROUP_SIZE; ++g) {
                bool operandsOnly = false;
                CHECK(!showsDouble(op, g < GROUP_SIZE ? stored.prelim[g] : stored.final, &operandsOnly),
                      "%s level %u stored nest shows a number beside its double", OP_NAME[o],
                      (unsigned)level);
            }

            // Starve the search. It must stop, say so, and hand back the
            // stored nest untouched -- never a half-built or loosened one.
            static const uint32_t STARVED[] = {0, 1, 2, 5, 17, 40};
            for (uint32_t budget : STARVED) {
                for (uint32_t seed = 1; seed <= 25; ++seed) {
                    Puzzle p;
                    std::memset(&p, 0xAA, sizeof(p));
                    uint32_t spent = 0xFFFFFFFFu;
                    const bool made = generateWithin(p, op, level, mix(seed), budget, &spent);
                    CHECK(spent <= budget, "overspent: %lu of %lu", (unsigned long)spent,
                          (unsigned long)budget);
                    CHECK(puzzleHolds(p, op, level, seed), "%s level %u budget %lu is not a valid nest",
                          OP_NAME[o], (unsigned)level, (unsigned long)budget);
                    if (!made) {
                        CHECK(samePuzzle(p, stored), "a failed search returned something else");
                    }
                    if (budget == 0) {
                        CHECK(!made, "a search with nothing to spend claimed success");
                    }
                    // Choosing the final group's two operands alone examines
                    // every operand value twice -- eighteen candidates at the
                    // narrowest level -- so no budget under that builds a
                    // nest. One that did was not being spent.
                    if (budget <= 17) {
                        CHECK(!made && spent == budget,
                              "a budget of %lu built a nest (spent %lu): the budget is not being spent",
                              (unsigned long)budget, (unsigned long)spent);
                    }
                }
            }
        }
    }
}

// ---- progression ----------------------------------------------------------

static void testProgression() {
    Progress p;
    std::memset(&p, 0xAA, sizeof(p));
    resetProgress(p);
    CHECK(p.level[0] == 1 && p.level[1] == 1 && p.run[0] == 0 && p.run[1] == 0 &&
              !p.earned[0] && !p.earned[1],
          "a fresh player starts both operations at the easiest level, owed nothing");
    CHECK(!finishNest(p, Op::Add) && p.level[0] == 1, "finishing a nest with nothing earned promotes");

    // Five in a row is not enough. The sixth EARNS the promotion; the level
    // itself waits for the nest to be finished, and only that operation moves.
    for (int i = 0; i < PROMOTE_RUN - 1; ++i) {
        CHECK(!recordGroup(p, Op::Add, true), "earned after %d", i + 1);
    }
    CHECK(!p.earned[0] && p.level[0] == 1, "five in a row earned something");
    CHECK(recordGroup(p, Op::Add, true), "the sixth in a row did not earn the promotion");
    CHECK(p.earned[0] && p.level[0] == 1 && p.run[0] == 0,
          "earned, the run starts over, and the level has NOT moved yet");
    CHECK(!p.earned[1] && p.level[1] == 1 && p.run[1] == 0, "multiplication moved");
    // A slip after it is earned loses the run and keeps the promotion.
    CHECK(!recordGroup(p, Op::Add, false) && p.earned[0], "a later slip took back what was earned");
    CHECK(finishNest(p, Op::Add) && p.level[0] == 2 && !p.earned[0], "finishing the nest applies it");
    CHECK(!finishNest(p, Op::Add) && p.level[0] == 2, "one promotion was applied twice");
    CHECK(p.level[1] == 1, "addition's promotion reached multiplication");

    // A slip throws the run away; an answer reached after a slip does not count.
    for (int i = 0; i < 5; ++i) recordGroup(p, Op::Mul, true);
    CHECK(!recordGroup(p, Op::Mul, false) && p.run[1] == 0 && p.level[1] == 1 && !p.earned[1],
          "a slip resets the run and never demotes");
    for (int i = 0; i < 5; ++i) recordGroup(p, Op::Mul, true);
    CHECK(!p.earned[1] && !finishNest(p, Op::Mul) && p.level[1] == 1,
          "five after a slip is not six in a row");
    CHECK(recordGroup(p, Op::Mul, true) && finishNest(p, Op::Mul) && p.level[1] == 2,
          "six clean after the slip promotes");
    CHECK(p.level[0] == 2, "addition was touched by multiplication's run");

    // Nothing is earned twice while one promotion is waiting, however long
    // the run grows; the counter saturates rather than wrapping to zero.
    for (int i = 0; i < PROMOTE_RUN; ++i) recordGroup(p, Op::Add, true);
    CHECK(p.earned[0], "six more in a row");
    for (int i = 0; i < 1000; ++i) {
        CHECK(!recordGroup(p, Op::Add, true), "a second promotion was earned while one waited");
    }
    CHECK(p.run[0] == 0xFF && p.level[0] == 2, "the run counter wrapped, or the level moved early");
    CHECK(finishNest(p, Op::Add) && p.level[0] == 3, "the waiting promotion is one level, not several");
    // What was answered at the old level is not a head start on the new one.
    CHECK(p.run[0] == 0, "the run was carried into the new level (%u)", (unsigned)p.run[0]);
    for (int i = 0; i < PROMOTE_RUN - 1; ++i) recordGroup(p, Op::Add, true);
    CHECK(!p.earned[0] && !finishNest(p, Op::Add) && p.level[0] == 3,
          "five at the new level promoted again");
    CHECK(p.run[0] == PROMOTE_RUN - 1, "a nest that promoted nothing lost its run");
    p.run[0] = 0;

    // The top level is a ceiling, and nothing ever takes a level away.
    for (int nest = 0; nest < 40; ++nest) {
        for (int i = 0; i < PROMOTE_RUN; ++i) recordGroup(p, Op::Add, true);
        finishNest(p, Op::Add);
        CHECK(p.level[0] <= LEVEL_COUNT, "past the top level");
    }
    CHECK(p.level[0] == LEVEL_COUNT && !p.earned[0],
          "addition tops out at level %u with nothing left owing", (unsigned)LEVEL_COUNT);
    for (int i = 0; i < 50; ++i) recordGroup(p, Op::Add, false);
    CHECK(p.level[0] == LEVEL_COUNT, "slips never demote");

    // The on-screen control: up one, wrapping, per operation; the run and
    // anything earned at the old level go with it.
    resetProgress(p);
    for (int i = 0; i < PROMOTE_RUN; ++i) recordGroup(p, Op::Add, true);
    recordGroup(p, Op::Add, true);
    CHECK(p.earned[0] && p.run[0] == 1, "set-up");
    CHECK(cycleLevel(p, Op::Add) == 2 && !p.earned[0] && p.run[0] == 0,
          "stepping the level by hand kept a promotion earned at the old one");
    for (uint8_t want = 3; want <= LEVEL_COUNT; ++want) {
        CHECK(cycleLevel(p, Op::Add) == want && p.level[0] == want, "cycle to %u", (unsigned)want);
    }
    CHECK(cycleLevel(p, Op::Add) == 1, "the control wraps to level 1");
    CHECK(p.level[1] == 1 && !p.earned[1], "cycling addition touched multiplication");
    p.level[1] = 77;   // a stored value from somewhere else
    CHECK(cycleLevel(p, Op::Mul) == 2, "a nonsense level is treated as 1 before stepping");
    p.level[1] = 0;
    p.earned[1] = true;
    CHECK(finishNest(p, Op::Mul) && p.level[1] == 2, "a nonsense level is treated as 1 before promoting");
}

// ---- one sitting ----------------------------------------------------------

static Session freshSession(uint32_t seed, uint32_t now) {
    Session s;
    std::memset(&s, 0xAA, sizeof(s));   // garbage first: reset must set everything
    resetSession(s);
    deal(s, seed, now);
    return s;
}

// Lift the finger and wait out the settle time, as a person would.
static uint32_t ready(Session& s, uint32_t now) {
    tick(s, now, false);
    now += SETTLE_MS;
    tick(s, now, false);
    return now;
}

static uint8_t wrongButton(const Session& s) {
    const Group& g = currentGroup(s);
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
        if (i != g.rel.answer) return i;
    }
    return 0;
}

static void testSession() {
    uint32_t now = 1000;
    Session s = freshSession(42, now);
    CHECK(s.op == Op::Add && s.phase == Phase::Play && s.stage == 0 && s.collected == 0,
          "a sitting opens on addition, first group, empty nest");
    CHECK(s.puzzle.level == 1 && !s.armed, "level 1, and not armed until a finger lifts");

    // A finger held from before the group appeared answers nothing.
    const uint8_t right = currentGroup(s).rel.answer;
    tick(s, now, true);
    CHECK(tapNumber(s, right, now) == Tap::Ignored, "a held finger answered");
    tick(s, now + 5000, true);
    CHECK(tapNumber(s, right, now + 5000) == Tap::Ignored, "still held, still nothing");
    // One sample off the glass is not a lift. A resistive panel drops out in
    // the middle of a held press, and the frame after reads as a new press.
    now += 5000;
    tick(s, now, false);
    CHECK(!s.armed, "one sample off the glass armed the group");
    tick(s, now + RELEASE_MS - 1, false);
    CHECK(!s.armed, "armed before the finger had been up for RELEASE_MS");
    tick(s, now + RELEASE_MS - 1, true);   // contact again: it was a dropout
    CHECK(tapNumber(s, right, now + RELEASE_MS) == Tap::Ignored, "a flickering held press answered");
    // A dropout every other frame, for ten seconds, is still one held press.
    for (uint32_t t = 0; t < 10000; t += 40) {
        tick(s, now + 200 + t, false);
        tick(s, now + 220 + t, true);
        CHECK(!s.armed && !controlsArmed(s, now + 220 + t), "a flickering press armed the group");
    }
    now += 11000;
    // Lifted, and it stays lifted.
    tick(s, now, false);
    tick(s, now + RELEASE_MS, false);
    CHECK(s.armed, "a finger off the glass for RELEASE_MS did not arm the group");
    now += RELEASE_MS;
    CHECK(tapNumber(s, right, now) == Tap::Right, "a lifted finger, long after the group appeared, was refused");
    // A tap inside the settle time is one that was already on its way.
    s = freshSession(42, now);
    tick(s, now, false);
    tick(s, now + RELEASE_MS, false);
    CHECK(s.armed && !controlsArmed(s, now + RELEASE_MS), "armed is not the same as settled");
    CHECK(tapNumber(s, right, now + SETTLE_MS - 1) == Tap::Ignored, "a tap inside the settle time counted");
    CHECK(tapNumber(s, right, now + SETTLE_MS) != Tap::Ignored, "a tap after the settle time was refused");
    // A finger that was already well clear when a group appears arms it at
    // the next look: nobody has to wait for a lift that happened long ago.
    s = freshSession(42, now);
    tick(s, now, false);
    tick(s, now + 3000, false);
    deal(s, 43, now + 3000);
    CHECK(!s.armed, "a fresh nest starts unarmed");
    tick(s, now + 3020, false);
    CHECK(s.armed, "a finger long since lifted had to lift again");

    // A whole nest, with one slip in the second group.
    s = freshSession(7, now);
    uint8_t expectLevel = 1;
    for (uint8_t stage = 0; stage <= FINAL_STAGE; ++stage) {
        now = ready(s, now);
        CHECK(s.stage == stage && s.phase == Phase::Play, "stage %u", (unsigned)stage);
        const Group& g = currentGroup(s);
        CHECK(&g == (stage < GROUP_SIZE ? &s.puzzle.prelim[stage] : &s.puzzle.final),
              "stage %u shows the wrong group", (unsigned)stage);
        CHECK(tapNumber(s, GROUP_SIZE, now) == Tap::Ignored, "a button that does not exist");

        if (stage == 1) {
            const uint8_t wrong = wrongButton(s);
            CHECK(tapNumber(s, wrong, now) == Tap::Wrong, "a wrong tap");
            CHECK(s.wrongIndex == wrong && s.slips == 1 && s.mistakes == 1 && s.stage == 1,
                  "a wrong tap marks that button and moves nothing");
            CHECK(!tick(s, now + WRONG_MS - 1, false) && s.wrongIndex == wrong, "the mark cleared early");
            CHECK(tick(s, now + WRONG_MS, false) && s.wrongIndex < 0, "the mark did not clear");
            now += WRONG_MS;
        }
        const Tap result = tapNumber(s, g.rel.answer, now);
        if (stage < FINAL_STAGE) {
            CHECK(result == Tap::Right && s.phase == Phase::Reveal, "stage %u right", (unsigned)stage);
            CHECK(s.collected == stage + 1, "the nest holds %u", (unsigned)s.collected);
            // Nothing can be answered while the equation is showing...
            for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
                CHECK(tapNumber(s, i, now + 10) == Tap::Ignored, "a tap during the reveal counted");
            }
            CHECK(!tick(s, now + REVEAL_MS - 1, false) && s.stage == stage, "the next group came early");
            // ...and a finger resting on the glass as the next group arrives
            // leaves it unarmed.
            now += REVEAL_MS;
            CHECK(tick(s, now, true), "the next group did not arrive");
            CHECK(s.stage == stage + 1 && s.phase == Phase::Play && !s.armed && s.slips == 0,
                  "the next group is up, unarmed, with no slips");
            CHECK(tapNumber(s, currentGroup(s).rel.answer, now + SETTLE_MS) == Tap::Ignored,
                  "the resting finger answered the next group");
        } else {
            CHECK(result == Tap::Finished && s.phase == Phase::Done, "the final group finishes the nest");
            CHECK(!controlsArmed(s, now), "Play again could be pressed by the tap that finished");
            CHECK(tapNumber(s, 0, now + 5000) == Tap::Ignored, "a finished nest took another answer");
        }
    }
    CHECK(s.mistakes == 1 && s.collected == GROUP_SIZE, "one slip, five in the nest");
    // Six groups, one of them not at the first tap: the run was broken at
    // group 2, so four in a row since -- not six, and no promotion.
    CHECK(s.progress.level[0] == expectLevel && !s.promoted, "a nest with a slip does not promote");
    CHECK(s.progress.run[0] == 4, "run is %u", (unsigned)s.progress.run[0]);
    now = ready(s, now);
    CHECK(controlsArmed(s, now), "after lifting, Play again is live");

    // A clean nest is six in a row: it promotes, and the nest AFTER it is
    // dealt at the level earned.
    deal(s, 99, now);
    s.progress.run[0] = 0;
    for (uint8_t stage = 0; stage <= FINAL_STAGE; ++stage) {
        now = ready(s, now);
        const Tap result = tapNumber(s, currentGroup(s).rel.answer, now);
        CHECK(result == (stage < FINAL_STAGE ? Tap::Right : Tap::Finished), "clean stage %u", (unsigned)stage);
        if (stage < FINAL_STAGE) {
            now += REVEAL_MS;
            tick(s, now, false);
        }
    }
    CHECK(s.promoted && s.progress.level[0] == 2 && s.puzzle.level == 1,
          "a clean nest promotes, and was itself played at level 1 throughout");
    CHECK(s.progress.level[1] == 1, "multiplication is still at level 1");
    CHECK(viewOf(s).level == 2 && viewOf(s).status == static_cast<uint8_t>(Status::LevelUp),
          "the finished nest does not announce the level it earned");
    deal(s, 100, now);
    CHECK(s.puzzle.level == 2 && s.puzzle.op == Op::Add && !s.promoted && s.collected == 0,
          "Play again deals level 2 with an empty nest");

    // A run that reaches six PART WAY THROUGH a nest: the tail of one nest and
    // the head of the next. The promotion is earned there, the level on the
    // screen stays the level of the nest in play, and it moves at the end --
    // even though a slip follows.
    {
        Session t = freshSession(11, now);
        uint32_t at = now;
        // Nest one: a slip in the third group, so it ends on a run of three.
        for (uint8_t stage = 0; stage <= FINAL_STAGE; ++stage) {
            at = ready(t, at);
            if (stage == 2) {
                tapNumber(t, wrongButton(t), at);
                at += WRONG_MS;
                tick(t, at, false);
            }
            tapNumber(t, currentGroup(t).rel.answer, at);
            if (stage < FINAL_STAGE) {
                at += REVEAL_MS;
                tick(t, at, false);
            }
        }
        CHECK(t.progress.run[0] == 3 && t.progress.level[0] == 1 && !t.promoted,
              "nest one should end on a run of three at level 1 (run %u)", (unsigned)t.progress.run[0]);
        at = ready(t, at);
        deal(t, 12, at);
        // Nest two: the third answer makes six in a row.
        for (uint8_t stage = 0; stage < 3; ++stage) {
            at = ready(t, at);
            tapNumber(t, currentGroup(t).rel.answer, at);
            at += REVEAL_MS;
            tick(t, at, false);
        }
        CHECK(t.progress.earned[0] && t.progress.level[0] == 1 && viewOf(t).level == 1 &&
                  t.puzzle.level == 1,
              "six in a row mid-nest moved the level before the nest was finished");
        // A slip now does not take it back.
        at = ready(t, at);
        tapNumber(t, wrongButton(t), at);
        at += WRONG_MS;
        tick(t, at, false);
        CHECK(t.progress.earned[0] && t.progress.run[0] == 0, "the slip took the promotion back");
        for (uint8_t stage = 3; stage <= FINAL_STAGE; ++stage) {
            at = ready(t, at);
            const Tap result = tapNumber(t, currentGroup(t).rel.answer, at);
            CHECK(viewOf(t).level == (result == Tap::Finished ? 2 : 1),
                  "the level shown is not the level of the nest in play");
            if (stage < FINAL_STAGE) {
                at += REVEAL_MS;
                tick(t, at, false);
            }
        }
        CHECK(t.promoted && t.progress.level[0] == 2 && !t.progress.earned[0] && t.mistakes == 1,
              "the earned promotion was not applied when the nest finished");
        CHECK(t.progress.run[0] == 0, "the two answers after the slip were carried into level 2");
        // Switching away keeps what multiplication has not earned out of it.
        chooseOp(t, Op::Mul, 13, at);
        CHECK(t.progress.level[1] == 1 && !t.progress.earned[1], "addition's promotion leaked");
    }

    // Switching operation: a fresh nest, nothing collected, the press consumed,
    // and each operation's progress kept exactly as it was.
    now = ready(s, now);
    tapNumber(s, currentGroup(s).rel.answer, now);
    now += REVEAL_MS;
    tick(s, now, false);
    CHECK(s.collected == 1 && s.stage == 1, "one answer in the nest before switching");
    const Progress before = s.progress;
    CHECK(!chooseOp(s, Op::Add, 5, now), "choosing the operation in play did something");
    CHECK(s.collected == 1 && s.stage == 1, "and it left the nest alone");
    CHECK(chooseOp(s, Op::Mul, 5, now), "switching to multiplication");
    CHECK(s.op == Op::Mul && s.puzzle.op == Op::Mul && s.puzzle.level == 1, "a level 1 multiplication nest");
    CHECK(s.stage == 0 && s.collected == 0 && s.mistakes == 0 && s.wrongIndex < 0 &&
              s.phase == Phase::Play,
          "the switch cleared the nest");
    CHECK(std::memcmp(&before, &s.progress, sizeof(before)) == 0, "switching spent some progress");
    CHECK(!s.armed, "the press that switched is still down");
    tick(s, now, true);
    CHECK(tapNumber(s, currentGroup(s).rel.answer, now + SETTLE_MS + 1) == Tap::Ignored,
          "the switching press answered the first group");
    CHECK(chooseOp(s, Op::Add, 6, now) && s.puzzle.level == 2 && s.puzzle.op == Op::Add,
          "back to addition, still at the level it had earned");

    // The level control: per operation, wrapping, a fresh nest each time.
    chooseNextLevel(s, 8, now);
    CHECK(s.progress.level[0] == 3 && s.puzzle.level == 3 && s.progress.level[1] == 1 &&
              s.collected == 0 && !s.armed,
          "the level control steps addition only");

    // A millisecond counter that wraps must not freeze or skip the reveal.
    s = freshSession(3, 0xFFFFFF00u);
    uint32_t t = ready(s, 0xFFFFFF00u);
    CHECK(tapNumber(s, currentGroup(s).rel.answer, t) == Tap::Right, "right before the wrap");
    CHECK(!tick(s, t + 100, false), "100 ms into the reveal");
    CHECK(tick(s, t + REVEAL_MS, false) && s.stage == 1, "the reveal ended across the wrap (t=%lu)",
          (unsigned long)(t + REVEAL_MS));
}

// ---- the gate every control shares -----------------------------------------

static void testInput() {
    // A WRONG TAP ENDS THE RUN THERE AND THEN. Otherwise leaving the group --
    // switching operation, stepping the level -- would keep a run with a slip
    // in it, and five more first-tap answers would promote.
    uint32_t now = 2000;
    Session s = freshSession(77, now);
    for (uint8_t stage = 0; stage < 4; ++stage) {
        now = ready(s, now);
        CHECK(tapNumber(s, currentGroup(s).rel.answer, now) == Tap::Right, "set-up");
        now += REVEAL_MS;
        tick(s, now, false);
    }
    CHECK(s.progress.run[0] == 4, "four in a row");
    now = ready(s, now);
    CHECK(tapNumber(s, wrongButton(s), now) == Tap::Wrong, "a slip");
    CHECK(s.progress.run[0] == 0, "the run outlived the wrong tap (%u)", (unsigned)s.progress.run[0]);
    // Walk away from the group without solving it, and come back.
    chooseOp(s, Op::Mul, 78, now);
    chooseOp(s, Op::Add, 79, now);
    CHECK(s.progress.run[0] == 0 && !s.progress.earned[0], "leaving the group hid the slip");
    for (uint8_t stage = 0; stage < 2; ++stage) {
        now = ready(s, now);
        tapNumber(s, currentGroup(s).rel.answer, now);
        now += REVEAL_MS;
        tick(s, now, false);
    }
    CHECK(s.progress.run[0] == 2 && !s.progress.earned[0],
          "four before the slip and two after made six in a row");
    // A wrong tap does not take back a promotion already earned.
    s.progress.earned[0] = true;
    now = ready(s, now);
    tapNumber(s, wrongButton(s), now);
    CHECK(s.progress.earned[0], "a slip took back an earned promotion");
    // And multiplication's run is no business of an addition slip.
    s.progress.run[1] = 3;
    now += WRONG_MS;
    tick(s, now, false);
    tapNumber(s, wrongButton(s), now);
    CHECK(s.progress.run[1] == 3, "an addition slip ended multiplication's run");

    // THE LEVEL CONTROL, HELD. Each step deals a nest and disarms; a press
    // that is held, and flickers, must not step twice.
    s = freshSession(5, now);
    now = ready(s, now);
    CHECK(controlsArmed(s, now), "ready");
    tick(s, now, true);                       // the press
    chooseNextLevel(s, 6, now);
    CHECK(s.progress.level[0] == 2 && !controlsArmed(s, now), "one step, and the gate closes");
    for (uint32_t t = 20; t < 3000; t += 40) {
        tick(s, now + t, false);              // a dropout...
        tick(s, now + t + 20, true);          // ...and contact again
        CHECK(!controlsArmed(s, now + t + 20), "a held, flickering press could step the level again");
    }
    now += 3000;
    tick(s, now, false);
    CHECK(!controlsArmed(s, now + RELEASE_MS - 1), "the gate opened before the release was complete");
    tick(s, now + RELEASE_MS, false);
    CHECK(controlsArmed(s, now + RELEASE_MS), "the gate never reopened after a real lift");

    // THE RULES PAGE. The nest is not ticked while it is up -- only the
    // finger is watched -- and the press that opened it cannot close it.
    s = freshSession(9, now);
    now = ready(s, now);
    const View before = viewOf(s);
    tick(s, now, true);                       // the press on Rules
    holdInput(s, now);
    CHECK(!controlsArmed(s, now), "the opening press could close the page");
    for (uint32_t t = 20; t < 2000; t += 20) {
        watchFinger(s, now + t, true);
        CHECK(!controlsArmed(s, now + t), "a held press closed the rules page");
    }
    now += 2000;
    watchFinger(s, now, false);
    watchFinger(s, now + 20, true);           // flicker
    CHECK(!controlsArmed(s, now + 20), "a dropout closed the rules page");
    watchFinger(s, now + 40, false);
    watchFinger(s, now + 40 + RELEASE_MS, false);
    CHECK(controlsArmed(s, now + 40 + RELEASE_MS), "the rules page cannot be closed after a lift");
    now += 40 + RELEASE_MS;
    watchFinger(s, now, true);                // the press that closes it
    holdInput(s, now);
    CHECK(tapNumber(s, currentGroup(s).rel.answer, now + SETTLE_MS) == Tap::Ignored,
          "the press that closed the rules answered the group underneath");
    CHECK(changedRegions(before, viewOf(s)) == 0, "the rules page moved the nest on");
    // Lifted quickly: armed after RELEASE_MS, but a tap inside the settle
    // time of the page closing was already on its way.
    tick(s, now + 20, false);
    tick(s, now + 20 + RELEASE_MS, false);
    CHECK(s.armed && tapNumber(s, currentGroup(s).rel.answer, now + SETTLE_MS - 1) == Tap::Ignored,
          "a tap inside the settle time after the rules page counted");
    CHECK(tapNumber(s, currentGroup(s).rel.answer, now + SETTLE_MS) == Tap::Right,
          "the nest did not come back after the rules page");

    // ONE HELD PRESS ON A WRONG NUMBER IS ONE SLIP. The contact drops for a
    // frame in the middle of it and comes back at the same place; the gate
    // closed when the tap was taken, so the second "press" is not one.
    s = freshSession(21, now);
    now = ready(s, now);
    tick(s, now, true);                       // the press lands
    const uint8_t held = wrongButton(s);
    CHECK(tapNumber(s, held, now) == Tap::Wrong && s.mistakes == 1, "set-up: one wrong tap");
    CHECK(!s.armed, "a wrong tap left the gate open behind it");
    for (uint32_t t = 20; t < 400; t += 20) {
        const bool contact = t != 100;        // lost on one frame only
        tick(s, now + t, contact);
        if (t == 120) {
            CHECK(tapNumber(s, held, now + t) == Tap::Ignored, "a flicker on a wrong number was a second slip");
        }
    }
    CHECK(s.mistakes == 1 && s.slips == 1, "one held press counted %u slips", (unsigned)s.mistakes);
    // Lifted for long enough, the next tap counts -- the right one included.
    now += 400;
    tick(s, now, false);
    tick(s, now + RELEASE_MS, false);
    CHECK(tapNumber(s, currentGroup(s).rel.answer, now + RELEASE_MS) == Tap::Right,
          "after a wrong tap and a real lift, the right answer was refused");
    // And the right answer closes the gate too: the controls stay shut to
    // the press that made it, through the reveal.
    CHECK(!s.armed && !controlsArmed(s, now + RELEASE_MS + SETTLE_MS),
          "a right tap left the gate open for the press that made it");

    // A WRONG MARK DOES NOT OUTLIVE THE PICTURE IT WAS MADE ON. The nest's
    // clock stops while the rules page is up, so a mark left standing would
    // be painted again when the page closed, however long that was.
    s = freshSession(10, now);
    now = ready(s, now);
    const uint8_t slip = wrongButton(s);
    CHECK(tapNumber(s, slip, now) == Tap::Wrong && viewOf(s).status == static_cast<uint8_t>(Status::TryAgain),
          "set-up: a wrong mark is showing");
    holdInput(s, now + 100);                  // Rules, pressed inside WRONG_MS
    CHECK(s.wrongIndex < 0 && viewOf(s).status == static_cast<uint8_t>(Status::Prompt) &&
              viewOf(s).numberLook[slip] == static_cast<uint8_t>(NumberLook::Idle),
          "the wrong mark survived the rules page opening");
    CHECK(s.slips == 1 && s.mistakes == 1, "dropping the mark forgot the slip itself");

    // A millisecond counter that wraps must not hold the gate shut.
    s = freshSession(3, 0xFFFFFFC0u);
    tick(s, 0xFFFFFFC0u, true);
    tick(s, 0xFFFFFFE0u, false);
    tick(s, 0xFFFFFFE0u + RELEASE_MS, false);   // past the wrap
    CHECK(s.armed, "the release was not measured across the wrap");
    CHECK(controlsArmed(s, 0xFFFFFFC0u + SETTLE_MS), "the settle time was not measured across the wrap");
}

// ---- what gets repainted ----------------------------------------------------

static int bitsSet(uint16_t mask) {
    int n = 0;
    for (; mask; mask = static_cast<uint16_t>(mask & (mask - 1))) ++n;
    return n;
}

static uint16_t numbersMask(uint16_t mask) { return mask & static_cast<uint16_t>(0x1F << 5); }
static uint16_t slotsMask(uint16_t mask) { return mask & static_cast<uint16_t>(0x1F << 10); }

static void testRepaint() {
    uint32_t now = 500;
    Session s = freshSession(2024, now);
    View shown = viewOf(s);
    CHECK(changedRegions(shown, shown) == 0, "a view differs from itself");

    // IDLE: frame after frame with nothing happening names no region at all.
    for (int frame = 0; frame < 500; ++frame) {
        now += 20;
        const bool changed = tick(s, now, false);
        const View next = viewOf(s);
        CHECK(!changed && changedRegions(shown, next) == 0, "an idle frame repaints %04x",
              (unsigned)changedRegions(shown, next));
    }
    // A finger resting on the glass, away from anything, is idle too.
    for (int frame = 0; frame < 50; ++frame) {
        now += 20;
        CHECK(!tick(s, now, true) && changedRegions(shown, viewOf(s)) == 0, "a resting finger repaints");
    }
    now = ready(s, now);
    CHECK(changedRegions(shown, viewOf(s)) == 0, "arming the group repaints something");

    // WRONG TAP: that one button and the status line. Nothing else.
    const uint8_t wrong = wrongButton(s);
    CHECK(tapNumber(s, wrong, now) == Tap::Wrong, "wrong");
    View next = viewOf(s);
    uint16_t mask = changedRegions(shown, next);
    CHECK(mask == (numberRegion(wrong) | REGION_STATUS), "a wrong tap repaints %04x", (unsigned)mask);
    CHECK(next.numberLook[wrong] == static_cast<uint8_t>(NumberLook::Wrong) &&
              next.numberLook[currentGroup(s).rel.answer] == static_cast<uint8_t>(NumberLook::Idle),
          "the pressed button is marked, and the right one is NOT given away");
    shown = next;
    // The same wrong button again changes nothing on the panel.
    tapNumber(s, wrong, now + 50);
    CHECK(changedRegions(shown, viewOf(s)) == 0, "pressing the marked button again repaints");
    // The mark clearing puts back the same two regions.
    now += 50 + WRONG_MS;
    CHECK(tick(s, now, false), "the mark cleared");
    next = viewOf(s);
    mask = changedRegions(shown, next);
    CHECK(mask == (numberRegion(wrong) | REGION_STATUS), "clearing the mark repaints %04x", (unsigned)mask);
    shown = next;

    // RIGHT TAP: the answer, its two operands, the status line and one slot.
    const Group g = currentGroup(s);
    CHECK(tapNumber(s, g.rel.answer, now) == Tap::Right, "right");
    next = viewOf(s);
    mask = changedRegions(shown, next);
    const uint16_t want = static_cast<uint16_t>(numberRegion(g.rel.answer) | numberRegion(g.rel.first) |
                                                numberRegion(g.rel.second) | REGION_STATUS | slotRegion(0));
    CHECK(mask == want, "a right tap repaints %04x, expected %04x", (unsigned)mask, (unsigned)want);
    CHECK(next.status == static_cast<uint8_t>(Status::Equation) &&
              next.equation[0] <= next.equation[1] &&
              combine(s.op, next.equation[0], next.equation[1]) == next.equation[2] &&
              next.equation[2] == g.value[g.rel.answer],
          "the equation on show is not the one that was solved");
    CHECK(next.slot[0] == g.value[g.rel.answer] &&
              next.slotLook[0] == static_cast<uint8_t>(SlotLook::Fresh),
          "the answer did not drop into the first slot");
    shown = next;
    // Holding the equation is idle.
    for (uint32_t dt = 20; dt < REVEAL_MS; dt += 20) {
        CHECK(!tick(s, now + dt, false) && changedRegions(shown, viewOf(s)) == 0,
              "the reveal repaints while it waits");
    }

    // NEXT GROUP: buttons, status, caption, and the one slot that settles.
    // Never the mode buttons, the level, the footer or an untouched slot.
    now += REVEAL_MS;
    CHECK(tick(s, now, false), "next group");
    next = viewOf(s);
    mask = changedRegions(shown, next);
    CHECK((mask & (REGION_MODE | REGION_LEVEL | REGION_FOOTER)) == 0,
          "a fresh group repaints the controls: %04x", (unsigned)mask);
    CHECK(slotsMask(mask) == slotRegion(0), "a fresh group repaints slots %04x", (unsigned)slotsMask(mask));
    CHECK((mask & REGION_STATUS) && (mask & REGION_CAPTION), "status and caption follow the group");
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
        const bool differs = shown.number[i] != next.number[i] || shown.numberLook[i] != next.numberLook[i];
        CHECK(((mask & numberRegion(i)) != 0) == differs, "button %u repaint is not what changed", (unsigned)i);
    }
    shown = next;

    // Through to the final group: each step names only what it must.
    while (s.stage < FINAL_STAGE) {
        now = ready(s, now);
        tapNumber(s, currentGroup(s).rel.answer, now);
        shown = viewOf(s);
        now += REVEAL_MS;
        tick(s, now, false);
        next = viewOf(s);
        mask = changedRegions(shown, next);
        CHECK((mask & (REGION_MODE | REGION_LEVEL | REGION_FOOTER)) == 0, "controls repainted mid-nest");
        CHECK(bitsSet(slotsMask(mask)) == 1, "more than the settling slot repainted");
        shown = next;
    }
    // The final group IS the nest: same numbers, same order, in the buttons.
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
        CHECK(shown.number[i] == shown.slot[i] && shown.slot[i] == s.puzzle.final.value[i],
              "the final group is not the nest, in order");
        CHECK(shown.slotLook[i] == static_cast<uint8_t>(SlotLook::Filled), "slot %u", (unsigned)i);
    }

    // FINISHING: the footer appears exactly once, with the caption and status.
    now = ready(s, now);
    CHECK(tapNumber(s, s.puzzle.final.rel.answer, now) == Tap::Finished, "finished");
    next = viewOf(s);
    mask = changedRegions(shown, next);
    CHECK((mask & REGION_FOOTER) && (mask & REGION_STATUS) && (mask & REGION_CAPTION) && next.done,
          "finishing did not bring up the footer: %04x", (unsigned)mask);
    // The finished nest states the final equation: the caption painter reads
    // it from the view, so the view must carry it.
    CHECK(combine(s.op, next.equation[0], next.equation[1]) == next.equation[2] &&
              next.equation[2] == s.puzzle.final.value[s.puzzle.final.rel.answer] &&
              next.equation[0] <= next.equation[1],
          "a finished nest does not carry its final equation");
    {
        View other = next;
        other.equation[0] = static_cast<uint16_t>(other.equation[0] + 1);
        CHECK(changedRegions(other, next) & REGION_CAPTION,
              "a different final equation would not repaint the caption that states it");
    }
    CHECK((mask & (REGION_MODE | REGION_LEVEL)) == 0 || next.level != shown.level,
          "finishing repainted the mode buttons");
    CHECK(slotsMask(mask) == 0, "finishing repainted the nest");
    CHECK(bitsSet(numbersMask(mask)) == 3, "finishing repainted %d buttons", bitsSet(numbersMask(mask)));
    shown = next;
    for (int frame = 0; frame < 200; ++frame) {
        now += 20;
        CHECK(!tick(s, now, false) && changedRegions(shown, viewOf(s)) == 0, "a finished nest repaints");
    }

    // SWITCHING OPERATION: the mode buttons, and what the fresh nest changes.
    // The level control is repainted only if the two levels differ.
    now = ready(s, now);
    const uint8_t levelShown = shown.level;
    CHECK(chooseOp(s, Op::Mul, 31337, now), "switch");
    next = viewOf(s);
    mask = changedRegions(shown, next);
    CHECK((mask & REGION_MODE) && (mask & REGION_STATUS) && (mask & REGION_CAPTION) &&
              (mask & REGION_FOOTER),
          "a switch did not repaint what it changed: %04x", (unsigned)mask);
    CHECK(((mask & REGION_LEVEL) != 0) == (next.level != levelShown), "the level control repaint");
    CHECK(slotsMask(mask) == static_cast<uint16_t>(0x1F << 10), "a switch must empty all five slots");
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
        CHECK(next.slot[i] == 0 && next.slotLook[i] == static_cast<uint8_t>(SlotLook::Empty),
              "slot %u survived the switch", (unsigned)i);
    }
    shown = next;
    CHECK(!chooseOp(s, Op::Mul, 1, now) && changedRegions(shown, viewOf(s)) == 0,
          "pressing the selected mode repaints");

    // A full repaint is every region and nothing outside the set.
    CHECK(bitsSet(REGION_ALL) == 15, "REGION_ALL has %d regions", bitsSet(REGION_ALL));
    uint16_t every = REGION_MODE | REGION_LEVEL | REGION_STATUS | REGION_CAPTION | REGION_FOOTER;
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) every |= static_cast<uint16_t>(numberRegion(i) | slotRegion(i));
    CHECK(every == REGION_ALL, "the regions do not add up to REGION_ALL");
}

// Thousands of whole sittings driven by a seeded player who sometimes slips,
// with the promises checked after every event.
static void testManySittings() {
    for (uint32_t run = 0; run < 1500; ++run) {
        uint32_t now = mix(run) & 0xFFFFu;
        uint32_t dice = mix(run + 0x1234567u);
        Session s = freshSession(mix(run * 7919u + 1u), now);
        if (run & 1) chooseOp(s, Op::Mul, mix(run + 5u), now);
        View shown = viewOf(s);
        uint32_t taps = 0;
        uint8_t nests = 0;
        while (nests < 4 && taps < 400) {
            now = ready(s, now);
            shown = viewOf(s);
            if (s.phase == Phase::Done) {
                CHECK(s.collected == GROUP_SIZE && s.stage == FINAL_STAGE, "a finished nest is not full");
                ++nests;
                deal(s, mix(run * 31u + nests), now);
                CHECK(puzzleValid(s.puzzle) && s.puzzle.level == s.progress.level[opIndex(s.op)],
                      "a dealt nest is not at the level the player has reached");
                continue;
            }
            dice = mix(dice + taps);
            const Group g = currentGroup(s);
            const bool slip = (dice % 7) == 0;
            const uint8_t index = slip ? wrongButton(s) : g.rel.answer;
            const uint8_t levelBefore = s.progress.level[opIndex(s.op)];
            for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
                CHECK(shown.number[i] == g.value[i], "button %u shows %u and is judged as %u",
                      (unsigned)i, (unsigned)shown.number[i], (unsigned)g.value[i]);
            }
            const Tap result = tapNumber(s, index, now);
            ++taps;
            if (result == Tap::Right || result == Tap::Finished) {
                const View solved = viewOf(s);
                CHECK(solved.equation[0] <= solved.equation[1] &&
                          combine(s.op, solved.equation[0], solved.equation[1]) == solved.equation[2] &&
                          solved.equation[2] == g.value[g.rel.answer],
                      "the equation on show is %u, %u, %u", (unsigned)solved.equation[0],
                      (unsigned)solved.equation[1], (unsigned)solved.equation[2]);
                CHECK(solved.mistakes == s.mistakes, "the view lost count of the slips");
            }
            CHECK(result == (slip ? Tap::Wrong : (s.phase == Phase::Done ? Tap::Finished : Tap::Right)),
                  "a tap was judged wrongly");
            CHECK(s.progress.level[opIndex(s.op)] >= levelBefore, "a level went down");
            CHECK(s.collected <= GROUP_SIZE && s.stage <= FINAL_STAGE, "ran past the end");
            const View next = viewOf(s);
            const uint16_t mask = changedRegions(shown, next);
            CHECK((mask & REGION_MODE) == 0, "a number tap repainted the mode buttons");
            if (slip) {
                CHECK(bitsSet(numbersMask(mask)) <= 2 && slotsMask(mask) == 0,
                      "a wrong tap repainted %04x", (unsigned)mask);
            }
            if (s.phase == Phase::Reveal) {
                now += REVEAL_MS;
                tick(s, now, false);
            }
        }
        CHECK(nests == 4, "sitting %lu did not finish four nests in 400 taps", (unsigned long)run);
    }
}

// ---- the validators, on groups made to break exactly one rule ---------------

static Group groupOf(const uint16_t* v, uint8_t first, uint8_t second, uint8_t answer) {
    Group g{};
    for (int i = 0; i < GROUP_SIZE; ++i) g.value[i] = v[i];
    g.rel = Relation{first, second, answer};
    return g;
}

static void testValidators() {
    // THREE DISTINCT POSITIONS. A number is never its own operand: 0 + 5 = 5
    // and 1 x 5 = 5 are not relations, with the idle operand on either side.
    const uint16_t selfAddA[GROUP_SIZE] = {0, 5, 7, 9, 11};
    const uint16_t selfAddB[GROUP_SIZE] = {5, 0, 7, 9, 11};
    const uint16_t selfMulA[GROUP_SIZE] = {1, 5, 7, 9, 11};
    const uint16_t selfMulB[GROUP_SIZE] = {5, 1, 7, 9, 11};
    CHECK(countRelations(Op::Add, selfAddA, GROUP_SIZE, nullptr) == 0, "0 + 5 = 5 was counted");
    CHECK(countRelations(Op::Add, selfAddB, GROUP_SIZE, nullptr) == 0, "5 + 0 = 5 was counted");
    CHECK(countRelations(Op::Mul, selfMulA, GROUP_SIZE, nullptr) == 0, "1 x 5 = 5 was counted");
    CHECK(countRelations(Op::Mul, selfMulB, GROUP_SIZE, nullptr) == 0, "5 x 1 = 5 was counted");
    // Every third position is tried for a pair, not only the first that fits.
    const uint16_t twice[GROUP_SIZE] = {2, 3, 5, 5, 9};
    CHECK(countRelations(Op::Add, twice, GROUP_SIZE, nullptr) == 2, "2 + 3 = 5, twice over");
    // Fewer than five values: only the ones given are looked at.
    CHECK(countRelations(Op::Add, twice, 3, nullptr) == 1 && countRelations(Op::Add, twice, 2, nullptr) == 0,
          "countRelations() read past the count it was given");

    const Limits& add1 = LIMITS[0][0];   // operands 1..9, nothing above 20
    const Limits& add2 = LIMITS[0][1];   // operands 1..20, nothing above 40
    const Limits& add3 = LIMITS[0][2];   // operands 2..50, nothing below 2 or above 100

    // A repeated bystander, with the one relation intact.
    const uint16_t repeated[GROUP_SIZE] = {3, 7, 10, 12, 12};
    CHECK(countRelations(Op::Add, repeated, GROUP_SIZE, nullptr) == 1, "set-up");
    CHECK(!groupValid(groupOf(repeated, 0, 1, 2), Op::Add, add2), "a repeated bystander is valid");
    // A number under the smallest the level shows.
    const uint16_t tooSmall[GROUP_SIZE] = {1, 20, 30, 50, 77};
    CHECK(countRelations(Op::Add, tooSmall, GROUP_SIZE, nullptr) == 1, "set-up");
    CHECK(!groupValid(groupOf(tooSmall, 1, 2, 3), Op::Add, add3), "a 1 at a level that starts at 2 is valid");
    // An operand over the operand limit -- the first of the pair, then the second.
    const uint16_t bigFirst[GROUP_SIZE] = {10, 5, 15, 3, 19};
    const uint16_t bigSecond[GROUP_SIZE] = {5, 10, 15, 3, 19};
    CHECK(countRelations(Op::Add, bigFirst, GROUP_SIZE, nullptr) == 1, "set-up");
    CHECK(!groupValid(groupOf(bigFirst, 0, 1, 2), Op::Add, add1), "a first operand of 10 is valid at level 1");
    CHECK(!groupValid(groupOf(bigSecond, 0, 1, 2), Op::Add, add1), "a second operand of 10 is valid at level 1");
    CHECK(groupValid(groupOf(bigFirst, 0, 1, 2), Op::Add, add2), "the same group is fine where operands reach 20");
    // Two relations are not one, even when the relation recorded is one of them.
    const uint16_t twoAnswers[GROUP_SIZE] = {2, 3, 5, 8, 20};
    Relation last{9, 9, 9};
    CHECK(countRelations(Op::Add, twoAnswers, GROUP_SIZE, &last) == 2, "set-up");
    CHECK(!groupValid(groupOf(twoAnswers, last.first, last.second, last.answer), Op::Add, add2),
          "a group with two answers is valid when one of them is the recorded one");
    // The recorded positions: each of the three has to be right.
    const uint16_t fine[GROUP_SIZE] = {3, 7, 10, 12, 18};
    CHECK(groupValid(groupOf(fine, 0, 1, 2), Op::Add, add2), "set-up");
    CHECK(!groupValid(groupOf(fine, 0, 3, 2), Op::Add, add2), "the wrong second operand is recorded");
    CHECK(!groupValid(groupOf(fine, 3, 1, 2), Op::Add, add2), "the wrong first operand is recorded");
    CHECK(!groupValid(groupOf(fine, 0, 1, 4), Op::Add, add2), "the wrong answer is recorded");

    // A WHOLE NEST, broken one way at a time. Each stored nest is valid; each
    // of these is that nest with a single thing wrong.
    for (Op op : OPS) {
        for (uint8_t level = 1; level <= LEVEL_COUNT; ++level) {
            const Puzzle good = fallbackPuzzle(op, level);
            CHECK(puzzleValid(good), "set-up");
            // Two groups in each other's place: every group still valid, and
            // the nest no longer the answers in order.
            for (int a = 0; a + 1 < GROUP_SIZE; ++a) {
                Puzzle p = good;
                const Group held = p.prelim[a];
                p.prelim[a] = p.prelim[a + 1];
                p.prelim[a + 1] = held;
                CHECK(!puzzleValid(p), "%s level %u: groups %d and %d swapped is still a nest",
                      OP_NAME[opIndex(op)], (unsigned)level, a, a + 1);
            }
            // The final group recorded wrongly. The values, and so the
            // carry-forward, are untouched.
            {
                Puzzle p = good;
                for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
                    if (i != p.final.rel.first && i != p.final.rel.second && i != p.final.rel.answer) {
                        p.final.rel.answer = i;
                        break;
                    }
                }
                CHECK(!puzzleValid(p), "%s level %u: a wrongly recorded final group is still a nest",
                      OP_NAME[opIndex(op)], (unsigned)level);
            }
            // Each preliminary group in turn with one bystander repeated.
            for (int g = 0; g < GROUP_SIZE; ++g) {
                Puzzle p = good;
                Group& group = p.prelim[g];
                int spare[2] = {-1, -1};
                int found = 0;
                for (int i = 0; i < GROUP_SIZE; ++i) {
                    if (i != group.rel.first && i != group.rel.second && i != group.rel.answer) {
                        spare[found++] = i;
                    }
                }
                group.value[spare[0]] = group.value[spare[1]];
                CHECK(!puzzleValid(p), "%s level %u: group %d with a repeated number is still a nest",
                      OP_NAME[opIndex(op)], (unsigned)level, g);
            }
            // A nest filed under the wrong level's limits: the hardest stored
            // nest of an operation does not fit the easiest level.
            if (level == LEVEL_COUNT) {
                Puzzle p = good;
                p.level = 1;
                CHECK(!puzzleValid(p), "%s: the level 5 nest passes for level 1", OP_NAME[opIndex(op)]);
            }
        }
    }
}

// ---- each operation keeps its own -------------------------------------------

static void testPerOperation() {
    for (Op op : OPS) {
        const uint8_t me = opIndex(op);
        const uint8_t other = static_cast<uint8_t>(1 - me);
        const Op otherOp = OPS[other];
        const char* name = OP_NAME[me];

        Progress p;
        resetProgress(p);
        p.run[other] = 4;
        p.level[other] = 3;
        for (int i = 0; i < 3; ++i) recordGroup(p, op, true);
        CHECK(p.run[me] == 3 && p.run[other] == 4, "%s: answers were counted against the other operation", name);
        recordGroup(p, op, false);
        CHECK(p.run[me] == 0 && p.run[other] == 4, "%s: a slip ended the other operation's run", name);
        p.run[me] = 2;
        CHECK(cycleLevel(p, op) == 2, "%s: the level control", name);
        CHECK(p.level[me] == 2 && p.run[me] == 0 && p.level[other] == 3 && p.run[other] == 4,
              "%s: stepping the level reached the other operation", name);
        // A promotion belongs to the operation that earned it.
        p.earned[other] = true;
        CHECK(!finishNest(p, op) && p.earned[other] && p.level[other] == 3 && p.level[me] == 2,
              "%s: finishing a nest spent the other operation's promotion", name);
        CHECK(finishNest(p, otherOp) && p.level[other] == 4 && p.level[me] == 2,
              "%s: the other operation's promotion was lost", name);
        // The top level: nothing more to earn, and nothing more to apply.
        p.level[me] = LEVEL_COUNT;
        p.run[me] = 0;
        p.earned[me] = false;
        for (int i = 0; i < PROMOTE_RUN * 3; ++i) {
            CHECK(!recordGroup(p, op, true) && !p.earned[me], "%s: a promotion was earned at the top level", name);
        }
        p.earned[me] = true;   // as a stored oddity might leave it
        CHECK(!finishNest(p, op) && p.level[me] == LEVEL_COUNT && !p.earned[me],
              "%s: a level above the top one was reached", name);

        // The same, through a sitting.
        uint32_t now = 5000;
        Session s = freshSession(900u + me, now);
        if (op == Op::Mul) chooseOp(s, Op::Mul, 901, now);
        s.progress.level[other] = 4;
        s.progress.run[other] = 3;
        s.progress.run[me] = 2;
        CHECK(viewOf(s).op == me && viewOf(s).level == 1 && s.puzzle.op == op,
              "%s: the level on show is not this operation's", name);
        // A right answer counts toward this operation's run...
        now = ready(s, now);
        CHECK(tapNumber(s, currentGroup(s).rel.answer, now) == Tap::Right, "%s: set-up", name);
        CHECK(s.progress.run[me] == 3 && s.progress.run[other] == 3,
              "%s: a right answer was counted against the wrong operation (%u, %u)", name,
              (unsigned)s.progress.run[me], (unsigned)s.progress.run[other]);
        now += REVEAL_MS;
        tick(s, now, false);
        // ...and a wrong tap ends this operation's run and no other.
        s.progress.run[other] = 3;
        now = ready(s, now);
        CHECK(tapNumber(s, wrongButton(s), now) == Tap::Wrong, "%s: set-up", name);
        CHECK(s.progress.run[me] == 0 && s.progress.run[other] == 3,
              "%s: a wrong tap ended the wrong operation's run", name);
        chooseNextLevel(s, 902, now);
        CHECK(s.progress.level[me] == 2 && s.progress.level[other] == 4 && s.puzzle.level == 2 &&
                  s.puzzle.op == op && viewOf(s).level == 2,
              "%s: the level control stepped the wrong operation", name);
        // Away and back spends nothing of either: levels, runs, and a
        // promotion earned and not yet applied.
        s.progress.run[me] = 5;
        s.progress.earned[me] = true;
        const Progress before = s.progress;
        CHECK(chooseOp(s, otherOp, 903, now) && std::memcmp(&before, &s.progress, sizeof(before)) == 0,
              "%s: switching away spent some progress", name);
        CHECK(s.puzzle.level == 4 && viewOf(s).level == 4, "%s: the other operation's level is not 4", name);
        CHECK(chooseOp(s, op, 904, now) && std::memcmp(&before, &s.progress, sizeof(before)) == 0,
              "%s: switching back spent some progress", name);

        // A whole clean nest in this operation promotes THIS operation: the
        // sixth answer earns it and finishing the nest applies it, here and
        // nowhere else -- and a promotion the other operation is owed stays
        // owed.
        Session t = freshSession(950u + me, now);
        if (op == Op::Mul) chooseOp(t, Op::Mul, 951, now);
        t.progress.level[other] = 3;
        t.progress.earned[other] = true;
        for (uint8_t stage = 0; stage <= FINAL_STAGE; ++stage) {
            now = ready(t, now);
            const Tap result = tapNumber(t, currentGroup(t).rel.answer, now);
            CHECK(result == (stage < FINAL_STAGE ? Tap::Right : Tap::Finished), "%s: clean stage %u", name,
                  (unsigned)stage);
            if (stage < FINAL_STAGE) {
                now += REVEAL_MS;
                tick(t, now, false);
            }
        }
        CHECK(t.promoted && t.progress.level[me] == 2 && !t.progress.earned[me],
              "%s: a clean nest did not promote the operation it was played in", name);
        CHECK(t.progress.level[other] == 3 && t.progress.earned[other],
              "%s: a clean nest spent the other operation's promotion", name);
        CHECK(viewOf(t).status == static_cast<uint8_t>(Status::LevelUp) && viewOf(t).level == 2,
              "%s: the finished nest does not announce its own level", name);
        // The tap that finished the nest closes the gate like any other: no
        // settle time alone lets Play again through while it is still down.
        CHECK(!t.armed && !controlsArmed(t, now + SETTLE_MS + 10),
              "%s: the finishing tap left the gate open for Play again", name);
    }
}

// ---- the flow, at the places a small mistake hides ---------------------------

static void testSessionDetails() {
    uint32_t now = 20000;

    // THE SEED DECIDES THE NEST. Play again must not deal the same one.
    Session s = freshSession(1, now);
    const Puzzle first = s.puzzle;
    deal(s, 2, now);
    CHECK(!samePuzzle(first, s.puzzle), "two seeds dealt one nest");
    deal(s, 1, now);
    CHECK(samePuzzle(first, s.puzzle), "one seed dealt two nests");

    // A wrong mark does not follow into the next nest.
    now = ready(s, now);
    CHECK(tapNumber(s, wrongButton(s), now) == Tap::Wrong && s.wrongIndex >= 0, "set-up");
    deal(s, 3, now + 100);
    CHECK(s.wrongIndex < 0 && viewOf(s).status == static_cast<uint8_t>(Status::Prompt),
          "a wrong mark was carried into a fresh nest");

    // A right tap clears a wrong mark that is still showing.
    now = ready(s, now + 100);
    CHECK(tapNumber(s, wrongButton(s), now) == Tap::Wrong, "set-up");
    tick(s, now + 20, false);
    tick(s, now + 20 + RELEASE_MS, false);
    uint32_t solvedAt = now + 20 + RELEASE_MS;
    CHECK(solvedAt - now < WRONG_MS, "set-up: the mark must still be showing");
    CHECK(tapNumber(s, currentGroup(s).rel.answer, solvedAt) == Tap::Right, "set-up");
    CHECK(s.wrongIndex < 0, "a right tap left the wrong mark standing");
    {
        const View v = viewOf(s);
        for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
            CHECK(v.numberLook[i] != static_cast<uint8_t>(NumberLook::Wrong),
                  "a button still looks wrong beside the equation that solved the group");
        }
    }

    // NOTHING IS ANSWERED DURING THE REVEAL -- not even by a finger that has
    // lifted and waited, for which every other control is live again.
    tick(s, solvedAt + 20, false);
    tick(s, solvedAt + 20 + RELEASE_MS, false);
    const uint32_t during = solvedAt + SETTLE_MS + 50;
    CHECK(during - solvedAt < REVEAL_MS && s.phase == Phase::Reveal && controlsArmed(s, during),
          "set-up: armed, settled, and the equation still showing");
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
        CHECK(tapNumber(s, i, during) == Tap::Ignored, "a number was answered during the reveal");
    }
    CHECK(s.phase == Phase::Reveal && s.stage == 0 && s.collected == 1, "the reveal was disturbed");

    // THE SETTLE TIME STARTS AGAIN WITH EACH GROUP. The finger has been off
    // the glass all this while, so the group is armed the moment it arrives,
    // and it is the settle time alone that refuses a tap already on its way.
    const uint32_t arrived = solvedAt + REVEAL_MS;
    CHECK(tick(s, arrived, false) && s.stage == 1 && s.armed, "set-up: the next group, armed at once");
    CHECK(tapNumber(s, currentGroup(s).rel.answer, arrived + SETTLE_MS - 1) == Tap::Ignored,
          "a tap inside the new group's settle time counted");
    CHECK(tapNumber(s, currentGroup(s).rel.answer, arrived + SETTLE_MS) == Tap::Right,
          "a tap after the new group's settle time was refused");

    // THE CLOCK WRAPS, AND SO DO DEADLINES. Here the deadline is past the
    // wrap and the clock is not: comparing them as plain numbers would end
    // the reveal at once.
    s = freshSession(3, 0xFFFFFC00u);
    const uint32_t t = ready(s, 0xFFFFFC00u);
    CHECK(t > 0xFFFFFC00u && static_cast<uint32_t>(t + REVEAL_MS) < t, "set-up: the deadline wraps");
    CHECK(tapNumber(s, currentGroup(s).rel.answer, t) == Tap::Right, "set-up");
    CHECK(!tick(s, t + 100, false) && s.stage == 0 && s.phase == Phase::Reveal,
          "the reveal ended at once because its deadline lies past the wrap");
    CHECK(tick(s, t + REVEAL_MS, false) && s.stage == 1, "the reveal did not end across the wrap");

    // WHAT A FINISHED NEST SAYS. With slips: how many. Clean, and no level
    // to gain: that it was clean.
    now = 40000;
    s = freshSession(8, now);
    for (uint8_t stage = 0; stage <= FINAL_STAGE; ++stage) {
        now = ready(s, now);
        if (stage == 3) {
            for (int slip = 0; slip < 2; ++slip) {
                CHECK(tapNumber(s, wrongButton(s), now) == Tap::Wrong, "set-up");
                now += WRONG_MS;
                tick(s, now, false);
                now = ready(s, now);
            }
        }
        tapNumber(s, currentGroup(s).rel.answer, now);
        if (stage < FINAL_STAGE) {
            now += REVEAL_MS;
            tick(s, now, false);
        }
    }
    {
        const View v = viewOf(s);
        CHECK(v.done && v.status == static_cast<uint8_t>(Status::Complete) && v.mistakes == 2,
              "a nest with two slips reports status %u and %u slips", (unsigned)v.status, (unsigned)v.mistakes);
    }
    s.progress.level[0] = LEVEL_COUNT;
    now = ready(s, now);
    deal(s, 9, now);
    for (uint8_t stage = 0; stage <= FINAL_STAGE; ++stage) {
        now = ready(s, now);
        tapNumber(s, currentGroup(s).rel.answer, now);
        if (stage < FINAL_STAGE) {
            now += REVEAL_MS;
            tick(s, now, false);
        }
    }
    {
        const View v = viewOf(s);
        CHECK(v.done && v.status == static_cast<uint8_t>(Status::CompleteClean) && v.mistakes == 0 &&
                  v.level == LEVEL_COUNT && !s.promoted,
              "a clean nest at the top level reports status %u", (unsigned)v.status);
    }
}

// ---- which region shows which field -------------------------------------------

static void testViewDetails() {
    uint32_t now = 60000;
    Session s = freshSession(77, now);
    const View base = viewOf(s);

    // ONE FIELD AT A TIME. A field a painter reads must name that painter's
    // region when it changes, whether or not play can change it alone.
    View v = base;
    v.level = static_cast<uint8_t>(base.level + 1);
    CHECK(changedRegions(base, v) == REGION_LEVEL, "a level alone repaints %04x",
          (unsigned)changedRegions(base, v));
    v = base;
    v.slot[2] = 7;
    CHECK(changedRegions(base, v) == slotRegion(2), "a slot's value alone repaints %04x",
          (unsigned)changedRegions(base, v));
    v = base;
    v.number[4] = static_cast<uint16_t>(base.number[4] + 1);
    CHECK(changedRegions(base, v) == numberRegion(4), "a number alone repaints %04x",
          (unsigned)changedRegions(base, v));
    View a = base;
    a.status = static_cast<uint8_t>(Status::Equation);
    a.equation[0] = 1; a.equation[1] = 2; a.equation[2] = 3;
    v = a;
    v.equation[1] = 5;
    CHECK(changedRegions(a, v) == REGION_STATUS, "a different equation repaints %04x",
          (unsigned)changedRegions(a, v));
    a = base;
    a.done = true;
    a.status = static_cast<uint8_t>(Status::Complete);
    a.mistakes = 1;
    v = a;
    v.mistakes = 2;
    CHECK(changedRegions(a, v) == REGION_STATUS, "a different slip count repaints %04x",
          (unsigned)changedRegions(a, v));
    a.status = static_cast<uint8_t>(Status::LevelUp);
    v = a;
    v.level = static_cast<uint8_t>(a.level + 1);
    CHECK(changedRegions(a, v) == (REGION_STATUS | REGION_LEVEL), "a different level under Level up repaints %04x",
          (unsigned)changedRegions(a, v));

    // IN PLAY. Idle frames name nothing at any group, the final one included.
    for (uint8_t stage = 0; stage < FINAL_STAGE; ++stage) {
        now = ready(s, now);
        const View shown = viewOf(s);
        for (int frame = 0; frame < 30; ++frame) {
            now += 20;
            CHECK(!tick(s, now, false) && changedRegions(shown, viewOf(s)) == 0,
                  "an idle frame at group %u repaints %04x", (unsigned)(stage + 1),
                  (unsigned)changedRegions(shown, viewOf(s)));
        }
        tapNumber(s, currentGroup(s).rel.answer, now);
        now += REVEAL_MS;
        tick(s, now, false);
    }
    now = ready(s, now);
    View shown = viewOf(s);
    CHECK(s.stage == FINAL_STAGE && shown.status == static_cast<uint8_t>(Status::Prompt), "set-up: the final group");
    for (int frame = 0; frame < 30; ++frame) {
        now += 20;
        CHECK(!tick(s, now, false) && changedRegions(shown, viewOf(s)) == 0,
              "an idle frame at the final group repaints %04x", (unsigned)changedRegions(shown, viewOf(s)));
    }
    // The level control at the final group: same operation, a prompt before
    // and a prompt after -- and the prompt has lost its "Final group!".
    chooseNextLevel(s, 5, now);
    View next = viewOf(s);
    uint16_t mask = changedRegions(shown, next);
    CHECK((mask & REGION_STATUS) && (mask & REGION_LEVEL) && (mask & REGION_CAPTION) && !(mask & REGION_MODE),
          "stepping the level at the final group repaints %04x", (unsigned)mask);
    CHECK(slotsMask(mask) == static_cast<uint16_t>(0x1F << 10), "the nest was not emptied: %04x", (unsigned)mask);
    // A mode switch from one waiting prompt to another: the prompt names the
    // operation, so it is repainted although its status has not changed.
    shown = next;
    CHECK(chooseOp(s, Op::Mul, 6, now), "set-up");
    next = viewOf(s);
    mask = changedRegions(shown, next);
    CHECK(shown.status == next.status && (mask & REGION_STATUS) && (mask & REGION_MODE),
          "a mode switch between two prompts repaints %04x", (unsigned)mask);
    CHECK(((mask & REGION_LEVEL) != 0) == (shown.level != next.level), "the level control and the switch");
}

int main() {
    testArithmetic();
    testExamples();
    testLimits();
    testGeneration();
    testFallbacks();
    testProgression();
    testValidators();
    testPerOperation();
    testSession();
    testSessionDetails();
    testInput();
    testRepaint();
    testViewDetails();
    testManySittings();
    std::printf("%ld checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
