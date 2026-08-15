#include "GameCatalog.h"
#include "engine/AppRegistry.h"

GameCatalogEntry gameCatalogAt(uint8_t index) {
    const AppDefinition& app = playableAppAt(index);
    return GameCatalogEntry{app.id(), app.title(), app.subtitle(), app.label(), app.blurb()};
}
