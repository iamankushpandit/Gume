#pragma once

#include <Arduino.h>

/*
 * The 118 chemical elements, as much of them as a player needs.
 *
 * Deliberately absent: atomic mass to four decimals, electron configuration,
 * electronegativity. None of it means anything to the audience and all of it
 * costs flash, which is the scarce resource here.
 *
 * `col` and `row` place the element in the folded 18-wide wall-chart layout.
 * Rows 8 and 9 are the lanthanide and actinide strips lifted out from under
 * periods 6 and 7, which is why row is 1..9 rather than 1..7.
 *
 * Regenerate with: python tools/gen_elements.py
 */

constexpr uint8_t ELEMENT_CATEGORY_COUNT = 10;

/* Index into ELEMENT_CATEGORY_NAMES. "Rare earth" and "Radioactive metal"
 * stand in for lanthanide and actinide: the proper names are jargon for a
 * six-year-old, and these two are what the labels actually mean. */
enum : uint8_t {
    ELEM_ALKALI = 0,
    ELEM_ALKALINE_EARTH,
    ELEM_TRANSITION,
    ELEM_METAL,
    ELEM_METALLOID,
    ELEM_NONMETAL,
    ELEM_HALOGEN,
    ELEM_NOBLE,
    ELEM_RARE_EARTH,
    ELEM_RADIOACTIVE
};

constexpr uint8_t ELEMENT_STATE_COUNT = 3;
enum : uint8_t { ELEM_SOLID = 0, ELEM_LIQUID, ELEM_GAS };

extern const char* const ELEMENT_CATEGORY_NAMES[ELEMENT_CATEGORY_COUNT];
extern const char* const ELEMENT_STATE_NAMES[ELEMENT_STATE_COUNT];

struct ElementFact {
    const char* symbol;    /**< One or two letters, e.g. "Fe". */
    const char* name;      /**< "Iron". US spellings, matching the rest of the catalog. */
    const char* fact;      /**< One player-facing line, <= 46 chars. */
    uint8_t z;             /**< Atomic number, 1..118. Also the proton count. */
    uint8_t col;           /**< Table column, 1..18. */
    uint8_t row;           /**< Table row, 1..9 (8 and 9 are the f-block strips). */
    uint8_t category : 4;  /**< ELEM_* category index. */
    uint8_t state : 2;     /**< ELEM_SOLID / LIQUID / GAS at room temperature. */
    uint8_t tier : 2;      /**< 1 = the famous ones, 2 = the everyday table, 3 = the rest. */
};

constexpr uint16_t ELEMENT_COUNT = 118;
extern const ElementFact ELEMENTS[ELEMENT_COUNT];

/** Category display name, or "?" if out of range. */
const char* elementCategoryName(uint8_t category);

/** State display name, or "?" if out of range. */
const char* elementStateName(uint8_t state);

/** Index of the element occupying (col, row), or 0xFF for an empty cell. */
uint8_t elementAtCell(uint8_t col, uint8_t row);

/** How many elements sit at or below `maxTier`. Pair with elementFromPool(). */
uint16_t elementPoolSize(uint8_t maxTier);

/** The n-th element (0-based) at or below `maxTier`, or 0xFF if out of range. */
uint8_t elementFromPool(uint16_t index, uint8_t maxTier);
