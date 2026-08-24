#include "ElementData.h"

/*
 * Lookups over the generated table. Kept out of ElementDataTable.cpp so that
 * regenerating the data never rewrites hand-written code -- the same split
 * CountryData.cpp / CountryDataTable.cpp uses.
 *
 * Every one of these is linear over 118 entries. That is deliberate: they run
 * on a tap or on a new question, never per frame, and a scan of 118 flash
 * reads is far cheaper than the index tables it would take to avoid it.
 */

const char* elementCategoryName(uint8_t category) {
    return category < ELEMENT_CATEGORY_COUNT ? ELEMENT_CATEGORY_NAMES[category] : "?";
}

const char* elementStateName(uint8_t state) {
    return state < ELEMENT_STATE_COUNT ? ELEMENT_STATE_NAMES[state] : "?";
}

uint8_t elementAtCell(uint8_t col, uint8_t row) {
    for (uint16_t i = 0; i < ELEMENT_COUNT; ++i) {
        if (ELEMENTS[i].col == col && ELEMENTS[i].row == row) {
            return static_cast<uint8_t>(i);
        }
    }
    return 0xFF;
}

uint16_t elementPoolSize(uint8_t maxTier) {
    uint16_t n = 0;
    for (uint16_t i = 0; i < ELEMENT_COUNT; ++i) {
        if (ELEMENTS[i].tier <= maxTier) ++n;
    }
    return n;
}

uint8_t elementFromPool(uint16_t index, uint8_t maxTier) {
    for (uint16_t i = 0; i < ELEMENT_COUNT; ++i) {
        if (ELEMENTS[i].tier > maxTier) continue;
        if (index == 0) return static_cast<uint8_t>(i);
        --index;
    }
    return 0xFF;
}
