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
    void saveProgress(AppContext& host);

    const char* const* maze_ = nullptr;
    uint8_t levelIndex_ = 0;
    int8_t playerCol_ = 1;
    int8_t playerRow_ = 1;
    uint16_t moves_ = 0;
    uint16_t bestMoves_ = 0;
    bool won_ = false;
    uint32_t lastMoveAt_ = 0;
};
