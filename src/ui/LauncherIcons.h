#pragma once

#include <Arduino.h>
#include "ui/Ui.h"

enum class LauncherIcon : uint8_t {
    TicTacToe,
    Memory,
    Math,
    Multiplication,
    Time,
    WhackAMole,
    Cinnamon,
    Microku,
    ShapeColor,
    Counting,
    Money,
    Fractions,
    Maze,
    Sort,
    ColorMix,
    SlidingPuzzle,
    OddOneOut,
    ObjectAdd,
    FingerCount,
    Sequence,
    NumberLine,
    Flag,
    States,
    Trace,
    StateFlag,
    StateMap,
    Percent,
    GreWords,
    Dice,
    CoinFlip,
    Profiles,
    Scores,
    Settings,
    WiFi,
    About,
    SystemInfo,
    Nearby,
};

/* Half-width of the box an icon occupies. Per-board: a 320x240 panel's 145x46
 * landscape tile takes about 18, the 4-inch board's 228x73 tile carries 24.
 * Published so callers can position text beside the icon. */
#ifndef LAUNCHER_ICON_HALF
#define LAUNCHER_ICON_HALF 18
#endif
constexpr int16_t LAUNCHER_ICON_BOX = LAUNCHER_ICON_HALF;

/* Smallest tile height any supported panel produces: 320x240 landscape, at 46.
 * An icon centred half a tile down cannot exceed that half without overrunning
 * the tile, which is asserted where ICON_HALF is defined. */
#ifndef LAUNCHER_MIN_TILE_H
#define LAUNCHER_MIN_TILE_H 46
#endif

void drawLauncherIcon(Ui::Renderer& tft, LauncherIcon icon, const Rect& r,
                      uint16_t fill, int16_t cx, int16_t cy);
