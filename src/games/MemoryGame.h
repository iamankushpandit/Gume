#pragma once

#include "engine/Game.h"
#include "engine/ContentLoader.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& memoryAppMetadata();

class MemoryGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    void newRound(AppContext& host);
    int8_t cardAt(int16_t x, int16_t y) const;
    Rect cardRect(uint8_t index) const;
    bool allMatched() const;

    MemoryConfig config_;
    uint8_t cards_[MAX_MEMORY_CARDS] = {};
    bool visible_[MAX_MEMORY_CARDS] = {};
    bool matched_[MAX_MEMORY_CARDS] = {};
    int8_t first_ = -1;
    int8_t second_ = -1;
    uint16_t moves_ = 0;
    uint16_t bestMoves_ = 0;
    uint32_t resolveAt_ = 0;
    bool resolving_ = false;
};
