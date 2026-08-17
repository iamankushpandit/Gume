#include "MazeGame.h"
#include "MazeData.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint8_t MAZE_COLS = MazeData::COLS;
constexpr uint8_t MAZE_ROWS = MazeData::ROWS;
constexpr int16_t CELL_MAX = 22;
constexpr int16_t MAZE_Y = TOP_BAR_HEIGHT + 28;

/* 12 columns of 22px is 264px wide -- fine on a 320px panel and 24px too
 * wide for a 240px one, so the cell shrinks to 19 in portrait rather than
 * running the maze off both edges. Kept square: a maze with rectangular
 * cells is much harder to read a path through. */
int16_t mazeCell(const Ui::Frame& f) {
    int16_t cell = static_cast<int16_t>((f.w - 8) / MAZE_COLS);
    const int16_t byHeight = static_cast<int16_t>((f.h - MAZE_Y - 6) / MAZE_ROWS);
    if (byHeight < cell) {
        cell = byHeight;
    }
    return cell > CELL_MAX ? CELL_MAX : cell;
}

int16_t mazeOriginX(const Ui::Frame& f) {
    return static_cast<int16_t>((f.w - MAZE_COLS * mazeCell(f)) / 2);
}
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

void MazeGame::saveProgress(AppContext& host) {
    char key[12];
    snprintf(key, sizeof(key), "mazeB%u", levelIndex_);
    if (host.saveBestScore(key, moves_, true)) {
        bestMoves_ = moves_;
    }
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

bool MazeGame::touchToCell(const Ui::Frame& f, int16_t x, int16_t y, int8_t& col, int8_t& row) const {
    const int16_t cell = mazeCell(f);
    const int16_t originX = mazeOriginX(f);
    if (x < originX || y < MAZE_Y ||
        x >= originX + MAZE_COLS * cell || y >= MAZE_Y + MAZE_ROWS * cell) {
        return false;
    }
    col = static_cast<int8_t>((x - originX) / cell);
    row = static_cast<int8_t>((y - MAZE_Y) / cell);
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
        saveProgress(host);
        if (levelIndex_ + 1 < MazeData::COUNT) {
            host.setScore(mazeAppMetadata().score->bestKey, levelIndex_ + 1);
        }
        host.beepOk();
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
    if (touchToCell(Ui::frame(host.display()), touch.x, touch.y, col, row)) {
        tryMove(host, col, row);
    }
}

void MazeGame::drawHud(Ui::Renderer& tft, const Ui::Frame& f) const {
    tft.fillRect(0, TOP_BAR_HEIGHT + 12, f.w, 18, Ui::bg());
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
    tft.drawString(hud, f.cx(), TOP_BAR_HEIGHT + 20, 1);
}

void MazeGame::drawMazeCell(Ui::Renderer& tft, const Ui::Frame& f, uint8_t col, uint8_t row) const {
    const int16_t cellPx = mazeCell(f);
    const int16_t x = static_cast<int16_t>(mazeOriginX(f) + col * cellPx);
    const int16_t y = static_cast<int16_t>(MAZE_Y + row * cellPx);
    const char cell = maze_[row][col];
    uint16_t fill = PATH;
    if (cell == '#') {
        fill = WALL;
    } else if (cell == 'E') {
        fill = EXIT;
    }
    tft.fillRect(x, y, cellPx, cellPx, fill);
    tft.drawRect(x, y, cellPx, cellPx, Ui::rgb(210, 210, 210));
}

void MazeGame::drawMaze(Ui::Renderer& tft, const Ui::Frame& f) const {
    for (uint8_t row = 0; row < MAZE_ROWS; ++row) {
        for (uint8_t col = 0; col < MAZE_COLS; ++col) {
            drawMazeCell(tft, f, col, row);
        }
    }
}

void MazeGame::drawPlayer(Ui::Renderer& tft, const Ui::Frame& f) const {
    const int16_t cell = mazeCell(f);
    const int16_t px = static_cast<int16_t>(mazeOriginX(f) + playerCol_ * cell + cell / 2);
    const int16_t py = static_cast<int16_t>(MAZE_Y + playerRow_ * cell + cell / 2);
    /* The dot has to stay inside a cell that is 22px in landscape and 19 in
     * portrait, so its radius follows rather than sitting at a flat 8. */
    const int16_t r = static_cast<int16_t>(cell * 8 / 22);
    tft.fillCircle(px, py, r, PLAYER);
    tft.drawCircle(px, py, r, TFT_BLACK);
}

void MazeGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());
        Ui::drawLabel(tft, Rect{12, TOP_BAR_HEIGHT + 2, static_cast<int16_t>(f.w - 24), 16},
                      "Drag the red dot to the green exit", Ui::text(), 2, Align::Center);
        drawHud(tft, f);
        drawMaze(tft, f);
    } else {
        drawHud(tft, f);
        if (playerMoved_) {
            drawMazeCell(tft, f, static_cast<uint8_t>(prevPlayerCol_), static_cast<uint8_t>(prevPlayerRow_));
            drawMazeCell(tft, f, static_cast<uint8_t>(playerCol_), static_cast<uint8_t>(playerRow_));
        }
    }

    drawPlayer(tft, f);
    playerMoved_ = false;

    if (won_) {
        const Rect won = Ui::centreIn(Rect{0, 0, f.w, f.h}, 204, 58);
        tft.fillRoundRect(won.x, won.y, won.w, won.h, 8, Ui::panel());
        tft.drawRoundRect(won.x, won.y, won.w, won.h, 8, Ui::success());
        tft.setTextColor(Ui::success(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Maze solved!", f.cx(), won.y + 20, 4);
        tft.drawString(levelIndex_ + 1 < MazeData::COUNT ? "Tap for next maze" : "Tap to play again",
                       f.cx(), won.y + 46, 2);
        tft.setTextDatum(TL_DATUM);
    }
}
