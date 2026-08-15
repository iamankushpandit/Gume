#include "MazeGame.h"

namespace {
constexpr uint8_t MAZE_COLS = 12;
constexpr uint8_t MAZE_ROWS = 8;
constexpr int16_t CELL = 22;
constexpr int16_t MAZE_X = (SCREEN_WIDTH - MAZE_COLS * CELL) / 2;
constexpr int16_t MAZE_Y = TOP_BAR_HEIGHT + 28;
constexpr uint16_t WALL = 0x4A49;
constexpr uint16_t PATH = 0xFFFF;
constexpr uint16_t PLAYER = 0xE8E4;
constexpr uint16_t EXIT = 0x05D1;

const char* const MAZES[][MAZE_ROWS] = {
{
    "############",
    "#S.......E.#",
    "#..........#",
    "#..........#",
    "#..........#",
    "#..........#",
    "#..........#",
    "############",
},
{
    "############",
    "#S#.....#..#",
    "#.#.###.#.##",
    "#.#...#....#",
    "#.###.####.#",
    "#..........#",
    "##########E#",
    "############",
},
{
    "############",
    "#S...#.....#",
    "#.##.#.###.#",
    "#....#...#.#",
    "####.....#.#",
    "#....#...#E#",
    "#.######...#",
    "############",
},
{
    "############",
    "#S...#.....#",
    "#.#..#.###.#",
    "#.#..#...#.#",
    "#.#......#E#",
    "#.########.#",
    "#..........#",
    "############",
},
{
    "############",
    "#S...#.....#",
    "###.#.#.##.#",
    "#...#.#....#",
    "#.###.####.#",
    "#.....#....#",
    "#####.....E#",
    "############",
},
{
    "############",
    "#S.#.#.....#",
    "#.....#....#",
    "#.....#....#",
    "#...#..#.#.#",
    "#....#...#.#",
    "#.......#.E#",
    "############",
},
{
    "############",
    "#S...#.....#",
    "#...#.....##",
    "#...#..#...#",
    "#.#.....#..#",
    "#...#...##.#",
    "#..##...#.E#",
    "############",
},
{
    "############",
    "#S.#.......#",
    "#.#......#.#",
    "#.#..#.....#",
    "#..#....##.#",
    "##..#...#..#",
    "#......#..E#",
    "############",
},
{
    "############",
    "#S...##....#",
    "#...#...##.#",
    "#...#....#.#",
    "#........#.#",
    "###.##..#..#",
    "#....#.#..E#",
    "############",
},
{
    "############",
    "#S...#.....#",
    "#...#....#.#",
    "#...#...#..#",
    "#..#....##.#",
    "#.....##.#.#",
    "##......#.E#",
    "############",
},
{
    "############",
    "#S#........#",
    "#.##....#..#",
    "#.##...#...#",
    "#..#..##...#",
    "#....#.....#",
    "#...#.....E#",
    "############",
},
{
    "############",
    "#S.#..#...##",
    "#.####..#.##",
    "#.##....#..#",
    "#.##.###.#.#",
    "#.#..#.#...#",
    "#...#.....E#",
    "############",
},
{
    "############",
    "#S.....#...#",
    "#####..#.#.#",
    "#......#.#.#",
    "#.######.#.#",
    "#........#E#",
    "########...#",
    "############",
},
{
    "############",
    "#S.##......#",
    "#...#..##..#",
    "#....#..#..#",
    "#..#..#.#..#",
    "#..#....#..#",
    "#.....#.#.E#",
    "############",
},
{
    "############",
    "#S.#.......#",
    "#..#.#.....#",
    "##.#....##.#",
    "#..#.#.#...#",
    "#.#...##.#.#",
    "#...#...#.E#",
    "############",
},
{
    "############",
    "#S..#.....##",
    "#.##..#....#",
    "#..##..##..#",
    "##.#.#.#.#.#",
    "##...#.##..#",
    "#.......#.E#",
    "############",
},
{
    "############",
    "#S...#...#.#",
    "#....#.#...#",
    "#...#...#..#",
    "#...###..#.#",
    "#....#..##.#",
    "##.#...##.E#",
    "############",
},
{
    "############",
    "#S..#....###",
    "#..#..#.#..#",
    "##.#.##...##",
    "##.#..#.#..#",
    "##..#.#.#..#",
    "#.....#...E#",
    "############",
},
{
    "############",
    "#S###......#",
    "#...#...##.#",
    "#..#.#.##..#",
    "#..###..#..#",
    "#...#.#..#.#",
    "##......#.E#",
    "############",
},
{
    "############",
    "#S.#.......#",
    "#.##..###..#",
    "#.#.#...#..#",
    "#...##...#.#",
    "#.#...#..#.#",
    "#..#....#.E#",
    "############",
},
{
    "############",
    "#S.#.......#",
    "##.#.##..#.#",
    "##.#..###..#",
    "#..#....#..#",
    "##..###.##.#",
    "#.......#.E#",
    "############",
},
{
    "############",
    "#S..#.....##",
    "#.#.#.###..#",
    "#..#..##..##",
    "#..#.#.#.#.#",
    "#.#..###...#",
    "#...##.##.E#",
    "############",
},
{
    "############",
    "#S.#..#...##",
    "##.#.#..#..#",
    "#..##..#...#",
    "#.##..#...##",
    "#..#.#..#..#",
    "##...#....E#",
    "############",
},
{
    "############",
    "#S.##..#...#",
    "#...##...#.#",
    "##...##.#..#",
    "#.#.###.#.##",
    "#...#...#..#",
    "##....###.E#",
    "############",
},
{
    "############",
    "#S#.#......#",
    "#.###....#.#",
    "#..#...##..#",
    "#..#..#...##",
    "##.#...#.#.#",
    "##.....#..E#",
    "############",
},
{
    "############",
    "#S.#......##",
    "#.#..####..#",
    "#..#..##.#.#",
    "#..#...##..#",
    "#..###.##..#",
    "#.......#.E#",
    "############",
},
{
    "############",
    "#S#.#......#",
    "#.#......#.#",
    "#.#.######.#",
    "#.#...##...#",
    "#..##...#..#",
    "##....###.E#",
    "############",
},
{
    "############",
    "#S.#.......#",
    "#..#.#..#..#",
    "#.#...#.#..#",
    "#.###.#....#",
    "#.#.#.####.#",
    "#.....#...E#",
    "############",
},
{
    "############",
    "#S.#...#.###",
    "#..#.#..#.##",
    "##.#..#.#..#",
    "#..#.#...###",
    "#.##.###.#.#",
    "#......#..E#",
    "############",
},
{
    "############",
    "#S..##.....#",
    "#.###..#..##",
    "#.###.#....#",
    "#..##..#.#.#",
    "#.####.#...#",
    "#......###E#",
    "############",
},
};

constexpr uint8_t MAZE_COUNT = sizeof(MAZES) / sizeof(MAZES[0]);

const char* const FALLBACK_MAZE[MAZE_ROWS] = {
    "############",
    "#S.........#",
    "#.########.#",
    "#.#......#.#",
    "#.#.####.#.#",
    "#.#....#.#E#",
    "#.####...#.#",
    "############",
};
}

const char* MazeGame::title() const {
    return "Maze";
}

void MazeGame::begin(AppContext& host) {
    levelIndex_ = static_cast<uint8_t>(min<uint32_t>(host.getScore("mazeLevel", 0), MAZE_COUNT - 1));
    loadBestForLevel(host);
    chooseMaze();
    reset();
    markDirty();
}

void MazeGame::chooseMaze() {
    maze_ = MAZES[levelIndex_];
    if (!isActiveMazeSolvable()) {
        maze_ = FALLBACK_MAZE;
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

    playerCol_ = nextCol;
    playerRow_ = nextRow;
    ++moves_;
    lastMoveAt_ = millis();
    if (maze_[playerRow_][playerCol_] == 'E') {
        won_ = true;
        saveProgress(host);
        if (levelIndex_ + 1 < MAZE_COUNT) {
            host.setScore("mazeLevel", levelIndex_ + 1);
        }
        host.beepOk();
    }
    markDirty();
}

void MazeGame::update(AppContext& host, const TouchPoint& touch) {
    if (won_ && touch.justPressed) {
        if (levelIndex_ + 1 < MAZE_COUNT) {
            ++levelIndex_;
            host.setScore("mazeLevel", levelIndex_);
            loadBestForLevel(host);
            chooseMaze();
        }
        reset();
        markDirty();
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

void MazeGame::render(AppContext& host) {
    TFT_eSPI& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    Ui::drawLabel(tft, Rect{12, 32, 296, 16}, "Drag the red dot to the green exit", Ui::text(), 2, Align::Center);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(bestMoves_ > 0 ? String("Level ") + (levelIndex_ + 1) + "/" + MAZE_COUNT + "  Moves " + moves_ + "  Best " + bestMoves_ : String("Level ") + (levelIndex_ + 1) + "/" + MAZE_COUNT + "  Moves " + moves_, SCREEN_WIDTH / 2, 50, 1);

    for (uint8_t row = 0; row < MAZE_ROWS; ++row) {
        for (uint8_t col = 0; col < MAZE_COLS; ++col) {
            const int16_t x = MAZE_X + col * CELL;
            const int16_t y = MAZE_Y + row * CELL;
            const char cell = maze_[row][col];
            uint16_t fill = PATH;
            if (cell == '#') {
                fill = WALL;
            } else if (cell == 'E') {
                fill = EXIT;
            } else if (cell == 'S') {
                fill = PATH;
            }
            tft.fillRect(x, y, CELL, CELL, fill);
            tft.drawRect(x, y, CELL, CELL, Ui::rgb(210, 210, 210));
        }
    }

    const int16_t px = MAZE_X + playerCol_ * CELL + CELL / 2;
    const int16_t py = MAZE_Y + playerRow_ * CELL + CELL / 2;
    tft.fillCircle(px, py, 8, PLAYER);
    tft.drawCircle(px, py, 8, TFT_BLACK);

    if (won_) {
        tft.fillRoundRect(58, 86, 204, 58, 8, Ui::panel());
        tft.drawRoundRect(58, 86, 204, 58, 8, Ui::success());
        tft.setTextColor(Ui::success(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Maze solved!", SCREEN_WIDTH / 2, 106, 4);
        tft.drawString(levelIndex_ + 1 < MAZE_COUNT ? "Tap for next maze" : "Tap to play again", SCREEN_WIDTH / 2, 132, 2);
        tft.setTextDatum(TL_DATUM);
    }
}
