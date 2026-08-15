#pragma once

#include <Arduino.h>
#include "engine/Game.h"
#include "engine/GameCatalog.h"
#include "ui/LauncherIcons.h"

class Board;
struct GameInstances;

struct AppDefinition {
    const GameCatalogEntry* catalog;
    const char* systemId;
    const char* systemTitle;
    const char* systemSubtitle;
    uint8_t catalogIndex;
    bool alwaysVisible;
    bool followsLayout;
    LauncherIcon icon;
    Game& (*instance)(GameInstances&);

    const char* id() const;
    const char* title() const;
    const char* subtitle() const;
    bool visible(Board& board) const;
    Game& game(GameInstances& games) const;
};

constexpr uint8_t SYSTEM_APP_COUNT = 6;
constexpr uint8_t APP_REGISTRY_COUNT = GAME_CATALOG_COUNT + SYSTEM_APP_COUNT;

extern const AppDefinition APP_REGISTRY[APP_REGISTRY_COUNT];

uint8_t appVisibleCount(Board& board);
const AppDefinition& appVisibleAt(Board& board, uint8_t filteredIndex);
