#pragma once

#include <Arduino.h>
#include "engine/AppCapabilities.h"
#include "engine/Game.h"
#include "ui/LauncherIcons.h"

class Board;
struct GameInstances;

struct AppScoreInfo {
    const char* gameId;
    const char* label;
    const char* bestKey;
    const char* unit;
    bool lowerIsBetter;
};

struct AppMetadata {
    const char* id;
    const char* title;
    const char* screenTitle;
    const char* subtitle;
    const char* label;
    const char* blurb;
    const AppScoreInfo* score;
    LauncherIcon icon;
    uint8_t launcherIndex;
    bool defaultVisible;
};

struct AppDefinition {
    const AppMetadata* metadata;
    const char* systemId;
    const char* systemTitle;
    const char* systemSubtitle;
    bool alwaysVisible;
    bool followsLayout;
    LauncherIcon systemIcon;
    uint32_t capabilities;
    Game& (*instance)(GameInstances&);

    const char* id() const;
    const char* title() const;
    const char* subtitle() const;
    const char* label() const;
    const char* blurb() const;
    const AppScoreInfo* score() const;
    LauncherIcon icon() const;
    uint8_t launcherIndex() const;
    bool defaultVisible() const;
    bool hasCapability(uint32_t capability) const;
    bool isCatalogApp() const;
    bool visible(Board& board) const;
    Game& game(GameInstances& games) const;
};

constexpr uint8_t PLAYABLE_APP_COUNT = 31;
constexpr uint8_t SYSTEM_APP_COUNT = 7;
constexpr uint8_t APP_REGISTRY_COUNT = PLAYABLE_APP_COUNT + SYSTEM_APP_COUNT;

extern const AppDefinition APP_REGISTRY[APP_REGISTRY_COUNT];

constexpr uint8_t playableAppCount() { return PLAYABLE_APP_COUNT; }
const AppDefinition& playableAppAt(uint8_t index);
uint8_t appVisibleCount(Board& board);
const AppDefinition& appVisibleAt(Board& board, uint8_t filteredIndex);
