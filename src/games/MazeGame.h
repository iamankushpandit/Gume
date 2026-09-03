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
    void render(AppContext& host) override;

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
