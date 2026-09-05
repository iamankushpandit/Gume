#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& mazeAppMetadata();

class MazeGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. The maze walls are drawn once; a step repaints the two cells
     * the dot moved between. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    void chooseMaze();
    void reset();
    bool canMoveTo(int8_t col, int8_t row) const;
    void tryMove(AppContext& host, int8_t targetCol, int8_t targetRow);
    bool touchToCell(int16_t x, int16_t y, int8_t& col, int8_t& row) const;
    bool findCell(char target, int8_t& col, int8_t& row) const;
    bool isActiveMazeSolvable() const;
    void loadBestForLevel(AppContext& host);
    /* Returns whether this run beat the stored best for the level, so the
     * caller can sound the difference. */
    bool saveProgress(AppContext& host);
    void drawHud(Ui::Renderer& tft) const;

    /* What the HUD strip and the win panel currently show. drawHud() wipes a
     * full-width 18px band before it writes, so calling it on every repaint --
     * which the old partial branch did -- cost that band several times a second
     * for a line that changes once per step at most. */
    uint16_t drawnMoves_ = 0xFFFF;
    uint16_t drawnBest_ = 0xFFFF;
    uint8_t drawnLevel_ = 0xFF;
    bool drawnWon_ = false;
    void drawMazeCell(Ui::Renderer& tft, uint8_t col, uint8_t row) const;
    void drawMaze(Ui::Renderer& tft) const;
    void drawPlayer(Ui::Renderer& tft) const;

    const char* const* maze_ = nullptr;
    uint8_t levelIndex_ = 0;
    int8_t playerCol_ = 1;
    int8_t playerRow_ = 1;
    int8_t prevPlayerCol_ = 1;
    int8_t prevPlayerRow_ = 1;
    uint16_t moves_ = 0;
    uint16_t bestMoves_ = 0;
    bool won_ = false;
    bool playerMoved_ = false;
    uint32_t lastMoveAt_ = 0;
};
