#include "SortGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr int16_t HUD_Y = TOP_BAR_HEIGHT + 5;
constexpr int16_t PROMPT_Y = TOP_BAR_HEIGHT + 25;
constexpr int16_t TRAY_TOP = TOP_BAR_HEIGHT + 52;
constexpr int16_t TRAY_MAX_H = 130;

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

Rect SortGame::tileBand(const Ui::Frame& f) const {
    /* Down to the solved banner, but capped -- portrait leaves 174px here and
     * two rows of 83px tiles look like buttons for a different game. */
    int16_t h = static_cast<int16_t>(f.h - 64 - TRAY_TOP);
    if (h > TRAY_MAX_H) {
        h = TRAY_MAX_H;
    }
    return Rect{16, TRAY_TOP, static_cast<int16_t>(f.w - 32), h};
}

Rect SortGame::tileRect(const Ui::Frame& f, uint8_t index) const {
    return Ui::gridCell(tileBand(f), 3, 2, index, 8);
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

int8_t SortGame::touchedTile(const Ui::Frame& f, int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < count_; ++i) {
        if (!locked_[i] && tileRect(f, i).contains(x, y, TOUCH_HIT_SLOP)) {
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

    const int8_t tile = touchedTile(Ui::frame(host.display()), touch.x, touch.y);
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

void SortGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    char roundsBuf[20];
    snprintf(roundsBuf, sizeof(roundsBuf), "Rounds %u", score_);
    tft.drawString(roundsBuf, 10, 35, 2);
    tft.setTextDatum(TR_DATUM);
    char bestBuf[24];
    snprintf(bestBuf, sizeof(bestBuf), "Best streak %u", bestStreak_);
    tft.drawString(bestBuf, f.w - 8, HUD_Y, 2);
    Ui::drawLabel(tft, Rect{10, PROMPT_Y, static_cast<int16_t>(f.w - 20), 18},
                  ascending_ ? "Tap smallest to largest" : "Tap largest to smallest",
                  Ui::text(), 2, Align::Center);

    for (uint8_t i = 0; i < count_; ++i) {
        uint16_t fill = locked_[i] ? LOCKED : TILE;
        uint16_t text = locked_[i] ? TFT_BLACK : TFT_WHITE;
        if (flashTile_ == i) {
            fill = flashError_ ? WRONG : LOCKED;
            text = TFT_BLACK;
        }
        char label[8];
        snprintf(label, sizeof(label), "%d", numbers_[i]);
        Ui::drawButton(tft, tileRect(f, i), label, fill, Ui::outline(), text, false, 4);
    }

    if (allLocked()) {
        tft.fillRoundRect(58, 178, 204, 42, 8, Ui::panel());
        tft.drawRoundRect(58, 178, 204, 42, 8, Ui::success());
        Ui::drawLabel(tft, Ui::centreIn(Rect{0, static_cast<int16_t>(f.h - 56), f.w, 30}, 200, 30),
                      "Sorted - tap next", Ui::success(), 2, Align::Center);
    }
}

