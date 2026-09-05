#include "SortGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t TILE = 0x24BD;
constexpr uint16_t LOCKED = 0x37F0;
constexpr uint16_t WRONG = 0xF9EA;

constexpr AppScoreInfo SORT_SCORE = {
    "sort", "Sorting", "sortBest", "pts", false
};

constexpr AppMetadata SORT_METADATA = {
    "sort",
    "Sorting",
    nullptr,
    "order nums",
    "Sorting",
    "Order numbers up or down.",
    &SORT_SCORE,
    LauncherIcon::Sort,
    13,
    true,
};
}

const AppMetadata& sortAppMetadata() {
    return SORT_METADATA;
}

const char* SortGame::title() const {
    return sortAppMetadata().screenTitle != nullptr
        ? sortAppMetadata().screenTitle
        : sortAppMetadata().title;
}

void SortGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(sortAppMetadata().score->bestKey, 0));
    newRound();
    markDirty();
}

Rect SortGame::tileRect(uint8_t index) const {
    const uint8_t col = index % 3;
    const uint8_t row = index / 3;
    return Rect{static_cast<int16_t>(24 + col * 92), static_cast<int16_t>(82 + row * 58), 76, 44};
}

void SortGame::newRound() {
    /* New numbers, every tile unlocked, the direction possibly reversed and the
     * sorted panel off. A layout change, and it lives here so no caller can
     * deal a round and forget. */
    markFullDirty();
    count_ = static_cast<uint8_t>(4 + min<uint16_t>(2, score_ / 4));
    ascending_ = (score_ % 6) < 4;
    next_ = 0;
    flashTile_ = -1;
    flashUntil_ = 0;
    const int16_t maxValue = score_ < 6 ? 20 : (score_ < 12 ? 50 : 99);

    for (uint8_t i = 0; i < count_; ++i) {
        bool unique = false;
        while (!unique) {
            numbers_[i] = random(1, maxValue + 1);
            unique = true;
            for (uint8_t j = 0; j < i; ++j) {
                if (numbers_[j] == numbers_[i]) {
                    unique = false;
                }
            }
        }
        ordered_[i] = numbers_[i];
        locked_[i] = false;
    }

    for (uint8_t i = 0; i < count_; ++i) {
        for (uint8_t j = i + 1; j < count_; ++j) {
            if (ordered_[j] < ordered_[i]) {
                const int16_t tmp = ordered_[i];
                ordered_[i] = ordered_[j];
                ordered_[j] = tmp;
            }
        }
    }
}

int16_t SortGame::expectedValue() const {
    return ascending_ ? ordered_[next_] : ordered_[count_ - 1 - next_];
}

bool SortGame::allLocked() const {
    return next_ >= count_;
}

int8_t SortGame::touchedTile(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < count_; ++i) {
        if (!locked_[i] && tileRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

void SortGame::update(AppContext& host, const TouchPoint& touch) {
    if (flashUntil_ > 0 && millis() > flashUntil_) {
        flashUntil_ = 0;
        flashTile_ = -1;
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    if (allLocked()) {
        ++score_;
        ++streak_;
        if (host.saveBestScore(sortAppMetadata().score->bestKey, streak_, false)) {
            bestStreak_ = streak_;
        }
        newRound();
        markDirty();
        return;
    }

    const int8_t tile = touchedTile(touch.x, touch.y);
    if (tile < 0) {
        return;
    }

    if (numbers_[tile] == expectedValue()) {
        locked_[tile] = true;
        flashTile_ = tile;
        flashError_ = false;
        flashUntil_ = millis() + 350UL;
        ++next_;
    } else {
        streak_ = 0;
        flashTile_ = tile;
        flashError_ = true;
        flashUntil_ = millis() + 500UL;
    }
    markDirty();
}

void SortGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    /* The direction is fixed for the round, and a new round is a full repaint,
     * so this is static even though it is not constant. */
    Ui::drawLabel(tft, Rect{10, 55, 300, 18},
                  ascending_ ? "Tap smallest to largest" : "Tap largest to smallest",
                  Ui::text(), 2, Align::Center);

    for (uint8_t i = 0; i < 6; ++i) {
        drawnTile_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnBest_ = 0xFFFF;
    drawnSorted_ = false;
}

void SortGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    for (uint8_t i = 0; i < count_; ++i) {
        const uint8_t state = static_cast<uint8_t>((locked_[i] ? 1 : 0) |
                              (flashTile_ == static_cast<int8_t>(i)
                                   ? (flashError_ ? 2 : 4) : 0));
        if (state == drawnTile_[i]) {
            continue;
        }
        uint16_t fill = locked_[i] ? LOCKED : TILE;
        uint16_t text = locked_[i] ? TFT_BLACK : TFT_WHITE;
        if (flashTile_ == static_cast<int8_t>(i)) {
            fill = flashError_ ? WRONG : LOCKED;
            text = TFT_BLACK;
        }
        char label[8];
        snprintf(label, sizeof(label), "%d", numbers_[i]);
        /* drawButton fills its rect opaquely, so a tile erases the tile that
         * was there and the flash needs no clearing of its own. */
        Ui::drawButton(tft, tileRect(i), label, fill, Ui::outline(), text, false, 4);
        drawnTile_[i] = state;
    }

    /* Rounds grows and Best streak grows, but both reset when the game is
     * re-entered, and Best is TR_DATUM so it would leave its old left-hand end
     * behind. Cleared before written. */
    if (score_ != drawnScore_ || bestStreak_ != drawnBest_) {
        tft.fillRect(10, 33, 140, 20, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 8 - 170, 33, 170, 20, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        char buf[24];
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Rounds %u", score_);
        tft.drawString(buf, 10, 35, 2);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Best streak %u", bestStreak_);
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 35, 2);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnBest_ = bestStreak_;
    }

    /* Painted once. It overlaps the bottom row of tiles, but it never has to be
     * taken off: the only way past a sorted board is a tap, which deals a new
     * round and asks for a full repaint. */
    if (allLocked() && !drawnSorted_) {
        tft.fillRoundRect(58, 178, 204, 42, 8, Ui::panel());
        tft.drawRoundRect(58, 178, 204, 42, 8, Ui::success());
        Ui::drawLabel(tft, Rect{60, 184, 200, 30}, "Sorted - tap next", Ui::success(), 2, Align::Center);
        drawnSorted_ = true;
    }
}

