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
    /* Two-phase. A slide swaps a tile with the gap, so exactly two of the
     * four, nine or sixteen change. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

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

    /* What is on the panel. Note drawnSize_ as well as the tiles: this is the
     * one screen whose board geometry is not constant for the life of the
     * screen -- solving the 2x2 promotes it to 3x3 -- so a size change has to
     * be a full repaint rather than a tile-by-tile one. */
    uint8_t drawnTiles_[9] = {};
    uint8_t drawnSize_ = 0;
    uint16_t drawnMoves_ = 0xFFFF;
    uint16_t drawnBest_ = 0xFFFF;
    bool drawnWon_ = false;

    void drawTile(Ui::Renderer& tft, uint8_t index);
};

