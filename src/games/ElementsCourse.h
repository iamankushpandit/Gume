// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ElementData.h"

/* WHAT ELEMENTS TEACHES, LEVEL BY LEVEL.
 *
 * The quiz drew one of six kinds of question at random, for an element
 * weighted by mastery, from a pool that grew when enough of the easy
 * elements had been answered right once in any way -- so a player who could
 * name Iron was taken to know where it sits and how many protons it has.
 *
 * This course asks the 26 famous elements' names, then their symbols and
 * states, then where they sit, their protons and the element with N
 * protons; the 63 common elements the same three ways round, with families
 * in the middle; then all 118: names, then places (row, column, family).
 * The last level is all of it.
 *
 * Every kind says which elements it can honestly ask about: a state only
 * for an element a player could meet; a family only where the label has
 * one right answer (an alkali metal, not "a metal"); a fact only where it
 * does not give its element away and no other element in the table fits
 * it too.
 *
 * Pure C++ apart from the table's header: test/host/elements_course_test.cpp
 * compiles this file with the real table through the PC build's shim. */
namespace ElementsCourse {

constexpr uint8_t LEVEL_COUNT = 9;

enum class Kind : uint8_t {
    SymbolToName,   // Which element is Fe?
    NameToSymbol,   // What is the symbol for Gold?
    FindIt,         // Find Oxygen in the table
    ProtonCount,    // How many protons does Carbon have?
    FactToElement,  // the fact -> four names
    StateOf,        // Mercury at room temperature is a...
    ZToElement,     // Which element has 8 protons?
    FamilyOf,       // Which family is Argon in?
    WhichIsFamily,  // Which one is a noble gas?
    NextAfter,      // Which element comes right after Neon?
    RowOf,          // Which row of the table is Iron in?
    SameColumn,     // Which is in the same column as Sodium?
    COUNT
};

constexpr uint16_t kindBit(Kind k) { return static_cast<uint16_t>(1u << static_cast<uint8_t>(k)); }

struct Level {
    const char* name;
    uint8_t tier;     // elements of tier <= this
    uint16_t kinds;   // kindBit(Kind) set
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Names", 1, kindBit(Kind::SymbolToName) | kindBit(Kind::FactToElement)},
    {"Symbols", 1, kindBit(Kind::NameToSymbol) | kindBit(Kind::StateOf)},
    {"The chart", 1, kindBit(Kind::FindIt) | kindBit(Kind::ProtonCount) | kindBit(Kind::ZToElement)},
    {"More elements", 2,
     kindBit(Kind::SymbolToName) | kindBit(Kind::NameToSymbol) | kindBit(Kind::FactToElement)},
    {"Families", 2, kindBit(Kind::FamilyOf) | kindBit(Kind::WhichIsFamily) | kindBit(Kind::StateOf)},
    {"Numbers", 2,
     kindBit(Kind::ProtonCount) | kindBit(Kind::ZToElement) | kindBit(Kind::FindIt) | kindBit(Kind::NextAfter)},
    {"All 118: names", 3,
     kindBit(Kind::SymbolToName) | kindBit(Kind::NameToSymbol) | kindBit(Kind::FactToElement)},
    {"All 118: places", 3,
     kindBit(Kind::FindIt) | kindBit(Kind::RowOf) | kindBit(Kind::SameColumn) | kindBit(Kind::FamilyOf)},
    {"Mastery", 3, 0x0FFF},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const Level& levelOf(uint8_t level) { return LEVELS[clampLevel(level) - 1]; }
inline const char* levelName(uint8_t level) { return levelOf(level).name; }
inline uint8_t tierFor(uint8_t level) { return levelOf(level).tier; }

constexpr uint8_t LEVEL_LINE_MAX = 30;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* The row a player counts: the two f-block strips belong to rows 6 and 7. */
inline uint8_t tableRow(uint8_t e) {
    const uint8_t r = ELEMENTS[e].row;
    return r == 8 ? 6 : (r == 9 ? 7 : r);
}

/* Family labels with one right answer. "Metal", "Nonmetal", "Rare earth"
 * and "Radioactive metal" are true of elements filed under other labels. */
inline bool clearFamily(uint8_t category) {
    return category == ELEM_ALKALI || category == ELEM_ALKALINE_EARTH ||
           category == ELEM_TRANSITION || category == ELEM_METALLOID ||
           category == ELEM_HALOGEN || category == ELEM_NOBLE;
}
/* "Which one is a ...?" only for the four families nothing else can be
 * mistaken for. */
inline bool askableFamily(uint8_t category) {
    return category == ELEM_ALKALI || category == ELEM_ALKALINE_EARTH ||
           category == ELEM_HALOGEN || category == ELEM_NOBLE;
}

/* Facts true of more than one element, by symbol: these are never offered
 * against each other. */
constexpr const char* FACT_CLASHES[][3] = {
    {"Po", "Ra", nullptr}, {"Rb", "Cs", nullptr}, {"V", "Mn", "Mo"},
};
constexpr uint8_t FACT_CLASH_COUNT = sizeof(FACT_CLASHES) / sizeof(FACT_CLASHES[0]);

inline bool factClash(uint8_t a, uint8_t b) {
    for (uint8_t g = 0; g < FACT_CLASH_COUNT; ++g) {
        bool hasA = false, hasB = false;
        for (uint8_t k = 0; k < 3 && FACT_CLASHES[g][k] != nullptr; ++k) {
            hasA = hasA || strcmp(FACT_CLASHES[g][k], ELEMENTS[a].symbol) == 0;
            hasB = hasB || strcmp(FACT_CLASHES[g][k], ELEMENTS[b].symbol) == 0;
        }
        if (hasA && hasB) return true;
    }
    return false;
}

/* Does the fact name its own element ("Tin cans, and solder...")? */
inline bool factGivesAway(uint8_t e) {
    const char* fact = ELEMENTS[e].fact;
    const char* name = ELEMENTS[e].name;
    const size_t n = strlen(name), f = strlen(fact);
    for (size_t i = 0; i + n <= f; ++i) {
        size_t k = 0;
        while (k < n && tolower(static_cast<unsigned char>(fact[i + k])) ==
                            tolower(static_cast<unsigned char>(name[k]))) ++k;
        if (k == n) return true;
    }
    return false;
}

inline uint8_t byZ(uint8_t z) {
    for (uint8_t i = 0; i < ELEMENT_COUNT; ++i)
        if (ELEMENTS[i].z == z) return i;
    return 0xFF;
}

inline bool inTier(uint8_t e, uint8_t tier) { return e < ELEMENT_COUNT && ELEMENTS[e].tier <= tier; }

/* Can `kind` be asked about element `e` with the pool of `tier`? */
inline bool applies(Kind kind, uint8_t e, uint8_t tier) {
    const ElementFact& x = ELEMENTS[e];
    switch (kind) {
        case Kind::StateOf: return x.tier <= 2;   // beyond, the state is a prediction
        case Kind::FactToElement: return !factGivesAway(e);
        case Kind::FamilyOf: return clearFamily(x.category);
        case Kind::WhichIsFamily: return askableFamily(x.category);
        case Kind::NextAfter: {
            const uint8_t n = byZ(static_cast<uint8_t>(x.z + 1));
            return x.z < 118 && inTier(n, tier);
        }
        case Kind::SameColumn: {
            if (x.row > 7) return false;
            for (uint8_t i = 0; i < ELEMENT_COUNT; ++i)
                if (i != e && inTier(i, tier) && ELEMENTS[i].col == x.col && ELEMENTS[i].row <= 7) return true;
            return false;
        }
        default: return true;
    }
}

/* The `turn`-th kind of the level that applies to `e`, so an element gets
 * each of its level's kinds in turn. SymbolToName when none applies. */
inline Kind kindFor(uint8_t level, uint8_t e, uint8_t turn) {
    const Level& l = levelOf(level);
    Kind usable[static_cast<uint8_t>(Kind::COUNT)];
    uint8_t n = 0;
    for (uint8_t k = 0; k < static_cast<uint8_t>(Kind::COUNT); ++k) {
        const Kind kind = static_cast<Kind>(k);
        if ((l.kinds & kindBit(kind)) && applies(kind, e, l.tier)) usable[n++] = kind;
    }
    return n == 0 ? Kind::SymbolToName : usable[turn % n];
}

/* What the question is about, and what its options are. A value is an
 * element index, except for StateOf (a state), FamilyOf (a category) and
 * RowOf (a row number). Option 0 is the answer; the caller shuffles. */
struct Question {
    Kind kind;
    uint8_t element;   // what the question is about
    uint8_t count;     // 0 for FindIt, 3 for StateOf, else 4
    uint8_t values[4];
};

namespace detail {
inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
inline int gap(uint8_t a, uint8_t b) {
    return static_cast<int>(ELEMENTS[a].z) - static_cast<int>(ELEMENTS[b].z);
}
}  // namespace detail

/* The element that is the right answer, for kinds whose answer is one. */
inline uint8_t answerElement(const Question& q, uint8_t tier) {
    const ElementFact& x = ELEMENTS[q.element];
    switch (q.kind) {
        case Kind::NextAfter: return byZ(static_cast<uint8_t>(x.z + 1));
        case Kind::SameColumn: {
            /* The nearest one in the column, of the level's elements. */
            uint8_t best = q.element;
            int bestGap = 99;
            for (uint8_t i = 0; i < ELEMENT_COUNT; ++i) {
                if (i == q.element || !inTier(i, tier) || ELEMENTS[i].col != x.col || ELEMENTS[i].row > 7) continue;
                const int g = ELEMENTS[i].row > x.row ? ELEMENTS[i].row - x.row : x.row - ELEMENTS[i].row;
                if (g < bestGap) { bestGap = g; best = i; }
            }
            return best;
        }
        default: return q.element;
    }
}

/* Is element `c` an acceptable wrong option for question `q`? `strict` adds
 * the closeness that makes a wrong option worth thinking about. */
inline bool wrongElementOk(const Question& q, uint8_t ans, uint8_t c, uint8_t tier, bool strict) {
    if (!inTier(c, tier) || c == ans || c == q.element) return false;
    const ElementFact& x = ELEMENTS[q.element];
    switch (q.kind) {
        case Kind::FactToElement: return !factClash(ans, c);
        case Kind::ProtonCount:
        case Kind::ZToElement:
        case Kind::NextAfter: {
            const int g = detail::gap(c, ans);
            if (q.kind == Kind::NextAfter && ELEMENTS[c].z == x.z) return false;
            return !strict || (g <= 6 && g >= -6);
        }
        case Kind::WhichIsFamily: return ELEMENTS[c].category != x.category;
        case Kind::SameColumn:
            return ELEMENTS[c].col != x.col && (!strict || tableRow(c) == tableRow(ans));
        default: return true;
    }
}

inline void make(uint8_t level, uint8_t e, uint8_t turn, uint32_t seed, Question& q) {
    uint32_t s = seed == 0 ? 0x1B873593u : seed;
    /* An element above the level ("Quiz me" on the card) widens the pool to
     * its own tier, or it would be the one unfamiliar name among four. */
    const uint8_t tier = ELEMENTS[e].tier > tierFor(level) ? ELEMENTS[e].tier : tierFor(level);
    q.element = e;
    q.kind = kindFor(level, e, turn);
    const ElementFact& x = ELEMENTS[e];
    switch (q.kind) {
        case Kind::FindIt:
            q.count = 0;
            return;
        case Kind::StateOf:
            q.count = 3;
            q.values[0] = x.state;
            q.values[1] = static_cast<uint8_t>((x.state + 1) % 3);
            q.values[2] = static_cast<uint8_t>((x.state + 2) % 3);
            return;
        case Kind::FamilyOf: {
            static constexpr uint8_t FAMILIES[6] = {ELEM_ALKALI, ELEM_ALKALINE_EARTH, ELEM_TRANSITION,
                                                    ELEM_METALLOID, ELEM_HALOGEN, ELEM_NOBLE};
            q.count = 4;
            q.values[0] = x.category;
            uint8_t n = 1;
            for (uint8_t tries = 0; tries < 60 && n < 4; ++tries) {
                const uint8_t c = FAMILIES[detail::stir(s) % 6];
                bool taken = false;
                for (uint8_t k = 0; k < n; ++k) taken = taken || q.values[k] == c;
                if (!taken) q.values[n++] = c;
            }
            return;
        }
        case Kind::RowOf: {
            q.count = 4;
            const uint8_t r = tableRow(e);
            q.values[0] = r;
            uint8_t n = 1;
            /* The rows beside it are the mistakes worth offering. */
            for (uint8_t tries = 0; tries < 60 && n < 4; ++tries) {
                const int d = static_cast<int>(detail::stir(s) % 7) - 3;
                const int c = static_cast<int>(r) + d;
                if (d == 0 || c < 1 || c > 7) continue;
                bool taken = false;
                for (uint8_t k = 0; k < n; ++k) taken = taken || q.values[k] == c;
                if (!taken) q.values[n++] = static_cast<uint8_t>(c);
            }
            for (uint8_t c = 1; c <= 7 && n < 4; ++c) {
                bool taken = false;
                for (uint8_t k = 0; k < n; ++k) taken = taken || q.values[k] == c;
                if (!taken) q.values[n++] = c;
            }
            return;
        }
        default:
            break;
    }
    /* The rest have element options. */
    q.count = 4;
    const uint8_t ans = answerElement(q, tier);
    q.values[0] = ans;
    uint8_t n = 1;
    for (uint8_t pass = 0; pass < 2 && n < 4; ++pass) {
        for (uint16_t tries = 0; tries < 200 && n < 4; ++tries) {
            const uint8_t c = static_cast<uint8_t>(detail::stir(s) % ELEMENT_COUNT);
            if (!wrongElementOk(q, ans, c, tier, pass == 0)) continue;
            bool taken = false;
            for (uint8_t k = 0; k < n; ++k) taken = taken || q.values[k] == c;
            if (!taken) q.values[n++] = c;
        }
    }
    for (uint8_t c = 0; c < ELEMENT_COUNT && n < 4; ++c) {
        bool taken = false;
        for (uint8_t k = 0; k < n; ++k) taken = taken || q.values[k] == c;
        if (!taken && wrongElementOk(q, ans, c, 3, false)) q.values[n++] = c;
    }
}

/* The question line. */
inline void prompt(const Question& q, char* out, size_t len) {
    const ElementFact& x = ELEMENTS[q.element];
    static constexpr const char* FAMILY_A[ELEMENT_CATEGORY_COUNT] = {
        "an alkali metal", "an alkaline earth", "", "", "", "", "a halogen", "a noble gas", "", ""};
    switch (q.kind) {
        case Kind::SymbolToName: snprintf(out, len, "Which element is %s?", x.symbol); break;
        case Kind::NameToSymbol: snprintf(out, len, "What is the symbol for %s?", x.name); break;
        case Kind::FindIt: snprintf(out, len, "Find %s in the table", x.name); break;
        case Kind::ProtonCount: snprintf(out, len, "How many protons does %s have?", x.name); break;
        case Kind::FactToElement: snprintf(out, len, "%s", x.fact); break;
        case Kind::StateOf: snprintf(out, len, "%s at room temperature is a...", x.name); break;
        case Kind::ZToElement:
            snprintf(out, len, "Which element has %u protons?", static_cast<unsigned>(x.z));
            break;
        case Kind::FamilyOf: snprintf(out, len, "Which family is %s in?", x.name); break;
        case Kind::WhichIsFamily: snprintf(out, len, "Which one is %s?", FAMILY_A[x.category]); break;
        case Kind::NextAfter: snprintf(out, len, "Which element comes right after %s?", x.name); break;
        case Kind::RowOf: snprintf(out, len, "Which row of the table is %s in?", x.name); break;
        case Kind::SameColumn: snprintf(out, len, "Which is in the same column as %s?", x.name); break;
        default: out[0] = '\0'; break;
    }
}

/* A button's label. */
inline void label(const Question& q, uint8_t value, char* out, size_t len) {
    switch (q.kind) {
        case Kind::NameToSymbol: snprintf(out, len, "%s", ELEMENTS[value].symbol); break;
        case Kind::ProtonCount: snprintf(out, len, "%u", static_cast<unsigned>(ELEMENTS[value].z)); break;
        case Kind::StateOf: snprintf(out, len, "%s", elementStateName(value)); break;
        case Kind::FamilyOf: snprintf(out, len, "%s", elementCategoryName(value)); break;
        case Kind::RowOf: snprintf(out, len, "Row %u", static_cast<unsigned>(value)); break;
        default: snprintf(out, len, "%s", ELEMENTS[value].name); break;
    }
}

}  // namespace ElementsCourse
