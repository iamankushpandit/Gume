#include "MemoryGame.h"

namespace {
constexpr uint16_t CARD_BACK = 0x24BD;
constexpr uint16_t CARD_FRONT = 0x39EF;
constexpr uint16_t MATCHED = 0x07F3;
constexpr uint16_t ACCENT = 0xE8E4;
constexpr int16_t GRID_TOP = TOP_BAR_HEIGHT + 30;
constexpr int16_t GRID_GAP = 4;
}

const char* MemoryGame::title() const {
    return "Memory Match";
}

void MemoryGame::begin(GameHost& host) {
    newRound(host);
    markDirty();
}

void MemoryGame::newRound(GameHost& host) {
    host.content().loadMemoryConfig(config_);
    bestMoves_ = static_cast<uint16_t>(host.board().getScore("memBest", 0));
    const uint8_t total = config_.rows * config_.cols;
    config_.pairCount = min<uint8_t>(MAX_MEMORY_PAIRS, total / 2);

    for (uint8_t i = 0; i < total; ++i) {
        cards_[i] = i / 2;
        visible_[i] = false;
        matched_[i] = false;
    }
    for (int i = total - 1; i > 0; --i) {
        const uint8_t j = random(i + 1);
        const uint8_t tmp = cards_[i];
        cards_[i] = cards_[j];
        cards_[j] = tmp;
    }
    first_ = -1;
    second_ = -1;
    moves_ = 0;
    resolveAt_ = 0;
    resolving_ = false;
}

Rect MemoryGame::cardRect(uint8_t index) const {
    const uint8_t totalCols = config_.cols;
    const uint8_t totalRows = config_.rows;
    const int16_t maxCellW = (SCREEN_WIDTH - 20 - GRID_GAP * (totalCols - 1)) / totalCols;
    const int16_t maxCellH = (SCREEN_HEIGHT - GRID_TOP - 4 - GRID_GAP * (totalRows - 1)) / totalRows;
    const int16_t cell = min<int16_t>(maxCellW, maxCellH);
    const int16_t gridW = totalCols * cell + (totalCols - 1) * GRID_GAP;
    const int16_t gridH = totalRows * cell + (totalRows - 1) * GRID_GAP;
    const int16_t startX = (SCREEN_WIDTH - gridW) / 2;
    const int16_t startY = GRID_TOP + (SCREEN_HEIGHT - GRID_TOP - gridH) / 2;
    const uint8_t row = index / totalCols;
    const uint8_t col = index % totalCols;
    return Rect{static_cast<int16_t>(startX + col * (cell + GRID_GAP)), static_cast<int16_t>(startY + row * (cell + GRID_GAP)), cell, cell};
}

int8_t MemoryGame::cardAt(int16_t x, int16_t y) const {
    const uint8_t total = config_.rows * config_.cols;
    for (uint8_t i = 0; i < total; ++i) {
        if (cardRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

bool MemoryGame::allMatched() const {
    const uint8_t total = config_.rows * config_.cols;
    for (uint8_t i = 0; i < total; ++i) {
        if (!matched_[i]) {
            return false;
        }
    }
    return true;
}

void MemoryGame::update(GameHost& host, const TouchPoint& touch) {
    if (resolving_ && millis() >= resolveAt_) {
        visible_[first_] = false;
        visible_[second_] = false;
        first_ = -1;
        second_ = -1;
        resolving_ = false;
        markDirty();
    }

    if (!touch.justPressed || resolving_) {
        return;
    }

    if (allMatched()) {
        newRound(host);
        host.board().beepOk();
        markDirty();
        return;
    }

    const int8_t index = cardAt(touch.x, touch.y);
    if (index < 0 || matched_[index] || visible_[index]) {
        return;
    }

    visible_[index] = true;
    if (first_ < 0) {
        first_ = index;
    } else {
        second_ = index;
        ++moves_;
        if (cards_[first_] == cards_[second_]) {
            matched_[first_] = true;
            matched_[second_] = true;
            first_ = -1;
            second_ = -1;
            host.board().beepOk();
            if (allMatched()) {
                if (host.board().saveBestScore("memBest", moves_, true)) {
                    bestMoves_ = moves_;
                }
            }
        } else {
            resolving_ = true;
            resolveAt_ = millis() + 900UL;
            host.board().beepError();
        }
    }
    markDirty();
}

void MemoryGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    const uint8_t total = config_.rows * config_.cols;
    for (uint8_t i = 0; i < total; ++i) {
        const Rect r = cardRect(i);
        const bool up = visible_[i] || matched_[i];
        const uint16_t fill = matched_[i] ? MATCHED : (up ? CARD_FRONT : CARD_BACK);
        tft.fillRoundRect(r.x + 2, r.y + 3, r.w, r.h, 6, 0xBDF7);
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, TFT_DARKGREY);
        if (up) {
            const String label = config_.symbols[cards_[i]];
            const uint8_t font = tft.textWidth(label, 4) <= r.w - 8 ? 4 : 2;
            tft.setTextColor(TFT_BLACK, fill);
            tft.setTextDatum(MC_DATUM);
            tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, font);
            tft.setTextDatum(TL_DATUM);
        } else {
            tft.fillCircle(r.x + r.w / 2, r.y + r.h / 2, min(r.w, r.h) / 7, ACCENT);
        }
    }

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Moves ") + moves_, 8, TOP_BAR_HEIGHT + 2, 2);
    tft.drawString(bestMoves_ > 0 ? String("Best ") + bestMoves_ : "Best --", 8, TOP_BAR_HEIGHT + 18, 1);
    if (allMatched()) {
        tft.fillRoundRect(50, 92, 220, 56, 8, Ui::panel());
        tft.drawRoundRect(50, 92, 220, 56, 8, Ui::success());
        tft.setTextColor(Ui::success(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("You matched all!", SCREEN_WIDTH / 2, 112, 4);
        tft.drawString("Tap to play again", SCREEN_WIDTH / 2, 136, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
