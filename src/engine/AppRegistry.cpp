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
Game& scores(GameInstances& games) { return games.scores; }
Game& profiles(GameInstances& games) { return games.profile; }
Game& settings(GameInstances& games) { return games.settings; }
Game& wifi(GameInstances& games) { return games.wifi; }
Game& about(GameInstances& games) { return games.about; }
Game& systemInfo(GameInstances& games) { return games.systemInfo; }

AppDefinition catalogApp(uint8_t index, LauncherIcon icon, Game& (*instance)(GameInstances&),
                         bool followsLayout = false) {
    return AppDefinition{&GAME_CATALOG[index], nullptr, nullptr, nullptr,
                         index, false, followsLayout, icon, instance};
}

AppDefinition systemApp(const char* id, const char* title, const char* subtitle,
                        LauncherIcon icon, Game& (*instance)(GameInstances&),
                        bool followsLayout = false) {
    return AppDefinition{nullptr, id, title, subtitle, 0xFF,
                         true, followsLayout, icon, instance};
}

}

const AppDefinition APP_REGISTRY[APP_REGISTRY_COUNT] = {
    catalogApp(0, LauncherIcon::TicTacToe, ticTacToe),
    catalogApp(1, LauncherIcon::Memory, memory),
    catalogApp(2, LauncherIcon::Math, math),
    catalogApp(3, LauncherIcon::Multiplication, multiplication),
    catalogApp(4, LauncherIcon::Time, time),
    catalogApp(5, LauncherIcon::WhackAMole, whackAMole),
    catalogApp(6, LauncherIcon::Cinnamon, cinnamon),
    catalogApp(7, LauncherIcon::Microku, microku),
    catalogApp(8, LauncherIcon::ShapeColor, shapeColor),
    catalogApp(9, LauncherIcon::Counting, counting),
    catalogApp(10, LauncherIcon::Money, money),
    catalogApp(11, LauncherIcon::Fractions, fractions),
    catalogApp(12, LauncherIcon::Maze, maze),
    catalogApp(13, LauncherIcon::Sort, sort),
    catalogApp(14, LauncherIcon::ColorMix, colorMix),
    catalogApp(15, LauncherIcon::SlidingPuzzle, slidingPuzzle),
    catalogApp(16, LauncherIcon::OddOneOut, oddOneOut),
    catalogApp(17, LauncherIcon::ObjectAdd, objectAdd),
    catalogApp(18, LauncherIcon::FingerCount, fingerCount),
    catalogApp(19, LauncherIcon::Sequence, sequence),
    catalogApp(20, LauncherIcon::NumberLine, numberLine),
    catalogApp(21, LauncherIcon::Flag, flag),
    catalogApp(22, LauncherIcon::States, states),
    catalogApp(23, LauncherIcon::Trace, trace),
    catalogApp(24, LauncherIcon::StateFlag, stateFlag),
    catalogApp(25, LauncherIcon::StateMap, stateMap),
    catalogApp(26, LauncherIcon::Percent, percent),
    catalogApp(27, LauncherIcon::GreWords, greWords),
    systemApp("scores", "Scores", "best & worst", LauncherIcon::Scores, scores),
    systemApp("settings", "Settings", "device prefs", LauncherIcon::Settings, settings),
    systemApp("wifi", "Wi-Fi", "network & time", LauncherIcon::WiFi, wifi),
    systemApp("profiles", "Profiles", "switch player", LauncherIcon::Profiles, profiles, true),
    systemApp("systeminfo", "System Info", "device status", LauncherIcon::SystemInfo, systemInfo, true),
    systemApp("about", "About", "company info", LauncherIcon::About, about),
};

const char* AppDefinition::id() const {
    return catalog != nullptr ? catalog->id : systemId;
}

const char* AppDefinition::title() const {
    return catalog != nullptr ? catalog->title : systemTitle;
}

const char* AppDefinition::subtitle() const {
    return catalog != nullptr ? catalog->subtitle : systemSubtitle;
}

bool AppDefinition::visible(Board& board) const {
    return alwaysVisible || board.gameVisible(catalogIndex);
}

Game& AppDefinition::game(GameInstances& games) const {
    return instance(games);
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
