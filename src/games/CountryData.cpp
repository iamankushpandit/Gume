/*
 * Lookup helpers for the country facts table. Hand-written; the table data
 * itself lives in the generated CountryDataTable.cpp.
 */
#include "CountryData.h"
#include "map_n_flag.h"

#include <ctype.h>

const CountryFact* countryFact(const char* iso2) {
    if (iso2 == nullptr || iso2[0] == '\0' || iso2[1] == '\0') return nullptr;

    const char k0 = static_cast<char>(toupper(static_cast<unsigned char>(iso2[0])));
    const char k1 = static_cast<char>(toupper(static_cast<unsigned char>(iso2[1])));

    int lo = 0;
    int hi = static_cast<int>(COUNTRY_FACT_COUNT) - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const char* c = COUNTRY_FACTS[mid].iso2;
        int cmp = c[0] - k0;
        if (cmp == 0) cmp = c[1] - k1;
        if (cmp == 0) return &COUNTRY_FACTS[mid];
        if (cmp < 0) lo = mid + 1;
        else         hi = mid - 1;
    }
    return nullptr;
}

const char* continentName(uint8_t continent) {
    if (continent >= CONTINENT_COUNT) return "?";
    return CONTINENT_NAMES[continent];
}

/* A country qualifies if it is within the tier and, when an outline is
 * required, map-n-flag actually has one. FM, MH, PS and TV have flags but no
 * outline, so the geography game must skip them. */
static bool qualifies(const CountryFact& f, uint8_t maxTier, bool needsMap) {
    if (f.tier > maxTier) return false;
    /* Outlines were removed for licensing reasons, so needsMap can never be
     * satisfied. Kept in the signature so callers do not all have to change. */
    if (needsMap) return false;
    return mnf_flag(f.iso2) != nullptr;
}

uint16_t countryIndex(const CountryFact* fact) {
    if (fact == nullptr) return 0xFFFF;
    return static_cast<uint16_t>(fact - COUNTRY_FACTS);
}

bool countryQualifies(uint16_t i, uint8_t maxTier, bool needsMap) {
    if (i >= COUNTRY_FACT_COUNT) return false;
    return qualifies(COUNTRY_FACTS[i], maxTier, needsMap);
}

uint16_t countryPoolSize(uint8_t maxTier, bool needsMap) {
    uint16_t n = 0;
    for (uint16_t i = 0; i < COUNTRY_FACT_COUNT; ++i) {
        if (qualifies(COUNTRY_FACTS[i], maxTier, needsMap)) ++n;
    }
    return n;
}

const CountryFact* countryFromPool(uint16_t index, uint8_t maxTier, bool needsMap) {
    for (uint16_t i = 0; i < COUNTRY_FACT_COUNT; ++i) {
        if (!qualifies(COUNTRY_FACTS[i], maxTier, needsMap)) continue;
        if (index == 0) return &COUNTRY_FACTS[i];
        --index;
    }
    return nullptr;
}
