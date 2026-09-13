// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

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
    Elements,
    Piano,
    Chess,
    SeaBattle,
    Cursive,
    Ludo,
    Backgammon,
    Go,
    Space,
    Roman,
    Profiles,
    Scores,
    Settings,
    WiFi,
    About,
    SystemInfo,
    Nearby,
};

void drawLauncherIcon(Ui::Renderer& tft, LauncherIcon icon, const Rect& r,
                      uint16_t fill, int16_t cx, int16_t cy);
