#include "SlidingPuzzleGame.h"
#include "engine/AppRegistry.h"

namespace {
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

bool SlidingPuzzleGame::saveBest(AppContext& host) {
    if (host.saveBestScore(size_ == 2 ? "slideB2" : "slideB3", moves_, true)) {
        bestMoves_ = moves_;
        return true;
    }
    return false;
}

void SlidingPuzzleGame::setupSolved(uint8_t size) {
    /* A layout change, and on this screen it can be a geometry change too:
     * solving the 2x2 promotes the board to 3x3, so the tile rects themselves
     * move. Nothing tile-by-tile can express that, and it lives here so no
     * caller can lay out a new board and forget. */
    markFullDirty();
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

Rect SlidingPuzzleGame::tileRect(uint8_t index) const {
    const int16_t cell = size_ == 2 ? 58 : 48;
    const int16_t grid = cell * size_;
    const int16_t startX = (GAME_CANVAS_WIDTH - grid) / 2;
    const int16_t startY = size_ == 2 ? 80 : 68;
    return Rect{static_cast<int16_t>(startX + (index % size_) * cell), static_cast<int16_t>(startY + (index / size_) * cell), cell, cell};
}

int8_t SlidingPuzzleGame::touchedTile(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < size_ * size_; ++i) {
        if (tiles_[i] != 0 && tileRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
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

    const int8_t tile = touchedTile(touch.x, touch.y);
    if (tile < 0 || !adjacentToGap(tile)) {
        return;
    }
    slide(tile);
    ++moves_;
    if (solved()) {
        won_ = true;
        /* Two different things can have just happened -- the puzzle was
         * solved, or it was solved in fewer moves than ever before -- and
         * until now they made the same noise. The green pulse belongs to
         * beepOk(), which this replaces, so it is kept by hand. */
        const bool best = saveBest(host);
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(best ? Sound::HighScore : Sound::Victory);
    } else {
        /* A tile sliding. This game used to make exactly one sound, once, at
         * the very end of a puzzle; the moves that make up the actual playing
         * of it were silent. */
        host.playSound(Sound::Whoosh);
    }
    markDirty();
}

/* One tile, gap or numbered. The whole tile rect is cleared first because the
 * two forms are not the same size -- the gap is inset 3 and a numbered tile is
 * inset 2 -- so drawing a gap over a tile without clearing would leave a
 * one-pixel ring of the old button around it. */
void SlidingPuzzleGame::drawTile(Ui::Renderer& tft, uint8_t index) {
    const Rect r = tileRect(index);
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    if (tiles_[index] == 0) {
        tft.fillRoundRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, 6, EMPTY);
        return;
    }
    char tileLabel[4];
    snprintf(tileLabel, sizeof(tileLabel), "%u", tiles_[index]);
    Ui::drawButton(tft, Rect{static_cast<int16_t>(r.x + 2), static_cast<int16_t>(r.y + 2),
                             static_cast<int16_t>(r.w - 4), static_cast<int16_t>(r.h - 4)},
                   tileLabel, TILE, Ui::outline(), TFT_WHITE, false, 4);
}

void SlidingPuzzleGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    Ui::drawLabel(tft, Rect{8, 54, 304, 16}, "Slide tiles into order", Ui::muted(), 1, Align::Center);

    for (uint8_t i = 0; i < 9; ++i) {
        drawnTiles_[i] = 0xFF;   // nothing painted there yet
    }
    drawnSize_ = size_;
    drawnMoves_ = 0xFFFF;
    drawnBest_ = 0xFFFF;
    drawnWon_ = false;
}

void SlidingPuzzleGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    const uint8_t total = size_ * size_;
    for (uint8_t i = 0; i < total; ++i) {
        if (tiles_[i] == drawnTiles_[i]) {
            continue;
        }
        drawTile(tft, i);
        drawnTiles_[i] = tiles_[i];
    }

    /* Moves resets to zero on a new board and Best only ever falls, so both can
     * get narrower; Best is TR_DATUM as well, so its stale characters would be
     * left at the LEFT end. Both rects are cleared before either is written. */
    if (moves_ != drawnMoves_ || bestMoves_ != drawnBest_) {
        tft.fillRect(8, 34, 160, 20, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 8 - 140, 34, 140, 20, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        char buf[24];
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "%ux%u  Moves %u", size_, size_, moves_);
        tft.drawString(buf, 8, 36, 2);
        tft.setTextDatum(TR_DATUM);
        if (bestMoves_ > 0) {
            snprintf(buf, sizeof(buf), "Best %u", bestMoves_);
        } else {
            snprintf(buf, sizeof(buf), "Best --");
        }
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 36, 2);
        tft.setTextDatum(TL_DATUM);
        drawnMoves_ = moves_;
        drawnBest_ = bestMoves_;
    }

    /* Painted once. Leaving a solved board goes through setupSolved(), which
     * asks for a full repaint -- and has to, because it may also change the
     * board's size. */
    if (won_ && !drawnWon_) {
        tft.fillRoundRect(52, 184, 216, 42, 8, Ui::panel());
        tft.drawRoundRect(52, 184, 216, 42, 8, WIN);
        Ui::drawLabel(tft, Rect{54, 190, 212, 28},
                      size_ == 2 ? "Solved - tap for 3x3" : "Solved - tap again",
                      WIN, 2, Align::Center);
        drawnWon_ = true;
    }
}
