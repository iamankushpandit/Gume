#pragma once

#include <Arduino.h>
#include "hal/Board.h"

constexpr uint8_t MAX_MEMORY_ROWS = 4;
constexpr uint8_t MAX_MEMORY_COLS = 6;
constexpr uint8_t MAX_MEMORY_PAIRS = 12;
constexpr uint8_t MAX_MEMORY_CARDS = MAX_MEMORY_PAIRS * 2;

struct MemoryConfig {
    uint8_t rows = 4;
    uint8_t cols = 6;
    uint8_t pairCount = 12;
    String symbols[MAX_MEMORY_PAIRS] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L"};
};

struct CountingConfig {
    uint8_t minCount = 1;
    uint8_t maxCount = 10;
};

class ContentLoader {
public:
    bool begin(Board& board);
    void scan();

    bool loadMemoryConfig(MemoryConfig& config) const;
    bool loadCountingConfig(CountingConfig& config) const;

    bool sdReady() const;

private:
    String absoluteEntryPath(const String& directory, const String& entryName) const;

    Board* board_ = nullptr;
};
