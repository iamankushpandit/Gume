#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& ticTacToeAppMetadata();

class TicTacToeGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    void resetBoard();
    char winner() const;
    bool boardFull() const;
    void handleCell(AppContext& host, uint8_t cell);
    void drawMark(Ui::Renderer& tft, uint8_t cell, char mark);
    void loadScores(AppContext& host);

    char cells_[9] = {};
    char turn_ = 'X';
    bool gameOver_ = false;
    String message_;
    uint32_t xWins_ = 0;
    uint32_t oWins_ = 0;
    uint32_t draws_ = 0;
};
