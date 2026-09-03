#include "TicTacToeGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr Rect RESET_BUTTON{8, 34, 74, 28};
constexpr Rect BOARD_RECT{76, 68, 168, 168};
constexpr uint16_t BLUE = 0x049F;
constexpr uint16_t RED = 0xD8A7;
constexpr uint16_t YELLOW = 0xFEC0;

constexpr AppMetadata TIC_TAC_TOE_METADATA = {
    "tictactoe",
    "Tic-Tac-Toe",
    nullptr,
    "classic grid",
    "Tic-Tac-Toe",
    "Play Xs and Os on a 3x3 board.",
    nullptr,
    LauncherIcon::TicTacToe,
    0,
    true,
};
}

const AppMetadata& ticTacToeAppMetadata() {
    return TIC_TAC_TOE_METADATA;
}

const char* TicTacToeGame::title() const {
    return ticTacToeAppMetadata().title;
}

void TicTacToeGame::begin(AppContext& host) {
    loadScores(host);
    resetBoard();
    markDirty();
}

void TicTacToeGame::loadScores(AppContext& host) {
    xWins_ = host.getScore("tttX", 0);
    oWins_ = host.getScore("tttO", 0);
    draws_ = host.getScore("tttDraw", 0);
}

void TicTacToeGame::resetBoard() {
    for (char& cell : cells_) {
        cell = ' ';
    }
    turn_ = 'X';
    gameOver_ = false;
    message_ = "X turn";
}

char TicTacToeGame::winner() const {
    static constexpr uint8_t lines[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
        {0, 4, 8}, {2, 4, 6},
    };
    for (const auto& line : lines) {
        const char a = cells_[line[0]];
        if (a != ' ' && a == cells_[line[1]] && a == cells_[line[2]]) {
            return a;
        }
    }
    return ' ';
}

bool TicTacToeGame::boardFull() const {
    for (const char cell : cells_) {
        if (cell == ' ') {
            return false;
        }
    }
    return true;
}

void TicTacToeGame::handleCell(AppContext& host, uint8_t cell) {
    if (cell >= 9 || cells_[cell] != ' ') {
        return;
    }
    if (gameOver_) {
        resetBoard();
        return;
    }

    cells_[cell] = turn_;
    char win = winner();
    if (win != ' ') {
        message_ = String(win) + " wins";
        if (win == 'X') {
            host.setScore("tttX", ++xWins_);
        } else {
            host.setScore("tttO", ++oWins_);
        }
        gameOver_ = true;
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(Sound::Victory);
        markDirty();
        return;
    }
    if (boardFull()) {
        message_ = "Draw game";
        host.setScore("tttDraw", ++draws_);
        gameOver_ = true;
        /* Both players are human here, so there is no losing side to console
         * -- but a full board with no winner is still the game ending without
         * anyone getting what they came for, and the falling cue is the only
         * one that says so. */
        host.playSound(Sound::GameOver);
        markDirty();
        return;
    }

    turn_ = turn_ == 'X' ? 'O' : 'X';
    message_ = String(turn_) + " turn";
    /* A mark going down. Nine of these a game, alternating between two
     * players, so it is the click and nothing more. */
    host.playSound(Sound::Tap);
    markDirty();
}

void TicTacToeGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    if (RESET_BUTTON.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        resetBoard();
        host.playSound(Sound::Select);
        markDirty();
        return;
    }
    if (!BOARD_RECT.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        return;
    }
    const int16_t cellSize = BOARD_RECT.w / 3;
    const uint8_t col = constrain((touch.x - BOARD_RECT.x) / cellSize, 0, 2);
    const uint8_t row = constrain((touch.y - BOARD_RECT.y) / cellSize, 0, 2);
    handleCell(host, static_cast<uint8_t>(row * 3 + col));
}

void TicTacToeGame::drawMark(Ui::Renderer& tft, uint8_t cell, char mark) {
    if (mark == ' ') {
        return;
    }
    const int16_t cellSize = BOARD_RECT.w / 3;
    const int16_t x = BOARD_RECT.x + (cell % 3) * cellSize;
    const int16_t y = BOARD_RECT.y + (cell / 3) * cellSize;
    const uint16_t color = mark == 'X' ? RED : BLUE;
    if (mark == 'X') {
        tft.drawLine(x + 14, y + 14, x + cellSize - 14, y + cellSize - 14, color);
        tft.drawLine(x + 15, y + 14, x + cellSize - 13, y + cellSize - 14, color);
        tft.drawLine(x + cellSize - 14, y + 14, x + 14, y + cellSize - 14, color);
        tft.drawLine(x + cellSize - 15, y + 14, x + 13, y + cellSize - 14, color);
    } else {
        tft.drawCircle(x + cellSize / 2, y + cellSize / 2, cellSize / 2 - 13, color);
        tft.drawCircle(x + cellSize / 2, y + cellSize / 2, cellSize / 2 - 12, color);
    }
}

void TicTacToeGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    Ui::drawButton(tft, RESET_BUTTON, "Reset", YELLOW, Ui::outline(), TFT_BLACK);
    Ui::drawLabel(tft, Rect{94, 36, 72, 24}, message_, Ui::text(), 2, Align::Center);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TR_DATUM);
    char scoreBuf[32];
    snprintf(scoreBuf, sizeof(scoreBuf), "X %u  O %u  D %u", xWins_, oWins_, draws_);
    tft.drawString(scoreBuf, SCREEN_WIDTH - 8, 40, 1);
    tft.setTextDatum(TL_DATUM);

    tft.fillRoundRect(BOARD_RECT.x - 4, BOARD_RECT.y - 4, BOARD_RECT.w + 8, BOARD_RECT.h + 8, 8, Ui::panel());
    const int16_t cellSize = BOARD_RECT.w / 3;
    for (uint8_t i = 1; i < 3; ++i) {
        tft.fillRect(BOARD_RECT.x + i * cellSize - 2, BOARD_RECT.y, 4, BOARD_RECT.h, TFT_DARKGREY);
        tft.fillRect(BOARD_RECT.x, BOARD_RECT.y + i * cellSize - 2, BOARD_RECT.w, 4, TFT_DARKGREY);
    }
    for (uint8_t i = 0; i < 9; ++i) {
        drawMark(tft, i, cells_[i]);
    }
    if (gameOver_) {
        tft.drawRoundRect(BOARD_RECT.x - 4, BOARD_RECT.y - 4, BOARD_RECT.w + 8, BOARD_RECT.h + 8, 8, RED);
    }
}
