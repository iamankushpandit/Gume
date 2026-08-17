#include "TicTacToeGame.h"
#include "engine/AppRegistry.h"

namespace {
/* The header band is a fixed height; everything below it is measured off the
 * live panel so the board stays square in either orientation. SCORE_W is the
 * width the "X 0  O 0  D 0" line needs at font 1, reserved so the message
 * never runs under it on a 240px-wide portrait panel. */
constexpr int16_t HEADER_TOP = TOP_BAR_HEIGHT + 4;
constexpr int16_t HEADER_H = 28;
constexpr int16_t RESET_W = 74;
constexpr int16_t SCORE_W = 78;
constexpr int16_t BOARD_MAX = 224;
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

Rect TicTacToeGame::resetRect(const Ui::Frame&) const {
    return Rect{8, HEADER_TOP, RESET_W, HEADER_H};
}

Rect TicTacToeGame::messageRect(const Ui::Frame& f) const {
    const int16_t left = 8 + RESET_W + 6;
    const int16_t right = static_cast<int16_t>(f.w - 8 - SCORE_W);
    return Rect{left, static_cast<int16_t>(HEADER_TOP + 2),
                static_cast<int16_t>(right - left), 24};
}

Rect TicTacToeGame::boardRect(const Ui::Frame& f) const {
    /* Whatever is left below the header, squared off and centred. Landscape
     * leaves 172px of height against 320 of width, so this reproduces the
     * 168x168 board the game was authored with; portrait leaves 248 against
     * 240, so it grows to the cap instead of running to the panel edges. */
    const int16_t top = HEADER_TOP + HEADER_H + 6;
    const Rect band{0, top, f.w, static_cast<int16_t>(f.h - top - 4)};
    return Ui::squareIn(band, BOARD_MAX);
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
        host.beepOk();
        markDirty();
        return;
    }
    if (boardFull()) {
        message_ = "Draw game";
        host.setScore("tttDraw", ++draws_);
        gameOver_ = true;
        host.beepOk();
        markDirty();
        return;
    }

    turn_ = turn_ == 'X' ? 'O' : 'X';
    message_ = String(turn_) + " turn";
    markDirty();
}

void TicTacToeGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    const Ui::Frame f = Ui::frame(host.display());
    if (resetRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        resetBoard();
        host.beepOk();
        markDirty();
        return;
    }
    const Rect board = boardRect(f);
    if (!board.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        return;
    }
    const int16_t cellSize = board.w / 3;
    const uint8_t col = constrain((touch.x - board.x) / cellSize, 0, 2);
    const uint8_t row = constrain((touch.y - board.y) / cellSize, 0, 2);
    handleCell(host, static_cast<uint8_t>(row * 3 + col));
}

void TicTacToeGame::drawMark(Ui::Renderer& tft, const Rect& board, uint8_t cell, char mark) {
    if (mark == ' ') {
        return;
    }
    const int16_t cellSize = board.w / 3;
    const int16_t x = static_cast<int16_t>(board.x + (cell % 3) * cellSize);
    const int16_t y = static_cast<int16_t>(board.y + (cell / 3) * cellSize);
    /* The inset used to be a flat 14px, which is a quarter of a 56px landscape
     * cell but only a fifth of a 74px portrait one -- so it scales with the
     * cell, or the marks look progressively lost as the board grows. */
    const int16_t inset = static_cast<int16_t>(cellSize / 4);
    const uint16_t color = mark == 'X' ? RED : BLUE;
    if (mark == 'X') {
        tft.drawLine(x + inset, y + inset, x + cellSize - inset, y + cellSize - inset, color);
        tft.drawLine(x + inset + 1, y + inset, x + cellSize - inset + 1, y + cellSize - inset, color);
        tft.drawLine(x + cellSize - inset, y + inset, x + inset, y + cellSize - inset, color);
        tft.drawLine(x + cellSize - inset - 1, y + inset, x + inset - 1, y + cellSize - inset, color);
    } else {
        const int16_t radius = static_cast<int16_t>(cellSize / 2 - inset + 1);
        tft.drawCircle(x + cellSize / 2, y + cellSize / 2, radius, color);
        tft.drawCircle(x + cellSize / 2, y + cellSize / 2, radius + 1, color);
    }
}

void TicTacToeGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());
    Ui::drawButton(tft, resetRect(f), "Reset", YELLOW, Ui::outline(), TFT_BLACK);
    Ui::drawLabel(tft, messageRect(f), message_, Ui::text(), 2, Align::Center);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TR_DATUM);
    char scoreBuf[32];
    snprintf(scoreBuf, sizeof(scoreBuf), "X %u  O %u  D %u", xWins_, oWins_, draws_);
    tft.drawString(scoreBuf, f.w - 8, HEADER_TOP + 6, 1);
    tft.setTextDatum(TL_DATUM);

    const Rect board = boardRect(f);
    tft.fillRoundRect(board.x - 4, board.y - 4, board.w + 8, board.h + 8, 8, Ui::panel());
    const int16_t cellSize = board.w / 3;
    for (uint8_t i = 1; i < 3; ++i) {
        tft.fillRect(board.x + i * cellSize - 2, board.y, 4, board.h, TFT_DARKGREY);
        tft.fillRect(board.x, board.y + i * cellSize - 2, board.w, 4, TFT_DARKGREY);
    }
    for (uint8_t i = 0; i < 9; ++i) {
        drawMark(tft, board, i, cells_[i]);
    }
    if (gameOver_) {
        tft.drawRoundRect(board.x - 4, board.y - 4, board.w + 8, board.h + 8, 8, RED);
    }
}
