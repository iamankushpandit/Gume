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
    /* Two-phase. Placing a mark changes one cell of nine; the board panel and
     * its grid bars are position-indexed constants. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

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

    /* What is on the panel. A cell is repainted only when its mark changes,
     * which on this screen means exactly one cell per move. */
    char drawnCells_[9] = {};
    /* A fixed buffer, not a String. message_ is one already -- that is the
     * screen's own long-standing debt -- but a SECOND one rewritten on every
     * message change is heap churn the memory rule exists to refuse, and
     * check_frame_rules.py caught it as soon as it was written. */
    char drawnMessage_[16] = {0};
    uint32_t drawnX_ = 0xFFFFFFFF;
    uint32_t drawnO_ = 0xFFFFFFFF;
    uint32_t drawnD_ = 0xFFFFFFFF;
    bool drawnOver_ = false;
};
