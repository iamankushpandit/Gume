#include "CountingGame.h"
#include "engine/AppRegistry.h"
#include "engine/ContentLoader.h"

namespace {
constexpr int16_t QUESTION_Y = TOP_BAR_HEIGHT + 2;
constexpr int16_t STATS_Y = TOP_BAR_HEIGHT + 30;
constexpr int16_t AREA_TOP = TOP_BAR_HEIGHT + 46;
constexpr int16_t ANSWER_H = 40;
constexpr uint8_t ANSWER_COUNT = 4;
/* Dot pitch, and the inset the first dot sits at inside the panel. */
constexpr int16_t DOT_PITCH = 39;
constexpr int16_t DOT_ROW_PITCH = 30;
constexpr int16_t DOT_INSET = 24;

constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t DOT_COLORS[5] = {0xE8E4, 0x24BD, 0x05D1, 0xFEC0, 0xAA9F};

constexpr AppScoreInfo COUNTING_SCORE = {
    "counting", "Counting", "countBest", "streak", false
};

constexpr AppMetadata COUNTING_METADATA = {
    "counting",
    "Counting",
    nullptr,
    "tap number",
    "Counting",
    "Count objects, tap the number.",
    &COUNTING_SCORE,
    LauncherIcon::Counting,
    9,
    true,
};
}

const AppMetadata& countingAppMetadata() {
    return COUNTING_METADATA;
}

const char* CountingGame::title() const {
    return countingAppMetadata().title;
}

void CountingGame::begin(AppContext& host) {
    host.content().loadCountingConfig(config_);
    score_ = 0;
    rounds_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(countingAppMetadata().score->bestKey, 0));
    newQuestion();
    markDirty();
}

uint8_t CountingGame::answerColumns(const Ui::Frame& f) const {
    /* Four 62px buttons plus gaps need 260px; landscape has it, portrait does
     * not, and 2x2 keeps them finger-sized rather than shrinking to fit. */
    return f.tall() ? 2 : ANSWER_COUNT;
}

Rect CountingGame::answerBand(const Ui::Frame& f) const {
    const uint8_t cols = answerColumns(f);
    const uint8_t rows = static_cast<uint8_t>(ANSWER_COUNT / cols);
    const int16_t h = static_cast<int16_t>(rows * ANSWER_H + (rows - 1) * 8);
    return Rect{15, static_cast<int16_t>(f.h - h - 12),
                static_cast<int16_t>(f.w - 30), h};
}

Rect CountingGame::answerRect(const Ui::Frame& f, uint8_t index) const {
    const uint8_t cols = answerColumns(f);
    const uint8_t rows = static_cast<uint8_t>(ANSWER_COUNT / cols);
    return Ui::gridCell(answerBand(f), cols, rows, index, 8);
}

Rect CountingGame::objectArea(const Ui::Frame& f) const {
    const int16_t bottom = static_cast<int16_t>(answerBand(f).y - 14);
    return Rect{18, AREA_TOP, static_cast<int16_t>(f.w - 36),
                static_cast<int16_t>(bottom - AREA_TOP)};
}

void CountingGame::newQuestion() {
    count_ = random(config_.minCount, config_.maxCount + 1);
    selected_ = -1;
    answered_ = false;
    makeOptions();
}

void CountingGame::makeOptions() {
    correctButton_ = random(4);
    for (uint8_t i = 0; i < 4; ++i) {
        options_[i] = 0;
    }
    options_[correctButton_] = count_;

    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctButton_) {
            continue;
        }
        uint8_t candidate = 0;
        bool unique = false;
        while (!unique) {
            const int low = max(1, static_cast<int>(count_) - 3);
            const int high = min(20, static_cast<int>(count_) + 4);
            candidate = random(low, high + 1);
            unique = candidate != count_;
            for (uint8_t j = 0; j < i; ++j) {
                if (options_[j] == candidate) {
                    unique = false;
                }
            }
        }
        options_[i] = candidate;
    }
}

void CountingGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    if (answered_) {
        newQuestion();
        markDirty();
        return;
    }
    const Ui::Frame f = Ui::frame(host.display());
    for (uint8_t i = 0; i < ANSWER_COUNT; ++i) {
        if (answerRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            selected_ = i;
            answered_ = true;
            ++rounds_;
            if (i == correctButton_) {
                ++score_;
                ++streak_;
                if (streak_ > bestStreak_ &&
                    host.saveBestScore(countingAppMetadata().score->bestKey, streak_, false)) {
                    bestStreak_ = streak_;
                }
                host.beepOk();
            } else {
                streak_ = 0;
                host.beepError();
            }
            markDirty();
            return;
        }
    }
}

void CountingGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());
    tft.setTextColor(Ui::text(), Ui::bg());
    /* The font-4 question spans roughly 60..260px when centred, and a
     * right-aligned "Score n/m" at the same y started around 213 -- they
     * overlapped. Score and streak now share one small line underneath. */
    tft.setTextDatum(TC_DATUM);
    tft.drawString("How many objects?", f.cx(), QUESTION_Y, 4);
    tft.setTextColor(Ui::muted(), Ui::bg());
    char stats[48];
    snprintf(stats, sizeof(stats), "Score %u/%u   Streak %u   Best %u",
             score_, rounds_, streak_, bestStreak_);
    tft.drawString(stats, f.cx(), STATS_Y, 1);
    tft.setTextColor(Ui::text(), Ui::bg());

    const Rect area = objectArea(f);
    tft.fillRoundRect(area.x, area.y, area.w, area.h, 8, Ui::panel());
    tft.drawRoundRect(area.x, area.y, area.w, area.h, 8, Ui::outline());
    /* Seven dots per row is what 284px of panel holds; 204px holds five. Work
     * it out from the panel rather than hardcoding the landscape answer, or
     * the right-hand dots fall outside the rounded rect in portrait. */
    uint8_t cols = static_cast<uint8_t>((area.w - 2 * DOT_INSET) / DOT_PITCH + 1);
    if (cols < 3) {
        cols = 3;
    }
    const uint8_t rows = static_cast<uint8_t>((count_ + cols - 1) / cols);
    int16_t rowPitch = DOT_ROW_PITCH;
    if (rows > 0 && rows * rowPitch > area.h - 2 * DOT_INSET + DOT_ROW_PITCH) {
        rowPitch = static_cast<int16_t>((area.h - DOT_INSET) / rows);
    }
    for (uint8_t i = 0; i < count_; ++i) {
        const uint8_t col = static_cast<uint8_t>(i % cols);
        const uint8_t row = static_cast<uint8_t>(i / cols);
        const int16_t x = static_cast<int16_t>(area.x + DOT_INSET + col * DOT_PITCH + (row % 2) * 8);
        const int16_t y = static_cast<int16_t>(area.y + DOT_INSET + row * rowPitch);
        tft.fillCircle(x, y, 10, DOT_COLORS[i % 5]);
        tft.drawCircle(x, y, 10, TFT_DARKGREY);
    }

    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t fill = BLUE;
        uint16_t text = TFT_WHITE;
        if (answered_) {
            if (i == correctButton_) {
                fill = GREEN;
                text = TFT_BLACK;
            } else if (i == selected_) {
                fill = RED;
                text = TFT_BLACK;
            }
        }
        char label[4];
        snprintf(label, sizeof(label), "%u", options_[i]);
        Ui::drawButton(tft, answerRect(f, i), label, fill, TFT_DARKGREY, text, false, 4);
    }

    if (answered_) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
        tft.drawString(selected_ == correctButton_ ? "Correct - tap for next" : "Green is the answer",
                       f.cx(), static_cast<int16_t>(answerBand(f).y - 10), 2);
    }
    tft.setTextDatum(TL_DATUM);
}
