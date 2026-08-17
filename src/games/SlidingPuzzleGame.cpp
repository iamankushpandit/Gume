#include "SlidingPuzzleGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr int16_t HUD_Y = TOP_BAR_HEIGHT + 6;
constexpr int16_t PROMPT_Y = TOP_BAR_HEIGHT + 24;
constexpr int16_t BOARD_TOP = TOP_BAR_HEIGHT + 38;
constexpr int16_t BOARD_MAX_CELL = 76;

constexpr uint16_t TILE = 0x24BD;
constexpr uint16_t EMPTY = 0x1085;
constexpr uint16_t WIN = 0x37F0;

constexpr AppMetadata SLIDING_PUZZLE_METADATA = {
    "slide",
    "Slide",
    "Slide Puzzle",
    "number puzzle",
    "Slide",
    "Slide tiles into order.",
    nullptr,
    LauncherIcon::SlidingPuzzle,
    15,
    true,
};
}

const AppMetadata& slidingPuzzleAppMetadata() {
    return SLIDING_PUZZLE_METADATA;
}

const char* SlidingPuzzleGame::title() const {
    return slidingPuzzleAppMetadata().screenTitle != nullptr
        ? slidingPuzzleAppMetadata().screenTitle
        : slidingPuzzleAppMetadata().title;
}

void SlidingPuzzleGame::begin(AppContext& host) {
    size_ = 2;
    loadBest(host);
    setupSolved(size_);
    shuffle();
    markDirty();
}

void SlidingPuzzleGame::loadBest(AppContext& host) {
    bestMoves_ = static_cast<uint16_t>(host.getScore(size_ == 2 ? "slideB2" : "slideB3", 0));
}

void SlidingPuzzleGame::saveBest(AppContext& host) {
    if (host.saveBestScore(size_ == 2 ? "slideB2" : "slideB3", moves_, true)) {
        bestMoves_ = moves_;
    }
}

void SlidingPuzzleGame::setupSolved(uint8_t size) {
    size_ = size;
    const uint8_t total = size_ * size_;
    for (uint8_t i = 0; i < total - 1; ++i) {
        tiles_[i] = i + 1;
    }
    tiles_[total - 1] = 0;
    gap_ = total - 1;
    moves_ = 0;
    won_ = false;
}

bool SlidingPuzzleGame::adjacentToGap(uint8_t index) const {
    const int8_t row = index / size_;
    const int8_t col = index % size_;
    const int8_t gapRow = gap_ / size_;
    const int8_t gapCol = gap_ % size_;
    return abs(row - gapRow) + abs(col - gapCol) == 1;
}

void SlidingPuzzleGame::slide(uint8_t index) {
    tiles_[gap_] = tiles_[index];
    tiles_[index] = 0;
    gap_ = index;
}

void SlidingPuzzleGame::shuffle() {
    uint8_t lastGap = 255;
    const uint16_t steps = size_ == 2 ? 18 : 80;
    for (uint16_t step = 0; step < steps; ++step) {
        uint8_t candidates[4];
        uint8_t count = 0;
        for (uint8_t i = 0; i < size_ * size_; ++i) {
            if (i != lastGap && adjacentToGap(i)) {
                candidates[count++] = i;
            }
        }
        if (count == 0) {
            lastGap = 255;
            continue;
        }
        const uint8_t chosen = candidates[random(count)];
        lastGap = gap_;
        slide(chosen);
    }
    if (solved()) {
        shuffle();
        return;
    }
    moves_ = 0;
}

Rect SlidingPuzzleGame::boardRect(const Ui::Frame& f) const {
    /* Largest centred square between the prompt and the solved banner, then
     * rounded down to a whole number of cells so the tiles stay flush. */
    const Rect band{0, BOARD_TOP, f.w, static_cast<int16_t>(f.h - BOARD_TOP - 34)};
    const Rect square = Ui::squareIn(band, static_cast<int16_t>(BOARD_MAX_CELL * size_));
    const int16_t cell = static_cast<int16_t>(square.w / size_);
    const int16_t grid = static_cast<int16_t>(cell * size_);
    return Ui::centreIn(band, grid, grid);
}

Rect SlidingPuzzleGame::tileRect(const Ui::Frame& f, uint8_t index) const {
    const Rect board = boardRect(f);
    const int16_t cell = static_cast<int16_t>(board.w / size_);
    return Rect{static_cast<int16_t>(board.x + (index % size_) * cell),
                static_cast<int16_t>(board.y + (index / size_) * cell), cell, cell};
}

int8_t SlidingPuzzleGame::touchedTile(const Ui::Frame& f, int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < size_ * size_; ++i) {
        if (tiles_[i] != 0 && tileRect(f, i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

bool SlidingPuzzleGame::solved() const {
    const uint8_t total = size_ * size_;
    for (uint8_t i = 0; i < total - 1; ++i) {
        if (tiles_[i] != i + 1) {
            return false;
        }
    }
    return tiles_[total - 1] == 0;
}

void SlidingPuzzleGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (won_) {
        if (size_ == 2) {
            size_ = 3;
        }
        loadBest(host);
        setupSolved(size_);
        shuffle();
        markDirty();
        return;
    }

    const int8_t tile = touchedTile(Ui::frame(host.display()), touch.x, touch.y);
    if (tile < 0 || !adjacentToGap(tile)) {
        return;
    }
    slide(tile);
    ++moves_;
    if (solved()) {
        won_ = true;
        saveBest(host);
        host.beepOk();
    }
    markDirty();
}

void SlidingPuzzleGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    char movesBuf[24];
    snprintf(movesBuf, sizeof(movesBuf), "%ux%u  Moves %u", size_, size_, moves_);
    tft.drawString(movesBuf, 8, 36, 2);
    tft.setTextDatum(TR_DATUM);
    char bestBuf[24];
    if (bestMoves_ > 0) {
        snprintf(bestBuf, sizeof(bestBuf), "Best %u", bestMoves_);
    } else {
        snprintf(bestBuf, sizeof(bestBuf), "Best --");
    }
    tft.drawString(bestBuf, f.w - 8, HUD_Y, 2);
    Ui::drawLabel(tft, Rect{8, PROMPT_Y, static_cast<int16_t>(f.w - 16), 16},
                  "Slide tiles into order", Ui::muted(), 1, Align::Center);

    const uint8_t total = size_ * size_;
    for (uint8_t i = 0; i < total; ++i) {
        const Rect r = tileRect(f, i);
        if (tiles_[i] == 0) {
            tft.fillRoundRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, 6, EMPTY);
            continue;
        }
        char tileLabel[4];
        snprintf(tileLabel, sizeof(tileLabel), "%u", tiles_[i]);
        Ui::drawButton(tft, Rect{static_cast<int16_t>(r.x + 2), static_cast<int16_t>(r.y + 2), static_cast<int16_t>(r.w - 4), static_cast<int16_t>(r.h - 4)}, tileLabel, TILE, Ui::outline(), TFT_WHITE, false, 4);
    }

    if (won_) {
        tft.fillRoundRect(52, 184, 216, 42, 8, Ui::panel());
        tft.drawRoundRect(52, 184, 216, 42, 8, WIN);
        Ui::drawLabel(tft, Ui::centreIn(Rect{0, static_cast<int16_t>(f.h - 50), f.w, 28}, 212, 28),
                      size_ == 2 ? "Solved - tap for 3x3" : "Solved - tap again", WIN, 2, Align::Center);
    }
}
