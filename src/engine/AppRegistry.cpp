#include "engine/AppRegistry.h"

#include "games/GameInstances.h"
#include "hal/Board.h"

namespace {

Game& ticTacToe(GameInstances& games) { return games.ticTacToe; }
Game& memory(GameInstances& games) { return games.memory; }
Game& math(GameInstances& games) { return games.math; }
Game& multiplication(GameInstances& games) { return games.multiplication; }
Game& time(GameInstances& games) { return games.time; }
Game& whackAMole(GameInstances& games) { return games.whackAMole; }
Game& cinnamon(GameInstances& games) { return games.cinnamon; }
Game& microku(GameInstances& games) { return games.microku; }
Game& shapeColor(GameInstances& games) { return games.shapeColor; }
Game& counting(GameInstances& games) { return games.counting; }
Game& money(GameInstances& games) { return games.money; }
Game& fractions(GameInstances& games) { return games.fractions; }
Game& maze(GameInstances& games) { return games.maze; }
Game& sort(GameInstances& games) { return games.sort; }
Game& colorMix(GameInstances& games) { return games.colorMix; }
Game& slidingPuzzle(GameInstances& games) { return games.slidingPuzzle; }
Game& oddOneOut(GameInstances& games) { return games.oddOneOut; }
Game& objectAdd(GameInstances& games) { return games.objectAdd; }
Game& fingerCount(GameInstances& games) { return games.fingerCount; }
Game& sequence(GameInstances& games) { return games.sequence; }
Game& numberLine(GameInstances& games) { return games.numberLine; }
Game& flag(GameInstances& games) { return games.flag; }
Game& states(GameInstances& games) { return games.states; }
Game& trace(GameInstances& games) { return games.trace; }
Game& stateFlag(GameInstances& games) { return games.stateFlag; }
Game& stateMap(GameInstances& games) { return games.stateMap; }
Game& percent(GameInstances& games) { return games.percent; }
Game& greWords(GameInstances& games) { return games.greWords; }
Game& dice(GameInstances& games) { return games.dice; }
Game& coinFlip(GameInstances& games) { return games.coinFlip; }
Game& elements(GameInstances& games) { return games.elements; }
Game& scores(GameInstances& games) { return games.scores; }
Game& profiles(GameInstances& games) { return games.profile; }
Game& settings(GameInstances& games) { return games.settings; }
Game& wifi(GameInstances& games) { return games.wifi; }
Game& about(GameInstances& games) { return games.about; }
Game& systemInfo(GameInstances& games) { return games.systemInfo; }
Game& nearby(GameInstances& games) { return games.nearby; }

AppDefinition metadataCatalogApp(const AppMetadata& metadata, Game& (*instance)(GameInstances&),
                                 bool followsLayout = false) {
    return AppDefinition{&metadata, nullptr, nullptr, nullptr,
                         false, followsLayout, LauncherIcon::About, APP_CAP_NONE, instance};
}

AppDefinition systemApp(const char* id, const char* title, const char* subtitle,
                        LauncherIcon icon, Game& (*instance)(GameInstances&),
                        uint32_t capabilities, bool followsLayout = false) {
    return AppDefinition{nullptr, id, title, subtitle,
                         true, followsLayout, icon, capabilities, instance};
}

}

const AppDefinition APP_REGISTRY[APP_REGISTRY_COUNT] = {
    metadataCatalogApp(ticTacToeAppMetadata(), ticTacToe),
    metadataCatalogApp(memoryAppMetadata(), memory),
    metadataCatalogApp(mathAppMetadata(), math),
    metadataCatalogApp(multiplicationAppMetadata(), multiplication),
    metadataCatalogApp(timeGameAppMetadata(), time),
    metadataCatalogApp(whackAMoleAppMetadata(), whackAMole),
    metadataCatalogApp(cinnamonAppMetadata(), cinnamon),
    metadataCatalogApp(microkuAppMetadata(), microku),
    metadataCatalogApp(shapeColorAppMetadata(), shapeColor),
    metadataCatalogApp(countingAppMetadata(), counting),
    metadataCatalogApp(moneyAppMetadata(), money),
    metadataCatalogApp(fractionAppMetadata(), fractions),
    metadataCatalogApp(mazeAppMetadata(), maze),
    metadataCatalogApp(sortAppMetadata(), sort),
    metadataCatalogApp(colorMixAppMetadata(), colorMix),
    metadataCatalogApp(slidingPuzzleAppMetadata(), slidingPuzzle),
    metadataCatalogApp(oddOneOutAppMetadata(), oddOneOut),
    metadataCatalogApp(objectAddAppMetadata(), objectAdd),
    metadataCatalogApp(fingerCountAppMetadata(), fingerCount),
    metadataCatalogApp(sequenceAppMetadata(), sequence),
    metadataCatalogApp(numberLineAppMetadata(), numberLine),
    metadataCatalogApp(flagAppMetadata(), flag),
    metadataCatalogApp(statesAppMetadata(), states),
    metadataCatalogApp(traceAppMetadata(), trace),
    metadataCatalogApp(stateFlagAppMetadata(), stateFlag),
    metadataCatalogApp(stateMapAppMetadata(), stateMap),
    metadataCatalogApp(percentCircleAppMetadata(), percent),
    metadataCatalogApp(greWordsAppMetadata(), greWords),
    metadataCatalogApp(diceAppMetadata(), dice),
    metadataCatalogApp(coinFlipAppMetadata(), coinFlip),
    metadataCatalogApp(elementsAppMetadata(), elements),
    systemApp("scores", "Scores", "best & worst", LauncherIcon::Scores, scores,
              APP_CAP_SCORES),
    systemApp("settings", "Settings", "device prefs", LauncherIcon::Settings, settings,
              APP_CAP_DEVICE_STATUS | APP_CAP_DEVICE_SETTINGS | APP_CAP_FACTORY_RESET),
    systemApp("wifi", "Wi-Fi", "network & time", LauncherIcon::WiFi, wifi,
              APP_CAP_DEVICE_STATUS | APP_CAP_NETWORK),
    systemApp("profiles", "Profiles", "switch player", LauncherIcon::Profiles, profiles,
              APP_CAP_PROFILES, true),
    systemApp("systeminfo", "System Info", "device status", LauncherIcon::SystemInfo, systemInfo,
              APP_CAP_DEVICE_STATUS | APP_CAP_DIAGNOSTICS, true),
    systemApp("about", "About", "company info", LauncherIcon::About, about,
              APP_CAP_DEVICE_STATUS),
    systemApp("nearby", "Nearby", "who else is here", LauncherIcon::Nearby, nearby,
              APP_CAP_DEVICE_STATUS | APP_CAP_DEVICE_SETTINGS, true),
};

const char* AppDefinition::id() const {
    if (metadata != nullptr) {
        return metadata->id;
    }
    return metadata != nullptr ? metadata->id : systemId;
}

const char* AppDefinition::title() const {
    return metadata != nullptr ? metadata->title : systemTitle;
}

const char* AppDefinition::subtitle() const {
    return metadata != nullptr ? metadata->subtitle : systemSubtitle;
}

const char* AppDefinition::label() const {
    return metadata != nullptr ? metadata->label : systemTitle;
}

const char* AppDefinition::blurb() const {
    return metadata != nullptr ? metadata->blurb : systemSubtitle;
}

const AppScoreInfo* AppDefinition::score() const {
    return metadata != nullptr ? metadata->score : nullptr;
}

LauncherIcon AppDefinition::icon() const {
    return metadata != nullptr ? metadata->icon : systemIcon;
}

uint8_t AppDefinition::launcherIndex() const {
    return metadata != nullptr ? metadata->launcherIndex : 0xFF;
}

bool AppDefinition::defaultVisible() const {
    return metadata == nullptr || metadata->defaultVisible;
}

bool AppDefinition::hasCapability(uint32_t capability) const {
    return capability == APP_CAP_NONE || (capabilities & capability) == capability;
}

bool AppDefinition::isCatalogApp() const {
    return metadata != nullptr;
}

bool AppDefinition::visible(Board& board) const {
    return alwaysVisible ||
           (metadata != nullptr && board.gameVisible(metadata->launcherIndex,
                                                     defaultVisible()));
}

Game& AppDefinition::game(GameInstances& games) const {
    return instance(games);
}

const AppDefinition& playableAppAt(uint8_t index) {
    return APP_REGISTRY[index];
}

uint8_t appVisibleCount(Board& board) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < APP_REGISTRY_COUNT; ++i) {
        if (APP_REGISTRY[i].visible(board)) {
            ++count;
        }
    }
    return count;
}

const AppDefinition& appVisibleAt(Board& board, uint8_t filteredIndex) {
    uint8_t visibleIndex = 0;
    for (uint8_t i = 0; i < APP_REGISTRY_COUNT; ++i) {
        if (!APP_REGISTRY[i].visible(board)) {
            continue;
        }
        if (visibleIndex == filteredIndex) {
            return APP_REGISTRY[i];
        }
        ++visibleIndex;
    }
    return APP_REGISTRY[APP_REGISTRY_COUNT - 1];
}
