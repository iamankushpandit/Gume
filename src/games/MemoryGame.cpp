#include "MemoryGame.h"
#include "engine/AppRegistry.h"
#include "engine/ContentLoader.h"

namespace {
constexpr uint16_t CARD_BACK = 0x24BD;
constexpr uint16_t CARD_FRONT = 0x39EF;
constexpr uint16_t MATCHED = 0x07F3;
constexpr uint16_t ACCENT = 0xE8E4;
constexpr int16_t GRID_TOP = TOP_BAR_HEIGHT + 30;
constexpr int16_t GRID_GAP = 4;

constexpr AppScoreInfo MEMORY_SCORE = {
    "memory", "Memory", "memBest", "moves", true
};

constexpr AppMetadata MEMORY_METADATA = {
    "memory",
    "Memory",
    "Memory Match",
    "match pairs",
    "Memory",
    "Flip cards to find matching pairs.",
    &MEMORY_SCORE,
    LauncherIcon::Memory,
    1,
    true,
};
}

const AppMetadata& memoryAppMetadata() {
    return MEMORY_METADATA;
}

const char* MemoryGame::title() const {
    return memoryAppMetadata().screenTitle != nullptr
        ? memoryAppMetadata().screenTitle
        : memoryAppMetadata().title;
}

void MemoryGame::begin(AppContext& host) {
    newRound(host);
    markDirty();
}

void MemoryGame::newRound(AppContext& host) {
    host.content().loadMemoryConfig(config_);
    bestMoves_ = static_cast<uint16_t>(host.getScore(memoryAppMetadata().score->bestKey, 0));
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

/* This one was already doing the right arithmetic -- fit square cards to the
 * space, centre the grid in what is left -- against the wrong two numbers.
 * Feeding it the live panel is the whole change. */
Rect MemoryGame::cardRect(const Ui::Frame& f, uint8_t index) const {
    const uint8_t totalCols = config_.cols;
    const uint8_t totalRows = config_.rows;
    const int16_t maxCellW = (f.w - 20 - GRID_GAP * (totalCols - 1)) / totalCols;
    const int16_t maxCellH = (f.h - GRID_TOP - 4 - GRID_GAP * (totalRows - 1)) / totalRows;
    const int16_t cell = min<int16_t>(maxCellW, maxCellH);
    const int16_t gridW = totalCols * cell + (totalCols - 1) * GRID_GAP;
    const int16_t gridH = totalRows * cell + (totalRows - 1) * GRID_GAP;
    const int16_t startX = (f.w - gridW) / 2;
    const int16_t startY = GRID_TOP + (f.h - GRID_TOP - gridH) / 2;
    const uint8_t row = index / totalCols;
    const uint8_t col = index % totalCols;
    return Rect{static_cast<int16_t>(startX + col * (cell + GRID_GAP)), static_cast<int16_t>(startY + row * (cell + GRID_GAP)), cell, cell};
}

int8_t MemoryGame::cardAt(const Ui::Frame& f, int16_t x, int16_t y) const {
    const uint8_t total = config_.rows * config_.cols;
    for (uint8_t i = 0; i < total; ++i) {
        if (cardRect(f, i).contains(x, y, TOUCH_HIT_SLOP)) {
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

void MemoryGame::update(AppContext& host, const TouchPoint& touch) {
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
        host.beepOk();
        markDirty();
        return;
    }

    const int8_t index = cardAt(Ui::frame(host.display()), touch.x, touch.y);
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
            host.beepOk();
            if (allMatched()) {
                if (host.saveBestScore(memoryAppMetadata().score->bestKey, moves_, true)) {
                    bestMoves_ = moves_;
                }
            }
        } else {
            resolving_ = true;
            resolveAt_ = millis() + 900UL;
            host.beepError();
        }
    }
    markDirty();
}

void MemoryGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());

    const uint8_t total = config_.rows * config_.cols;
    for (uint8_t i = 0; i < total; ++i) {
        const Rect r = cardRect(f, i);
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
        const int16_t bannerY = static_cast<int16_t>(
            TOP_BAR_HEIGHT + (f.h - TOP_BAR_HEIGHT) * 39 / 100);
        tft.drawString("You matched all!", f.cx(), bannerY, 4);
        tft.drawString("Tap to play again", f.cx(), bannerY + 24, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
