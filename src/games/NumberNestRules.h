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

/* Number Nest, as rules and nothing else.
 *
 * No drawing, no touch, no clock and no Arduino: everything here is a pure
 * function of a small flat struct, a seed and a time somebody else read. That
 * is what lets test/host/numbernest_rules_test.cpp build these two files with
 * a host compiler and play thousands of seeded nests -- and it is the only
 * thing that proves the purity, because the test will not compile if an
 * Arduino header ever leaks in.
 *
 * THE GAME. Five numbers are shown. Exactly one of them is the sum (Add) or
 * the product (Multiply) of two of the others; the player taps it. Five such
 * groups are solved in turn, each answer dropping into the nest, and the five
 * collected answers are then a sixth group, solved the same way.
 *
 * So a nest is built BACKWARDS. The final group is chosen first, and each
 * preliminary group is then built round one of its five values so that value
 * is that group's only answer. Nothing has to be hoped for afterwards: the
 * final group is solvable because it was the starting point.
 *
 * ONE DEFINITION OF "THE ANSWER". countRelations() enumerates every unordered
 * pair of positions and every third position, and a group is valid only when
 * that count is exactly one. The generator, the tap handler, the fallback
 * tables and the test all ask it; nothing decides correctness another way.
 *
 * AND TWO PREFERENCES, WHICH ARE NOT RULES. A valid group can still be an
 * unkind one, so the search steers away from two things where it can. It
 * does not show a number beside its own double: 3 + 3 = 6 is not an answer,
 * because a number used twice is one number, but it is correct arithmetic
 * and the player who taps the 6 is told no. And it does not ask one equation
 * twice in a nest: a final answer that can only be earned one way is earned
 * by the very equation the final group then asks. Neither is ever traded
 * for uniqueness or a limit, and both give way rather than send a nest to
 * the stored one -- 1 + 2 = 3 has no other way to be made, and inside the
 * ten-times table neither has 6 x 8.
 *
 * Names to avoid in this namespace: Arduino.h defines round, abs, sq, bit and
 * friends as macros, and the screen includes this header after it. */
namespace Nest {

enum class Op : uint8_t {
    Add = 0,
    Mul = 1,
};

constexpr uint8_t OP_COUNT = 2;
constexpr uint8_t GROUP_SIZE = 5;
/* Five preliminary groups, one per value of the final group, then the final
 * group itself. Stage FINAL_STAGE is the final one. */
constexpr uint8_t FINAL_STAGE = GROUP_SIZE;
constexpr uint8_t LEVEL_COUNT = 5;
/* Three digits is what a number button and a nest slot can show. */
constexpr uint16_t VALUE_CAP = 999;

constexpr uint8_t opIndex(Op op) {
    return op == Op::Mul ? 1 : 0;
}

/* ---- limits ------------------------------------------------------------
 *
 * What a level may put on the screen. operandMin..operandMax bind the two
 * operands of EVERY equation the player solves, the final group's included;
 * resultMax binds every number displayed, answers and bystanders alike, and
 * operandMin is also the smallest number ever shown.
 *
 * The two operations do not share a table and could not: a sum of two small
 * numbers is small, a product of two results is not. Multiplication's final
 * group is the tight case -- both of its operands must themselves be earned
 * as products of two different factors, so the smallest final answer there is
 * 6 x 8 = 48 whatever the level, and resultMax has to leave room for it.
 * Level 1 keeps every factor inside the ten-times table, and pays for it:
 * the only operands a final group can have there are 6, 8 and 10, so there
 * are three final equations, and 48, 60 and 80 are each one product of two
 * numbers up to 10 -- every level 1 nest meets its final equation earlier in
 * the same nest. Widening the level would fix that by making the easiest
 * level harder; it is one first-tap nest long, and the test states the
 * exception so that it stays a known one. */
struct Limits {
    uint16_t operandMin;
    uint16_t operandMax;
    uint16_t resultMax;
};

inline constexpr Limits LIMITS[OP_COUNT][LEVEL_COUNT] = {
    /* Add */
    {{1, 9, 20}, {1, 20, 40}, {2, 50, 100}, {5, 100, 200}, {10, 250, 500}},
    /* Mul */
    {{2, 10, 100}, {2, 12, 144}, {2, 15, 225}, {2, 20, 400}, {2, 25, 625}},
};

/* Levels are 1..LEVEL_COUNT everywhere outside this table. Anything else --
 * a stored value from another firmware, a garbled read -- is the easiest. */
constexpr uint8_t clampLevel(uint32_t level) {
    return (level >= 1 && level <= LEVEL_COUNT) ? static_cast<uint8_t>(level) : 1;
}

constexpr const Limits& limits(Op op, uint8_t level) {
    return LIMITS[opIndex(op)][clampLevel(level) - 1];
}

/* ---- arithmetic --------------------------------------------------------
 *
 * WIDENED BEFORE THE ARITHMETIC, NOT AFTER. Two uint16_t operands promote to
 * int, and 65535 * 65535 does not fit one: the multiplication would overflow
 * a signed type, which is undefined rather than merely wrong. As uint32_t the
 * largest product is 4,294,836,225, which fits, and the largest sum is
 * nowhere near. No value on the screen exceeds VALUE_CAP anyway; this is so
 * that the function is safe for every input, not only for the expected ones. */
constexpr uint32_t combine(Op op, uint16_t a, uint16_t b) {
    return op == Op::Mul ? static_cast<uint32_t>(a) * static_cast<uint32_t>(b)
                         : static_cast<uint32_t>(a) + static_cast<uint32_t>(b);
}
/* Held to that by the compiler that builds the firmware: evaluated as a
 * constant, a product taken as int overflows, and an overflow in a constant
 * expression does not compile. A run-time test cannot catch it -- on the
 * hardware the wrapped bits happen to be the right ones. */
static_assert(combine(Op::Mul, 0xFFFF, 0xFFFF) == 4294836225UL &&
                  combine(Op::Add, 0xFFFF, 0xFFFF) == 131070UL,
              "combine() must widen its operands before the arithmetic");

/* ---- groups ------------------------------------------------------------ */

/* Positions, not values: which two buttons are the operands and which one is
 * their result. first < second always. */
struct Relation {
    uint8_t first;
    uint8_t second;
    uint8_t answer;
};

struct Group {
    uint16_t value[GROUP_SIZE];
    Relation rel;
};

/* Every (first < second, answer) among the first `count` values for which
 * value[first] op value[second] == value[answer], with the answer at a third
 * position. Writes the last one found to *found when it is not null. */
constexpr uint8_t countRelations(Op op, const uint16_t* value, uint8_t count,
                                 Relation* found) {
    uint8_t total = 0;
    for (uint8_t i = 0; i < count; ++i) {
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < count; ++j) {
            const uint32_t result = combine(op, value[i], value[j]);
            for (uint8_t k = 0; k < count; ++k) {
                if (k == i || k == j) {
                    continue;
                }
                if (value[k] == result) {
                    if (found != nullptr) {
                        *found = Relation{i, j, k};
                    }
                    ++total;
                }
            }
        }
    }
    return total;
}

/* Can `target` be earned at this level: is it a op b for two DIFFERENT
 * operands a < b, both inside the operand limits? This is what makes a value
 * fit to stand in a final group, since each of those is a preliminary answer. */
constexpr bool earnable(Op op, const Limits& lim, uint32_t target) {
    for (uint32_t a = lim.operandMin; a <= lim.operandMax; ++a) {
        if (op == Op::Mul) {
            if (a * a >= target) {
                break;
            }
            if (target % a == 0) {
                const uint32_t b = target / a;
                if (b > a && b <= lim.operandMax) {
                    return true;
                }
            }
        } else {
            if (a * 2 >= target) {
                break;
            }
            const uint32_t b = target - a;
            if (b <= lim.operandMax) {
                return true;
            }
        }
    }
    return false;
}

/* A group is valid when its five values are distinct and inside the level's
 * limits, exactly one relation holds among them, that relation is the one
 * recorded, and its two operands are inside the operand limits. */
constexpr bool groupValid(const Group& g, Op op, const Limits& lim) {
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
        if (g.value[i] < lim.operandMin || g.value[i] > lim.resultMax ||
            g.value[i] > VALUE_CAP) {
            return false;
        }
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < GROUP_SIZE; ++j) {
            if (g.value[i] == g.value[j]) {
                return false;
            }
        }
    }
    Relation only{0, 0, 0};
    if (countRelations(op, g.value, GROUP_SIZE, &only) != 1) {
        return false;
    }
    if (only.first != g.rel.first || only.second != g.rel.second ||
        only.answer != g.rel.answer) {
        return false;
    }
    return g.value[only.first] <= lim.operandMax && g.value[only.second] <= lim.operandMax;
}

struct Puzzle {
    Group prelim[GROUP_SIZE];
    Group final;
    Op op;
    uint8_t level;
    /* True when the bounded search gave up and this is the stored puzzle for
     * the level instead. Still a complete, valid nest. */
    bool fallback;
};

/* The whole nest: the final group is valid, and preliminary group i is valid
 * with final.value[i] as its answer -- which is also what proves every final
 * value can be earned. */
constexpr bool puzzleValid(const Puzzle& p) {
    if (p.level < 1 || p.level > LEVEL_COUNT) {
        return false;
    }
    const Limits& lim = limits(p.op, p.level);
    if (!groupValid(p.final, p.op, lim)) {
        return false;
    }
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
        if (!groupValid(p.prelim[i], p.op, lim)) {
            return false;
        }
        if (p.prelim[i].value[p.prelim[i].rel.answer] != p.final.value[i]) {
            return false;
        }
    }
    return true;
}

/* ---- generation --------------------------------------------------------
 *
 * generate() fills `out` with a valid nest for (op, level) and returns true
 * when the seeded search produced it, false when it fell back to the stored
 * one. Either way puzzleValid(out) holds on return.
 *
 * The search is bounded by WORK, not by luck: every candidate number it
 * examines spends one unit of a fixed budget, and when the budget is gone it
 * stops and uses the fallback. It never loosens a limit and never accepts a
 * group with two answers to get out of trouble.
 *
 * Deterministic in (op, level, seed): the same three give the same nest. */
constexpr uint32_t SEARCH_BUDGET = 6000;

uint32_t mix(uint32_t x);
bool generate(Puzzle& out, Op op, uint8_t level, uint32_t seed);
/* The same search with a caller's budget, and how much of it was spent. For
 * the test, which needs to starve it and to measure it. */
bool generateWithin(Puzzle& out, Op op, uint8_t level, uint32_t seed,
                    uint32_t budget, uint32_t* spent);
const Puzzle& fallbackPuzzle(Op op, uint8_t level);

/* ---- progression -------------------------------------------------------
 *
 * There is no shared difficulty framework in this firmware, so this is the
 * smallest mechanism that agrees with the games nearest to it:
 *
 *   - FIVE LEVELS, as Math and Multiplication have.
 *   - KEPT, per operation, as Flags and States keep their tier -- a nest is
 *     six answers, and making a returning player climb from level 1 on every
 *     visit would cost minutes where Math costs seconds.
 *   - PROMOTED BY SIX IN A ROW, the rule those games use verbatim. An answer
 *     counts toward the run only when it was right at the first tap, which is
 *     what "a correct answer" means in a game where a wrong one is retried.
 *     Six is also exactly one nest solved without a slip.
 *   - APPLIED WHEN THE NEST IS FINISHED. Their questions are one answer long,
 *     so a promotion can take effect at the next one; a nest is dealt at one
 *     level and is six answers long. A run can reach six part way through a
 *     nest -- the tail of the last one and the head of this -- so the
 *     promotion is EARNED there and kept, a later slip notwithstanding, and
 *     the level moves when the nest is done. The level on the screen is
 *     therefore always the level of the nest being played. (The screen
 *     writes the level to storage the moment it is earned, so leaving
 *     before the nest ends does not lose it.)
 *   - THE RUN STARTS OVER WITH THE LEVEL. Answers that finished a nest at
 *     the old level are not a head start on the next one, and a wrong tap
 *     ends the run then and there -- not when the group is finally solved,
 *     or abandoning the group would hide the slip.
 *   - NEVER DEMOTED automatically, as in every game here. The level control
 *     on the screen is the way down, as their tier button is.
 *
 * Addition and multiplication each have their own level and their own run. */
constexpr uint8_t PROMOTE_RUN = 6;

struct Progress {
    uint8_t level[OP_COUNT];
    uint8_t run[OP_COUNT];
    bool earned[OP_COUNT];   // a promotion waiting for its nest to be finished
};

void resetProgress(Progress& p);
/* One group solved. Returns true when this answer completed the run that
 * earns a promotion. The level itself does not move here. */
bool recordGroup(Progress& p, Op op, bool firstTry);
/* A nest finished. Applies an earned promotion; true when the level rose. */
bool finishNest(Progress& p, Op op);
/* The on-screen control: next level, wrapping to 1. The run starts over and
 * anything earned at the old level is given up with it. */
uint8_t cycleLevel(Progress& p, Op op);

/* ---- one sitting -------------------------------------------------------
 *
 * The flow of a nest, kept here rather than in the screen so that "a held
 * finger must not answer the next group" is something a test can state.
 *
 * Times are milliseconds from any clock the caller likes; only differences
 * are taken, so a wrapping counter is fine. */
enum class Phase : uint8_t {
    Play,     // a group is up and waiting for a tap
    Reveal,   // the equation is showing; the next group follows
    Done,     // the final group is solved
};

enum class Tap : uint8_t {
    Ignored,
    Wrong,
    Right,
    Finished,   // right, and it was the final group
};

/* How long a wrong button stays marked. */
constexpr uint32_t WRONG_MS = 500;
/* How long the equation is held before the next group replaces it. */
constexpr uint32_t REVEAL_MS = 1200;
/* A fresh group ignores taps for this long. A tap already on its way when the
 * numbers changed was aimed at the old ones. */
constexpr uint32_t SETTLE_MS = 250;
/* How long a finger must be OFF the glass before it counts as lifted. One
 * sample is not evidence: a resistive panel drops below its pressure
 * threshold in the middle of a held press as a matter of course, and the
 * frame after the dropout reads as a fresh press at the same place. The lock
 * screen forgives the same 150 ms for the same reason. */
constexpr uint32_t RELEASE_MS = 150;

struct Session {
    Progress progress;
    Puzzle puzzle;
    Op op;
    Phase phase;
    uint8_t stage;       // 0..4 preliminary groups, FINAL_STAGE the final one
    uint8_t collected;   // nest slots filled so far
    uint8_t slips;       // wrong taps on the group that is up
    uint8_t mistakes;    // wrong taps in this nest
    int8_t wrongIndex;   // the button marked wrong, or -1
    /* False from the moment the picture changes, or a tap is accepted,
     * until a finger has been OFF the glass for RELEASE_MS. A press held
     * across the change -- the tap that solved the last group, the one that
     * switched mode, a wrong tap still resting on its number -- answers
     * nothing more, however long it is held and however the contact
     * flickers. */
    bool armed;
    bool promoted;       // this nest raised the level
    bool fingerUp;       // no contact at the last look
    uint32_t upSince;    // when that began
    uint32_t wrongSince;
    uint32_t phaseSince;
};

void resetSession(Session& s);
/* A fresh nest in the current operation at its current level. */
void deal(Session& s, uint32_t seed, uint32_t now);
/* False, and nothing changes, when `op` is already the operation in play. */
bool chooseOp(Session& s, Op op, uint32_t seed, uint32_t now);
void chooseNextLevel(Session& s, uint32_t seed, uint32_t now);
const Group& currentGroup(const Session& s);
Tap tapNumber(Session& s, uint8_t index, uint32_t now);
/* Call once a frame. Returns true when something a player can see changed. */
bool tick(Session& s, uint32_t now, bool fingerDown);
/* The arming half of tick() on its own, for a moment when the nest is not
 * running but the finger still has to be watched -- the rules page. */
void watchFinger(Session& s, uint32_t now, bool fingerDown);
/* The picture changed under the finger for a reason of the screen's own (the
 * rules page opening or closing). Nothing is accepted until it has lifted
 * and the settle time has passed, exactly as after a fresh group; and a
 * wrong mark still showing is dropped, since its clock is about to stop. */
void holdInput(Session& s, uint32_t now);
/* May a tap be taken now? One gate for every control on the screen, the
 * numbers included: lifted first, and not within SETTLE_MS of a change. */
bool controlsArmed(const Session& s, uint32_t now);

/* ---- what the screen should show ---------------------------------------
 *
 * The screen keeps the View it last painted and repaints only the regions
 * changedRegions() names. That makes "an idle frame draws nothing" and "a
 * wrong tap repaints one button and the status line" properties of this
 * function, which the test checks, rather than of care taken in a renderer. */
enum class NumberLook : uint8_t {
    Idle,
    Wrong,
    Operand,   // one of the two numbers that made the answer
    Answer,
};

enum class SlotLook : uint8_t {
    Empty,
    Filled,
    Fresh,     // the answer that has just dropped in
};

enum class Status : uint8_t {
    Prompt,
    TryAgain,
    Equation,
    Complete,        // the nest is finished, with slips along the way
    CompleteClean,   // finished without one
    LevelUp,         // finished, and the level rose during it
};

struct View {
    uint8_t op;
    uint8_t level;
    uint8_t stage;
    uint8_t status;
    uint8_t mistakes;
    bool done;
    uint16_t number[GROUP_SIZE];
    uint8_t numberLook[GROUP_SIZE];
    uint16_t slot[GROUP_SIZE];
    uint8_t slotLook[GROUP_SIZE];
    /* The operands, smaller first, and their result: "3 + 7 = 10". */
    uint16_t equation[3];
};

constexpr uint16_t REGION_MODE = 1u << 0;
constexpr uint16_t REGION_LEVEL = 1u << 1;
constexpr uint16_t REGION_STATUS = 1u << 2;
constexpr uint16_t REGION_CAPTION = 1u << 3;
constexpr uint16_t REGION_FOOTER = 1u << 4;
constexpr uint16_t REGION_NUMBER0 = 1u << 5;    // then one bit per button
constexpr uint16_t REGION_SLOT0 = 1u << 10;     // then one bit per slot
constexpr uint16_t REGION_ALL = 0x7FFF;

constexpr uint16_t numberRegion(uint8_t index) {
    return static_cast<uint16_t>(REGION_NUMBER0 << index);
}

constexpr uint16_t slotRegion(uint8_t index) {
    return static_cast<uint16_t>(REGION_SLOT0 << index);
}

View viewOf(const Session& s);
uint16_t changedRegions(const View& was, const View& now);

}  // namespace Nest
