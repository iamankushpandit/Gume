#include "MazeGame.h"
#include "MazeData.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint8_t MAZE_COLS = MazeData::COLS;
constexpr uint8_t MAZE_ROWS = MazeData::ROWS;
constexpr int16_t CELL = 22;
constexpr int16_t MAZE_X = (GAME_CANVAS_WIDTH - MAZE_COLS * CELL) / 2;
constexpr int16_t MAZE_Y = TOP_BAR_HEIGHT + 28;
constexpr uint16_t WALL = 0x4A49;
constexpr uint16_t PATH = 0xFFFF;
constexpr uint16_t PLAYER = 0xE8E4;
constexpr uint16_t EXIT = 0x05D1;

constexpr AppScoreInfo MAZE_SCORE = {
    "maze", "Maze", "mazeLevel", "lvl", false
};

constexpr AppMetadata MAZE_METADATA = {
    "maze",
    "Maze",
    nullptr,
    "drag dot",
    "Maze",
    "Drag the dot to the exit.",
    &MAZE_SCORE,
    LauncherIcon::Maze,
    12,
    true,
};
}

const AppMetadata& mazeAppMetadata() {
    return MAZE_METADATA;
}

const char* MazeGame::title() const {
    return mazeAppMetadata().screenTitle != nullptr
        ? mazeAppMetadata().screenTitle
        : mazeAppMetadata().title;
}

void MazeGame::begin(AppContext& host) {
    levelIndex_ = static_cast<uint8_t>(min<uint32_t>(
        host.getScore(mazeAppMetadata().score->bestKey, 0), MazeData::COUNT - 1));
    loadBestForLevel(host);
    chooseMaze();
    reset();
    markFullDirty();
}

void MazeGame::chooseMaze() {
    maze_ = MazeData::MAZES[levelIndex_];
    if (!isActiveMazeSolvable()) {
        maze_ = MazeData::FALLBACK_MAZE;
    }
}

void MazeGame::loadBestForLevel(AppContext& host) {
    char key[12];
    snprintf(key, sizeof(key), "mazeB%u", levelIndex_);
    bestMoves_ = static_cast<uint16_t>(host.getScore(key, 0));
}

bool MazeGame::saveProgress(AppContext& host) {
    char key[12];
    snprintf(key, sizeof(key), "mazeB%u", levelIndex_);
    if (host.saveBestScore(key, moves_, true)) {
        bestMoves_ = moves_;
        return true;
    }
    return false;
}

void MazeGame::reset() {
    int8_t startCol = 1;
    int8_t startRow = 1;
    if (findCell('S', startCol, startRow)) {
        playerCol_ = startCol;
        playerRow_ = startRow;
    } else {
        playerCol_ = 1;
        playerRow_ = 1;
    }
    moves_ = 0;
    won_ = false;
    prevPlayerCol_ = playerCol_;
    prevPlayerRow_ = playerRow_;
    playerMoved_ = false;
    lastMoveAt_ = 0;
}

bool MazeGame::canMoveTo(int8_t col, int8_t row) const {
    if (col < 0 || row < 0 || col >= MAZE_COLS || row >= MAZE_ROWS) {
        return false;
    }
    return maze_ != nullptr && maze_[row][col] != '#';
}

bool MazeGame::findCell(char target, int8_t& col, int8_t& row) const {
    if (maze_ == nullptr) {
        return false;
    }
    for (int8_t y = 0; y < MAZE_ROWS; ++y) {
        for (int8_t x = 0; x < MAZE_COLS; ++x) {
            if (maze_[y][x] == target) {
                col = x;
                row = y;
                return true;
            }
        }
    }
    return false;
}

bool MazeGame::isActiveMazeSolvable() const {
    int8_t startCol = 0;
    int8_t startRow = 0;
    int8_t exitCol = 0;
    int8_t exitRow = 0;
    if (!findCell('S', startCol, startRow) || !findCell('E', exitCol, exitRow)) {
        return false;
    }

    bool visited[MAZE_ROWS][MAZE_COLS] = {};
    int8_t queueCol[MAZE_ROWS * MAZE_COLS] = {};
    int8_t queueRow[MAZE_ROWS * MAZE_COLS] = {};
    uint8_t head = 0;
    uint8_t tail = 0;
    queueCol[tail] = startCol;
    queueRow[tail] = startRow;
    ++tail;
    visited[startRow][startCol] = true;

    while (head < tail) {
        const int8_t col = queueCol[head];
        const int8_t row = queueRow[head];
        ++head;
        if (col == exitCol && row == exitRow) {
            return true;
        }
        static constexpr int8_t DC[4] = {1, -1, 0, 0};
        static constexpr int8_t DR[4] = {0, 0, 1, -1};
        for (uint8_t i = 0; i < 4; ++i) {
            const int8_t nextCol = col + DC[i];
            const int8_t nextRow = row + DR[i];
            if (nextCol < 0 || nextRow < 0 || nextCol >= MAZE_COLS || nextRow >= MAZE_ROWS) {
                continue;
            }
            if (visited[nextRow][nextCol] || maze_[nextRow][nextCol] == '#') {
                continue;
            }
            visited[nextRow][nextCol] = true;
            queueCol[tail] = nextCol;
            queueRow[tail] = nextRow;
            ++tail;
        }
    }
    return false;
}

bool MazeGame::touchToCell(int16_t x, int16_t y, int8_t& col, int8_t& row) const {
    if (x < MAZE_X || y < MAZE_Y || x >= MAZE_X + MAZE_COLS * CELL || y >= MAZE_Y + MAZE_ROWS * CELL) {
        return false;
    }
    col = static_cast<int8_t>((x - MAZE_X) / CELL);
    row = static_cast<int8_t>((y - MAZE_Y) / CELL);
    return true;
}

void MazeGame::tryMove(AppContext& host, int8_t targetCol, int8_t targetRow) {
    if (won_ || millis() - lastMoveAt_ < 90) {
        return;
    }

    int8_t nextCol = playerCol_;
    int8_t nextRow = playerRow_;
    const int8_t dx = targetCol - playerCol_;
    const int8_t dy = targetRow - playerRow_;
    if (abs(dx) > abs(dy)) {
        nextCol += dx > 0 ? 1 : -1;
    } else if (dy != 0) {
        nextRow += dy > 0 ? 1 : -1;
    }

    if (nextCol == playerCol_ && nextRow == playerRow_) {
        return;
    }

    if (!canMoveTo(nextCol, nextRow)) {
        host.beepError();
        lastMoveAt_ = millis();
        return;
    }

    prevPlayerCol_ = playerCol_;
    prevPlayerRow_ = playerRow_;
    playerCol_ = nextCol;
    playerRow_ = nextRow;
    playerMoved_ = true;
    ++moves_;
    lastMoveAt_ = millis();
    if (maze_[playerRow_][playerCol_] == 'E') {
        won_ = true;
        /* HighScore when the level was walked in fewer steps than ever
         * before, Victory when it was merely finished. The pulse is
         * beepOk()'s, kept because on a codec-less board it is all there is. */
        const bool best = saveProgress(host);
        if (levelIndex_ + 1 < MazeData::COUNT) {
            host.setScore(mazeAppMetadata().score->bestKey, levelIndex_ + 1);
        }
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(best ? Sound::HighScore : Sound::Victory);
    } else {
        /* Every step. Tap rather than Whoosh: a step is one cell, the moves
         * come in quick succession while a finger is dragged, and anything
         * longer than a click starts overlapping itself. */
        host.playSound(Sound::Tap);
    }
    markDirty();
}

void MazeGame::update(AppContext& host, const TouchPoint& touch) {
    if (won_ && touch.justPressed) {
        if (levelIndex_ + 1 < MazeData::COUNT) {
            ++levelIndex_;
            host.setScore(mazeAppMetadata().score->bestKey, levelIndex_);
            loadBestForLevel(host);
            chooseMaze();
        }
        reset();
        markFullDirty();
        return;
    }
    if (!touch.down) {
        return;
    }
    int8_t col = 0;
    int8_t row = 0;
    if (touchToCell(touch.x, touch.y, col, row)) {
        tryMove(host, col, row);
    }
}

void MazeGame::drawHud(Ui::Renderer& tft) const {
    tft.fillRect(0, 42, GAME_CANVAS_WIDTH, 18, Ui::bg());
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    char hud[48];
    if (bestMoves_ > 0) {
        snprintf(hud, sizeof(hud), "Level %u/%u  Moves %u  Best %u",
                 static_cast<unsigned>(levelIndex_ + 1), static_cast<unsigned>(MazeData::COUNT),
                 static_cast<unsigned>(moves_), static_cast<unsigned>(bestMoves_));
    } else {
        snprintf(hud, sizeof(hud), "Level %u/%u  Moves %u",
                 static_cast<unsigned>(levelIndex_ + 1), static_cast<unsigned>(MazeData::COUNT),
                 static_cast<unsigned>(moves_));
    }
    tft.drawString(hud, GAME_CANVAS_WIDTH / 2, 50, 1);
}

void MazeGame::drawMazeCell(Ui::Renderer& tft, uint8_t col, uint8_t row) const {
    const int16_t x = static_cast<int16_t>(MAZE_X + col * CELL);
    const int16_t y = static_cast<int16_t>(MAZE_Y + row * CELL);
    const char cell = maze_[row][col];
    uint16_t fill = PATH;
    if (cell == '#') {
        fill = WALL;
    } else if (cell == 'E') {
        fill = EXIT;
    }
    tft.fillRect(x, y, CELL, CELL, fill);
    tft.drawRect(x, y, CELL, CELL, Ui::rgb(210, 210, 210));
}

void MazeGame::drawMaze(Ui::Renderer& tft) const {
    for (uint8_t row = 0; row < MAZE_ROWS; ++row) {
        for (uint8_t col = 0; col < MAZE_COLS; ++col) {
            drawMazeCell(tft, col, row);
        }
    }
}

void MazeGame::drawPlayer(Ui::Renderer& tft) const {
    const int16_t px = MAZE_X + playerCol_ * CELL + CELL / 2;
    const int16_t py = MAZE_Y + playerRow_ * CELL + CELL / 2;
    tft.fillCircle(px, py, 8, PLAYER);
    tft.drawCircle(px, py, 8, TFT_BLACK);
}

void MazeGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    Ui::drawLabel(tft, Rect{12, 32, 296, 16}, "Drag the red dot to the green exit",
                  Ui::text(), 2, Align::Center);
    drawMaze(tft);

    /* The panel has been wiped, so nothing on it is what we last drew. */
    drawnMoves_ = 0xFFFF;
    drawnBest_ = 0xFFFF;
    drawnLevel_ = 0xFF;
    drawnWon_ = false;
}

void MazeGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    /* Only when the line actually changed. drawHud() opens by wiping a
     * full-width 18px band, and the old code called it on every repaint --
     * including the ones driven by the drag timer, where nothing in it had
     * moved. */
    if (moves_ != drawnMoves_ || bestMoves_ != drawnBest_ || levelIndex_ != drawnLevel_) {
        drawHud(tft);
        drawnMoves_ = moves_;
        drawnBest_ = bestMoves_;
        drawnLevel_ = levelIndex_;
    }

    /* The dot's own erase: repaint the cell it left and the cell it arrived in,
     * then draw it at the new position. prevPlayerCol_/Row_ is the previous
     * position this needs -- the same idea as the screen saver tracking
     * ssav_textCy_, and it was already here. */
    if (playerMoved_) {
        drawMazeCell(tft, static_cast<uint8_t>(prevPlayerCol_), static_cast<uint8_t>(prevPlayerRow_));
        drawMazeCell(tft, static_cast<uint8_t>(playerCol_), static_cast<uint8_t>(playerRow_));
    }
    drawPlayer(tft);
    playerMoved_ = false;

    /* Painted once. It never needs erasing here: the only way off a solved
     * maze is a tap, which advances the level and asks for a full repaint. */
    if (won_ && !drawnWon_) {
        tft.fillRoundRect(58, 86, 204, 58, 8, Ui::panel());
        tft.drawRoundRect(58, 86, 204, 58, 8, Ui::success());
        tft.setTextColor(Ui::success(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Maze solved!", GAME_CANVAS_WIDTH / 2, 106, 4);
        tft.drawString(levelIndex_ + 1 < MazeData::COUNT ? "Tap for next maze" : "Tap to play again",
                       GAME_CANVAS_WIDTH / 2, 132, 2);
        tft.setTextDatum(TL_DATUM);
        drawnWon_ = true;
    }
}
