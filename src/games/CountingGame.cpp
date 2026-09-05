#include "CountingGame.h"
#include "engine/AppRegistry.h"
#include "engine/ContentLoader.h"

namespace {
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

Rect CountingGame::answerRect(uint8_t index) const {
    return Rect{static_cast<int16_t>(15 + index * 76), 188, 62, 40};
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
        /* A new count means a different number of objects in the panel, which
         * is static -- only a full repaint replaces it. */
        markFullDirty();
        return;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        if (answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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

void CountingGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);
    tft.drawString("How many objects?", GAME_CANVAS_WIDTH / 2, 32, 4);

    /* The objects belong to the round. There are up to 21 of them, each a
     * filled circle and an outline, and not one of them moves while the player
     * is choosing -- so the whole panel is drawn once and a new round, which
     * changes the count, is a full repaint. */
    const Rect area{18, 76, 284, 98};
    tft.fillRoundRect(area.x, area.y, area.w, area.h, 8, Ui::panel());
    tft.drawRoundRect(area.x, area.y, area.w, area.h, 8, Ui::outline());
    for (uint8_t i = 0; i < count_; ++i) {
        const uint8_t col = i % 7;
        const uint8_t row = i / 7;
        const int16_t x = area.x + 24 + col * 39 + (row % 2) * 8;
        const int16_t y = area.y + 24 + row * 30;
        tft.fillCircle(x, y, 10, DOT_COLORS[i % 5]);
        tft.drawCircle(x, y, 10, TFT_DARKGREY);
    }
    tft.setTextDatum(TL_DATUM);

    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnStreak_ = 0xFFFF;
    drawnAnswered_ = !answered_;
    drawnStats_ = false;
}

void CountingGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    /* One centred line, so it grows both ways from the middle and a shorter
     * one leaves tails at BOTH ends. Cleared across its whole width first;
     * it sits at y=60 and the object panel starts at 76, so this cannot reach
     * the objects. */
    if (!drawnStats_ || score_ != drawnScore_ || streak_ != drawnStreak_) {
        tft.fillRect(20, 58, GAME_CANVAS_WIDTH - 40, 12, Ui::bg());
        char stats[48];
        snprintf(stats, sizeof(stats), "Score %u/%u   Streak %u   Best %u",
                 score_, rounds_, streak_, bestStreak_);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString(stats, GAME_CANVAS_WIDTH / 2, 60, 1);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnStreak_ = streak_;
        drawnStats_ = true;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t state = 0;
        if (answered_) {
            if (i == correctButton_) state = 1;
            else if (i == selected_) state = 2;
        }
        if (state == drawnButton_[i]) {
            continue;
        }
        const uint16_t fill = state == 1 ? GREEN : (state == 2 ? RED : BLUE);
        const uint16_t text = state == 0 ? TFT_WHITE : TFT_BLACK;
        char label[4];
        snprintf(label, sizeof(label), "%u", options_[i]);
        Ui::drawButton(tft, answerRect(i), label, fill, TFT_DARKGREY, text, false, 4);
        drawnButton_[i] = state;
    }

    /* Stops at 186, two rows above answerRect(0) at 188 -- this runs after the
     * button loop, so a rect reaching into them would erase rows nothing puts
     * back. The two wordings are different lengths, hence the clear. */
    if (answered_ != drawnAnswered_) {
        tft.fillRect(20, 168, GAME_CANVAS_WIDTH - 40, 18, Ui::bg());
        if (answered_) {
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
            tft.drawString(selected_ == correctButton_ ? "Correct - tap for next"
                                                       : "Green is the answer",
                           GAME_CANVAS_WIDTH / 2, 178, 2);
            tft.setTextDatum(TL_DATUM);
        }
        drawnAnswered_ = answered_;
    }
}
