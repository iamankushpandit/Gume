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
};

void drawLauncherIcon(Ui::Renderer& tft, LauncherIcon icon, const Rect& r,
                      uint16_t fill, int16_t cx, int16_t cy);
