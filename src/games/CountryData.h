#pragma once

#include <Arduino.h>

/*
 * Capital cities, continents and difficulty tiers for the 195 countries that
 * the map-n-flag library carries artwork for.
 *
 * Country NAMES and the flag/outline images come from map-n-flag (mnf_name(),
 * mnf_flag(), mnf_map()). This table only adds what that library does not
 * store, so the two are keyed by the same ISO 3166-1 alpha-2 code.
 *
 * Regenerate with: python tools/gen_country_facts.py
 */

constexpr uint8_t CONTINENT_COUNT = 6;

/* Index into CONTINENT_NAMES. No UN member sits in Antarctica, so it is not
 * a category here -- the geography game asks which continent a country is in. */
enum : uint8_t {
    CONT_AFRICA = 0,
    CONT_ASIA,
    CONT_EUROPE,
    CONT_NORTH_AMERICA,
    CONT_SOUTH_AMERICA,
    CONT_OCEANIA
};

extern const char* const CONTINENT_NAMES[CONTINENT_COUNT];

struct CountryFact {
    const char* iso2;      /**< Upper-case ISO 3166-1 alpha-2, matches map-n-flag. */
    const char* capital;   /**< ASCII capital city, e.g. "Paris". */
    uint8_t continent;     /**< CONT_* index. */
    uint8_t tier;          /**< 1 = most familiar, 2 = well known, 3 = the rest. */
};

extern const CountryFact COUNTRY_FACTS[];
extern const uint16_t COUNTRY_FACT_COUNT;

/** Difficulty selectable in the geography/flag games. */
enum class GeoTier : uint8_t { Easy = 1, Medium = 2, Hard = 3 };

/** Look up by ISO code (case-insensitive). NULL if unknown. */
const CountryFact* countryFact(const char* iso2);

/** Continent display name, or "?" if out of range. */
const char* continentName(uint8_t continent);

/**
 * Number of countries at or below `maxTier` that also satisfy `needsMap`.
 * Four countries (FM, MH, PS, TV) have a flag but no outline in map-n-flag.
 */
uint16_t countryPoolSize(uint8_t maxTier, bool needsMap);

/**
 * The n-th country (0-based) in the same filtered pool. Returns NULL if out of
 * range. Pair with countryPoolSize() to pick a random entry.
 */
const CountryFact* countryFromPool(uint16_t index, uint8_t maxTier, bool needsMap);

/** Position of a fact within COUNTRY_FACTS, the key used by Progress. */
uint16_t countryIndex(const CountryFact* fact);

/** True if the country at COUNTRY_FACTS[i] is in the tier and has the art. */
bool countryQualifies(uint16_t i, uint8_t maxTier, bool needsMap);
