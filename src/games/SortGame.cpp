#include "SortGame.h"

namespace {
constexpr uint16_t TILE = 0x24BD;
constexpr uint16_t LOCKED = 0x37F0;
constexpr uint16_t WRONG = 0xF9EA;
}

const char* SortGame::title() const {
    return "Sorting";
}

void SortGame::begin(GameHost& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.board().getScore("sortBest", 0));
    newRound();
    markDirty();
}

Rect SortGame::tileRect(uint8_t index) const {
    const uint8_t col = index % 3;
    const uint8_t row = index / 3;
    return Rect{static_cast<int16_t>(24 + col * 92), static_cast<int16_t>(82 + row * 58), 76, 44};
}

void SortGame::newRound() {
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

void SortGame::update(GameHost& host, const TouchPoint& touch) {
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
        if (host.board().saveBestScore("sortBest", streak_, false)) {
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

void SortGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(tft, title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Rounds ") + score_, 10, 35, 2);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Best streak ") + bestStreak_, SCREEN_WIDTH - 8, 35, 2);
    Ui::drawLabel(tft, Rect{10, 55, 300, 18}, ascending_ ? "Tap smallest to largest" : "Tap largest to smallest", Ui::text(), 2, Align::Center);

    for (uint8_t i = 0; i < count_; ++i) {
        uint16_t fill = locked_[i] ? LOCKED : TILE;
        uint16_t text = locked_[i] ? TFT_BLACK : TFT_WHITE;
        if (flashTile_ == i) {
            fill = flashError_ ? WRONG : LOCKED;
            text = TFT_BLACK;
        }
        Ui::drawButton(tft, tileRect(i), String(numbers_[i]), fill, Ui::outline(), text, false, 4);
    }

    if (allLocked()) {
        tft.fillRoundRect(58, 178, 204, 42, 8, Ui::panel());
        tft.drawRoundRect(58, 178, 204, 42, 8, Ui::success());
        Ui::drawLabel(tft, Rect{60, 184, 200, 30}, "Sorted - tap next", Ui::success(), 2, Align::Center);
    }
}

