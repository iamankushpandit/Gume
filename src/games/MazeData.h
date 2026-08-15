#pragma once

#include <Arduino.h>

namespace MazeData {
constexpr uint8_t COLS = 12;
constexpr uint8_t ROWS = 8;

extern const char* const MAZES[][ROWS];
extern const char* const FALLBACK_MAZE[ROWS];
extern const uint8_t COUNT;
}
