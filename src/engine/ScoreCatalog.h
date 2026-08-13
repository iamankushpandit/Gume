#pragma once

#include <Arduino.h>

/*
 * What each game records, so the Scores screen can present it without knowing
 * anything about the games themselves.
 *
 * `lowerIsBetter` matters: for Maze and the timed Math run a *smaller* number
 * is the better result, so showing them alongside high scores unqualified
 * would be misleading.
 */
struct ScoreEntry {
    const char* gameId;      // matches GameCatalog id
    const char* label;       // shown on the Scores screen
    const char* bestKey;     // NVS key, profile-scoped by Board
    const char* unit;        // "pts", "s", "lvl" ...
    bool lowerIsBetter;
};

extern const ScoreEntry SCORE_CATALOG[];
extern const uint8_t SCORE_CATALOG_COUNT;
