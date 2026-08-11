#pragma once

#include <Arduino.h>

/*
 * The single source of truth for the game list.
 *
 * main.cpp and SettingsGame previously each held their own array of the same
 * 23 id strings, coupled by index with nothing enforcing agreement. A silent
 * mismatch would hide the wrong game in Settings, and adding a game meant
 * editing both in lockstep.
 *
 * `id`       persisted visibility key -- never rename, it lives in NVS
 * `title`    launcher tile heading
 * `subtitle` launcher tile caption
 * `label`    Settings list row
 */
struct GameCatalogEntry {
    const char* id;
    const char* title;
    const char* subtitle;
    const char* label;
};

constexpr uint8_t GAME_CATALOG_COUNT = 23;
extern const GameCatalogEntry GAME_CATALOG[GAME_CATALOG_COUNT];
