#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class SlidingPuzzleGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    Rect tileRect(uint8_t index) const;
    void setupSolved(uint8_t size);
    void shuffle();
    bool solved() const;
    bool adjacentToGap(uint8_t index) const;
    void slide(uint8_t index);
    int8_t touchedTile(int16_t x, int16_t y) const;
    void loadBest(GameHost& host);
    void saveBest(GameHost& host);

    uint8_t size_ = 2;
    uint8_t tiles_[9] = {};
    uint8_t gap_ = 3;
    uint16_t moves_ = 0;
    uint16_t bestMoves_ = 0;
    bool won_ = false;
};

