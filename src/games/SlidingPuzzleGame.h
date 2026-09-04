#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& slidingPuzzleAppMetadata();

class SlidingPuzzleGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    Rect tileRect(uint8_t index) const;
    void setupSolved(uint8_t size);
    void shuffle();
    bool solved() const;
    bool adjacentToGap(uint8_t index) const;
    void slide(uint8_t index);
    int8_t touchedTile(int16_t x, int16_t y) const;
    void loadBest(AppContext& host);
    /* Returns whether this solve beat the stored best, so the caller can
     * tell the player which of the two things happened. */
    bool saveBest(AppContext& host);

    uint8_t size_ = 2;
    uint8_t tiles_[9] = {};
    uint8_t gap_ = 3;
    uint16_t moves_ = 0;
    uint16_t bestMoves_ = 0;
    bool won_ = false;
};

