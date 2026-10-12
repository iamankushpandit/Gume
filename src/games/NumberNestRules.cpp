// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "NumberNestRules.h"

namespace Nest {

namespace {

/* ---- the stored nests --------------------------------------------------
 *
 * One complete nest per operation and level, for the day the bounded search
 * comes back empty. They are checked by the COMPILER: the static_asserts
 * below run the same puzzleValid() the game runs, so a stored nest that broke
 * a rule -- or a limit somebody tightened under it -- is a build error rather
 * than a puzzle with two answers in a player's hands.
 *
 * Each was produced by the search itself and then picked so that the final
 * answer is not also the largest number in the final group, no group shows a
 * number beside its own double, and the final equation is not also one of
 * the five before it -- except multiplication at level 1, where it always
 * is (see makePrelim). */
constexpr Puzzle FALLBACK[OP_COUNT][LEVEL_COUNT] = {
    {
        /* Add, level 1: 3+5=8 4+9=13 1+5=6 8+9=17 2+5=7  then 6+7=13 */
        {{
             {{8, 9, 15, 5, 3}, {3, 4, 0}},
             {{6, 9, 4, 1, 13}, {1, 2, 4}},
             {{1, 13, 3, 5, 6}, {0, 3, 4}},
             {{5, 17, 8, 11, 9}, {2, 4, 1}},
             {{7, 8, 5, 11, 2}, {2, 4, 0}},
         },
         {{8, 13, 6, 17, 7}, {2, 4, 1}}, Op::Add, 1, true},
        /* Add, level 2: 7+19=26 2+16=18 5+12=17 6+17=23 2+3=5  then 5+18=23 */
        {{
             {{7, 19, 26, 2, 30}, {0, 1, 2}},
             {{16, 10, 18, 2, 9}, {0, 3, 2}},
             {{5, 11, 17, 18, 12}, {0, 4, 2}},
             {{23, 17, 6, 7, 18}, {1, 2, 0}},
             {{2, 5, 3, 9, 13}, {0, 2, 1}},
         },
         {{26, 18, 17, 23, 5}, {1, 4, 3}}, Op::Add, 2, true},
        /* Add, level 3: 8+12=20 4+5=9 26+35=61 21+28=49 24+34=58  then 9+49=58 */
        {{
             {{20, 8, 12, 13, 3}, {1, 2, 0}},
             {{5, 4, 7, 17, 9}, {0, 1, 4}},
             {{61, 73, 26, 45, 35}, {2, 4, 0}},
             {{49, 62, 28, 53, 21}, {2, 4, 0}},
             {{24, 34, 58, 81, 13}, {0, 1, 2}},
         },
         {{20, 9, 61, 49, 58}, {1, 3, 4}}, Op::Add, 3, true},
        /* Add, level 4: 38+89=127 19+69=88 16+23=39 8+10=18 77+100=177  then 39+88=127 */
        {{
             {{127, 115, 103, 89, 38}, {3, 4, 0}},
             {{19, 88, 24, 69, 96}, {0, 3, 1}},
             {{5, 16, 39, 23, 57}, {1, 3, 2}},
             {{18, 9, 11, 8, 10}, {3, 4, 0}},
             {{100, 107, 77, 185, 177}, {0, 2, 4}},
         },
         {{127, 88, 39, 18, 177}, {1, 2, 0}}, Op::Add, 4, true},
        /* Add, level 5: 65+228=293 41+114=155 157+175=332 30+108=138 137+214=351  then 138+155=293 */
        {{
             {{288, 65, 228, 370, 293}, {1, 2, 4}},
             {{114, 155, 41, 199, 87}, {0, 2, 1}},
             {{332, 157, 175, 66, 180}, {1, 2, 0}},
             {{30, 138, 55, 19, 108}, {0, 4, 1}},
             {{267, 351, 376, 214, 137}, {3, 4, 1}},
         },
         {{293, 155, 332, 138, 351}, {1, 3, 0}}, Op::Add, 5, true},
    },
    {
        /* Mul, level 1: 2x5=10 2x3=6 6x10=60 8x10=80 2x9=18  then 6x10=60 */
        {{
             {{14, 2, 6, 10, 5}, {1, 4, 3}},
             {{2, 6, 3, 13, 10}, {0, 2, 1}},
             {{10, 6, 60, 69, 68}, {0, 1, 2}},
             {{10, 76, 80, 9, 8}, {0, 4, 2}},
             {{12, 23, 9, 2, 18}, {2, 3, 4}},
         },
         {{10, 6, 60, 80, 18}, {0, 1, 2}}, Op::Mul, 1, true},
        /* Mul, level 2: 5x12=60 2x3=6 3x4=12 2x5=10 6x11=66  then 6x10=60 */
        {{
             {{12, 60, 5, 56, 45}, {0, 2, 1}},
             {{3, 6, 2, 5, 7}, {0, 2, 1}},
             {{12, 4, 5, 17, 3}, {1, 4, 0}},
             {{5, 2, 16, 10, 6}, {0, 1, 3}},
             {{6, 4, 66, 98, 11}, {0, 4, 2}},
         },
         {{60, 6, 12, 10, 66}, {1, 3, 0}}, Op::Mul, 2, true},
        /* Mul, level 3: 9x13=117 5x15=75 9x10=90 3x5=15 2x3=6  then 6x15=90 */
        {{
             {{9, 161, 117, 113, 13}, {0, 4, 2}},
             {{15, 38, 5, 75, 105}, {0, 2, 3}},
             {{90, 10, 99, 92, 9}, {1, 4, 0}},
             {{11, 10, 3, 5, 15}, {2, 3, 4}},
             {{7, 5, 6, 2, 3}, {3, 4, 2}},
         },
         {{117, 75, 90, 15, 6}, {3, 4, 2}}, Op::Mul, 3, true},
        /* Mul, level 4: 5x11=55 2x3=6 3x20=60 2x5=10 8x11=88  then 6x10=60 */
        {{
             {{2, 5, 55, 11, 33}, {1, 3, 2}},
             {{2, 13, 5, 6, 3}, {0, 4, 3}},
             {{61, 47, 60, 3, 20}, {3, 4, 2}},
             {{6, 2, 8, 5, 10}, {1, 3, 4}},
             {{11, 3, 8, 88, 26}, {0, 2, 3}},
         },
         {{55, 6, 60, 10, 88}, {1, 3, 2}}, Op::Mul, 4, true},
        /* Mul, level 5: 3x7=21 14x18=252 2x6=12 10x22=220 13x23=299  then 12x21=252 */
        {{
             {{7, 21, 3, 17, 4}, {0, 2, 1}},
             {{74, 252, 284, 14, 18}, {3, 4, 1}},
             {{2, 5, 13, 12, 6}, {0, 4, 3}},
             {{220, 22, 134, 245, 10}, {1, 4, 0}},
             {{13, 196, 328, 299, 23}, {0, 4, 3}},
         },
         {{21, 252, 12, 220, 299}, {0, 2, 1}}, Op::Mul, 5, true},
    },
};

constexpr bool fallbacksValid() {
    for (uint8_t op = 0; op < OP_COUNT; ++op) {
        for (uint8_t level = 0; level < LEVEL_COUNT; ++level) {
            const Puzzle& p = FALLBACK[op][level];
            if (!puzzleValid(p) || !p.fallback || opIndex(p.op) != op ||
                p.level != level + 1) {
                return false;
            }
        }
    }
    return true;
}

/* A level has to be able to produce a nest at all, and harder levels must
 * not shrink what an easier one allowed. */
constexpr bool limitsSane() {
    for (uint8_t op = 0; op < OP_COUNT; ++op) {
        const uint16_t floorOperand = op == 0 ? 1 : 2;
        for (uint8_t level = 0; level < LEVEL_COUNT; ++level) {
            const Limits& lim = LIMITS[op][level];
            if (lim.operandMin < floorOperand || lim.operandMax <= lim.operandMin ||
                lim.resultMax < lim.operandMax || lim.resultMax > VALUE_CAP) {
                return false;
            }
            if (level > 0 && (lim.operandMax < LIMITS[op][level - 1].operandMax ||
                              lim.resultMax < LIMITS[op][level - 1].resultMax)) {
                return false;
            }
        }
    }
    return true;
}

static_assert(limitsSane(),
              "a level's limits are out of order, or a value would not fit a button");
static_assert(fallbacksValid(),
              "a stored nest breaks the rules or its level's limits -- regenerate it");

/* How many times each part of the search may start over before giving the
 * whole nest up. The budget is the real bound; these only stop one unlucky
 * choice from spending all of it. */
constexpr uint8_t PUZZLE_TRIES = 4;
constexpr uint8_t FINAL_TRIES = 6;
constexpr uint8_t PRELIM_TRIES = 4;
/* How many times a final equation the search would rather not use is drawn
 * again before it is used anyway. */
constexpr uint8_t FINAL_REPICKS = 3;

/* The seed as a stream, the way Ludo and Backgammon draw their dice: draw
 * number n is a function of (seed, n), nothing is carried between calls but
 * the counter, and the same seed therefore always builds the same nest. */
struct Rng {
    uint32_t seed;
    uint32_t count;

    uint32_t next() {
        const uint32_t h = mix(seed ^ mix(count * 0x9E3779B9U + 0x4E357A11U));
        ++count;
        return h;
    }

    /* The modulo bias of 2^32 against a bound of a few hundred is a few
     * parts in ten million. */
    uint32_t below(uint32_t bound) {
        return bound == 0 ? 0 : next() % bound;
    }
};

struct Search {
    Rng rng;
    Op op;
    const Limits* lim;
    uint32_t budget;

    /* One candidate examined. False once there is nothing left to spend, and
     * every loop below stops on it. */
    bool spend() {
        if (budget == 0) {
            return false;
        }
        --budget;
        return true;
    }
};

/* Is `y` the number `x` added to itself -- or multiplied by itself?
 *
 * A DOUBLE IS NOT AN ANSWER, AND IT IS KEPT OFF THE SCREEN ANYWAY. The rule
 * is two of the OTHERS, and one number used twice is one number, so a group
 * showing 3 and 6 has no second answer in it. But "3 + 3 = 6" is correct
 * arithmetic, and a young player who taps the 6 for it is told no and loses
 * the run that earns a level. Nothing about the rules page makes that their
 * mistake. So the generator does not show a number beside its own double:
 * never for the two bystanders, which it is free to choose, and for the
 * operands whenever the target can be made another way. 1 + 2 = 3 cannot,
 * and stands. This is a rule of the search, not part of what makes a group
 * valid -- countRelations() is still the only definition of that. */
bool doubles(Op op, uint32_t x, uint32_t y) {
    return combine(op, static_cast<uint16_t>(x), static_cast<uint16_t>(x)) == y;
}

/* Would `candidate` sit beside its own double, or be one? have[2] is the
 * answer in every group under construction, and a number whose double IS the
 * answer is harmless: tapping the answer is right whatever the reason. */
bool makesDouble(Op op, const uint16_t* have, uint8_t held, uint16_t candidate) {
    for (uint8_t i = 0; i < held; ++i) {
        if (doubles(op, have[i], candidate)) {
            return true;
        }
        if (i != 2 && doubles(op, candidate, have[i])) {
            return true;
        }
    }
    return false;
}

/* The first value at or after a random start in [lo, hi], wrapping round,
 * that is not in the group already, can be earned if that is asked, and
 * leaves the group with exactly ONE relation -- the one it started with --
 * and is not the double of anything shown, nor half of it. Stored at
 * have[held].
 *
 * A scan rather than repeated guessing, because it cannot miss: if a value
 * that works exists in the window, this finds it in at most one pass. */
bool scanFor(Search& s, uint16_t* have, uint8_t held, uint16_t lo, uint16_t hi,
             bool mustEarn) {
    if (hi < lo) {
        return false;
    }
    const uint32_t span = static_cast<uint32_t>(hi) - lo + 1;
    const uint32_t start = s.rng.below(span);
    for (uint32_t step = 0; step < span; ++step) {
        if (!s.spend()) {
            return false;
        }
        const uint16_t candidate = static_cast<uint16_t>(lo + (start + step) % span);
        bool taken = false;
        for (uint8_t i = 0; i < held; ++i) {
            if (have[i] == candidate) {
                taken = true;
            }
        }
        if (taken || (mustEarn && !earnable(s.op, *s.lim, candidate)) ||
            makesDouble(s.op, have, held, candidate)) {
            continue;
        }
        have[held] = candidate;
        if (countRelations(s.op, have, static_cast<uint8_t>(held + 1), nullptr) == 1) {
            return true;
        }
    }
    return false;
}

/* have[0..2] hold two operands and their result. Add two bystanders.
 *
 * Tried first in a window near the result, so the bystanders are numbers a
 * player might plausibly take for the answer, and then across everything the
 * level allows. Small sums need the second pass: round 2 + 3 = 5 there is no
 * pair of single digits that does not make a second sum with something.
 *
 * A bystander never makes a double (see doubles()), in either pass. There was
 * a third pass that gave that up as a last resort; in three and a half
 * million nests it never ran once, so it was a promise no test could keep,
 * and it is gone. A triple that cannot be filled is given up, as it always
 * could be, and the search tries another. */
bool fillGroup(Search& s, uint16_t* have, uint16_t anchor, bool mustEarn) {
    const Limits& lim = *s.lim;
    const uint32_t reach = static_cast<uint32_t>(anchor) + (anchor / 2 > 8 ? anchor / 2 : 8);
    const uint16_t nearTop = static_cast<uint16_t>(reach < lim.resultMax ? reach : lim.resultMax);
    const uint16_t tops[2] = {nearTop, lim.resultMax};
    for (uint8_t pass = 0; pass < 2; ++pass) {
        if (pass == 1 && tops[1] == tops[0]) {
            break;   // the same window twice proves nothing
        }
        if (scanFor(s, have, 3, lim.operandMin, tops[pass], mustEarn) &&
            scanFor(s, have, 4, lim.operandMin, tops[pass], mustEarn)) {
            return true;
        }
    }
    return false;
}

/* Shuffle the five into their display order and record where the one
 * relation ended up. */
void settle(Search& s, const uint16_t* have, Group& out) {
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
        out.value[i] = have[i];
    }
    for (uint8_t i = static_cast<uint8_t>(GROUP_SIZE - 1); i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(s.rng.below(static_cast<uint32_t>(i) + 1));
        const uint16_t held = out.value[i];
        out.value[i] = out.value[j];
        out.value[j] = held;
    }
    out.rel = Relation{0, 0, 0};
    countRelations(s.op, out.value, GROUP_SIZE, &out.rel);
}

/* An operand for the FINAL group, uniformly among those that qualify: inside
 * the operand limits and itself earnable, since it has to arrive as the
 * answer to a preliminary group. `exclude` is 0 for "none". */
bool pickFinalOperand(Search& s, uint16_t exclude, uint16_t& out) {
    const Limits& lim = *s.lim;
    uint32_t choices = 0;
    for (uint32_t v = lim.operandMin; v <= lim.operandMax; ++v) {
        if (!s.spend()) {
            return false;
        }
        if (v != exclude && earnable(s.op, lim, v)) {
            ++choices;
        }
    }
    if (choices == 0) {
        return false;
    }
    uint32_t want = s.rng.below(choices);
    for (uint32_t v = lim.operandMin; v <= lim.operandMax; ++v) {
        if (v == exclude || !earnable(s.op, lim, v)) {
            continue;
        }
        if (want == 0) {
            out = static_cast<uint16_t>(v);
            return true;
        }
        --want;
    }
    return false;
}

bool partnerOf(Op op, const Limits& lim, uint16_t target, uint32_t a, uint16_t& partner);

/* A final equation that is allowed and that the search would rather not use.
 *
 * Either one operand is the other doubled, or the answer can be earned in
 * only one way inside the limits -- in which case the group that earns it is
 * this same equation, and the nest asks one question twice. (a, b) is itself
 * one of the ways, so fewer than two means no other. */
bool poorFinal(const Search& s, uint16_t a, uint16_t b, uint16_t result) {
    if (doubles(s.op, a, b)) {
        return true;
    }
    const Limits& lim = *s.lim;
    uint32_t ways = 0;
    for (uint32_t x = lim.operandMin; x <= lim.operandMax && ways < 2; ++x) {
        uint16_t y = 0;
        if (partnerOf(s.op, lim, result, x, y)) {
            ++ways;
        }
    }
    return ways < 2;
}

bool makeFinal(Search& s, Group& out, uint16_t& low, uint16_t& high) {
    const Limits& lim = *s.lim;
    for (uint8_t attempt = 0; attempt < FINAL_TRIES; ++attempt) {
        /* Draw an equation; while it is a poor one, draw again, a bounded
         * number of times, and keep the last that was allowed. A level with
         * nothing better -- multiplication inside the ten-times table has
         * three final equations and each can be earned one way -- therefore
         * still gets its nest, and gets it at the first attempt. */
        uint16_t a = 0;
        uint16_t b = 0;
        uint32_t result = 0;
        bool drawn = false;
        for (uint8_t pick = 0; pick <= FINAL_REPICKS; ++pick) {
            uint16_t x = 0;
            uint16_t y = 0;
            if (!pickFinalOperand(s, 0, x) || !pickFinalOperand(s, x, y)) {
                return false;   // out of budget, or the level has no two such operands
            }
            if (x > y) {
                const uint16_t held = x;
                x = y;
                y = held;
            }
            const uint32_t made = combine(s.op, x, y);
            if (made > lim.resultMax || !earnable(s.op, lim, made)) {
                continue;
            }
            a = x;
            b = y;
            result = made;
            drawn = true;
            if (!poorFinal(s, x, y, static_cast<uint16_t>(made))) {
                break;
            }
        }
        if (!drawn) {
            continue;
        }
        uint16_t have[GROUP_SIZE] = {a, b, static_cast<uint16_t>(result), 0, 0};
        /* Its bystanders must be earnable too: every number in the final
         * group is the answer to one of the groups before it. */
        if (!fillGroup(s, have, static_cast<uint16_t>(result), true)) {
            continue;
        }
        settle(s, have, out);
        low = a;
        high = b;
        return true;
    }
    return false;
}

/* The operand that pairs with `a` to earn `target`, if there is one above it
 * inside the limits. */
bool partnerOf(Op op, const Limits& lim, uint16_t target, uint32_t a, uint16_t& partner) {
    uint32_t b = 0;
    if (op == Op::Mul) {
        if (a == 0 || target % a != 0) {
            return false;
        }
        b = target / a;
    } else {
        if (a >= target) {
            return false;
        }
        b = target - a;
    }
    if (b <= a || b > lim.operandMax) {
        return false;
    }
    partner = static_cast<uint16_t>(b);
    return true;
}

/* How particular the choice of an operand pair is being, strictest first. */
constexpr uint8_t PAIR_BEST = 0;      // not the pair to avoid, and neither is the other doubled
constexpr uint8_t PAIR_FRESH = 1;     // not the pair to avoid
constexpr uint8_t PAIR_ANY = 2;
constexpr uint8_t PAIR_TIERS = 3;

bool pairFits(Op op, uint8_t tier, uint32_t a, uint16_t b, uint16_t avoidLow,
              uint16_t avoidHigh) {
    const bool avoided = a == avoidLow && b == avoidHigh;
    if (tier == PAIR_BEST) {
        return !avoided && !doubles(op, a, b);
    }
    return tier == PAIR_ANY || !avoided;
}

/* A preliminary group whose one answer is `target`.
 *
 * (avoidLow, avoidHigh) is the pair the FINAL group uses when `target` is the
 * final answer, 0 otherwise. Earning 48 as 6 x 8 and then being asked for
 * 6 x 8 again in the final group is the same question twice, so another way
 * of making the target is preferred whenever one exists; and so is a pair in
 * which neither operand is the other doubled (see doubles()). The strictest
 * tier that has any pair in it is the one chosen from. When the only way to
 * make the target is the pair to avoid, the repeat stands: inside the
 * ten-times table that is every final answer there is, because 48, 60 and 80
 * are each one product of two numbers up to 10. */
bool makePrelim(Search& s, uint16_t target, uint16_t avoidLow, uint16_t avoidHigh, Group& out) {
    const Limits& lim = *s.lim;
    uint8_t tier = PAIR_BEST;
    uint32_t usable = 0;
    for (; tier < PAIR_TIERS && usable == 0; ++tier) {
        for (uint32_t a = lim.operandMin; a <= lim.operandMax; ++a) {
            uint16_t b = 0;
            if (partnerOf(s.op, lim, target, a, b) &&
                pairFits(s.op, tier, a, b, avoidLow, avoidHigh)) {
                ++usable;
            }
        }
        if (usable > 0) {
            break;
        }
    }
    if (usable == 0) {
        return false;
    }

    for (uint8_t attempt = 0; attempt < PRELIM_TRIES; ++attempt) {
        if (!s.spend()) {
            return false;
        }
        uint32_t want = s.rng.below(usable);
        uint16_t low = 0;
        uint16_t high = 0;
        for (uint32_t a = lim.operandMin; a <= lim.operandMax; ++a) {
            uint16_t b = 0;
            if (!partnerOf(s.op, lim, target, a, b) ||
                !pairFits(s.op, tier, a, b, avoidLow, avoidHigh)) {
                continue;
            }
            if (want == 0) {
                low = static_cast<uint16_t>(a);
                high = b;
                break;
            }
            --want;
        }
        if (high == 0) {
            return false;
        }
        uint16_t have[GROUP_SIZE] = {low, high, target, 0, 0};
        if (fillGroup(s, have, target, false)) {
            settle(s, have, out);
            return true;
        }
    }
    return false;
}

}  // namespace

uint32_t mix(uint32_t x) {
    /* lowbias32 (Wellons), the mixer the other rules units carry. */
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return x;
}

const Puzzle& fallbackPuzzle(Op op, uint8_t level) {
    return FALLBACK[opIndex(op)][clampLevel(level) - 1];
}

bool generate(Puzzle& out, Op op, uint8_t level, uint32_t seed, uint32_t budget,
              uint32_t* spent) {
    const uint8_t useLevel = clampLevel(level);
    Search s{Rng{seed, 0}, op, &limits(op, useLevel), budget};
    bool made = false;

    for (uint8_t attempt = 0; attempt < PUZZLE_TRIES && !made && s.budget > 0; ++attempt) {
        Puzzle p{};
        p.op = op;
        p.level = useLevel;
        p.fallback = false;

        uint16_t low = 0;
        uint16_t high = 0;
        if (!makeFinal(s, p.final, low, high)) {
            continue;
        }
        const uint16_t finalAnswer = p.final.value[p.final.rel.answer];
        bool all = true;
        for (uint8_t i = 0; i < GROUP_SIZE && all; ++i) {
            const uint16_t target = p.final.value[i];
            const bool isAnswer = target == finalAnswer;
            all = makePrelim(s, target, isAnswer ? low : 0, isAnswer ? high : 0, p.prelim[i]);
        }
        /* The whole nest is checked once more, as a unit, by the same
         * function that checks the stored ones. Nothing built above is
         * trusted on the strength of how it was built. */
        if (all && puzzleValid(p)) {
            out = p;
            made = true;
        }
    }

    if (spent != nullptr) {
        *spent = budget - s.budget;
    }
    if (!made) {
        out = fallbackPuzzle(op, useLevel);
    }
    return made;
}

}  // namespace Nest
