#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
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
    /* Geometry is derived from the live panel, never from SCREEN_WIDTH: the
     * board is the largest centred square left under the header, so the same
     * three expressions give a 168px board in landscape and a 224px one in
     * portrait. */
    Rect resetRect(const Ui::Frame& f) const;
    Rect messageRect(const Ui::Frame& f) const;
    Rect boardRect(const Ui::Frame& f) const;

    void resetBoard();
    char winner() const;
    bool boardFull() const;
    void handleCell(AppContext& host, uint8_t cell);
    void drawMark(Ui::Renderer& tft, const Rect& board, uint8_t cell, char mark);
    void loadScores(AppContext& host);

    char cells_[9] = {};
    char turn_ = 'X';
    bool gameOver_ = false;
    String message_;
    uint32_t xWins_ = 0;
    uint32_t oWins_ = 0;
    uint32_t draws_ = 0;
};
