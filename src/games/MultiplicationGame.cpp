#include "MultiplicationGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t YELLOW = 0xFEC0;

constexpr uint8_t LEVEL1_TABLES[] = {2, 5, 10};
constexpr uint8_t LEVEL2_TABLES[] = {1, 2, 3, 4, 5, 10};
constexpr uint8_t LEVEL3_TABLES[] = {1, 2, 3, 4, 5, 6, 10};
constexpr uint8_t LEVEL4_TABLES[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
constexpr uint8_t LEVEL5_TABLES[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

constexpr AppScoreInfo MULTIPLICATION_SCORE = {
    "multiply", "Multiply", "multBest", "pts", false
};

constexpr AppMetadata MULTIPLICATION_METADATA = {
    "multiply",
    "Multiply",
    "Multiplication",
    "times tables",
    "Multiply",
    "Practice multiplication facts.",
    &MULTIPLICATION_SCORE,
    LauncherIcon::Multiplication,
    3,
    true,
};
}

const AppMetadata& multiplicationAppMetadata() {
    return MULTIPLICATION_METADATA;
}

const char* MultiplicationGame::title() const {
    return multiplicationAppMetadata().screenTitle != nullptr
        ? multiplicationAppMetadata().screenTitle
        : multiplicationAppMetadata().title;
}

void MultiplicationGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(multiplicationAppMetadata().score->bestKey, 0));
    newQuestion();
    markDirty();
}

uint8_t MultiplicationGame::level() const {
    return min<uint8_t>(5, 1 + score_ / 5);
}

Rect MultiplicationGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152), static_cast<int16_t>(144 + row * 46), 132, 38};
}

uint8_t MultiplicationGame::pickTable(uint8_t currentLevel) const {
    switch (currentLevel) {
        case 1:
            return LEVEL1_TABLES[random(sizeof(LEVEL1_TABLES))];
        case 2:
            return LEVEL2_TABLES[random(sizeof(LEVEL2_TABLES))];
        case 3:
            return LEVEL3_TABLES[random(sizeof(LEVEL3_TABLES))];
        case 4:
            return LEVEL4_TABLES[random(sizeof(LEVEL4_TABLES))];
        default:
            return LEVEL5_TABLES[random(sizeof(LEVEL5_TABLES))];
    }
}

bool MultiplicationGame::optionExists(int16_t value, uint8_t upTo) const {
    for (uint8_t i = 0; i < upTo; ++i) {
        if (options_[i] == value) {
            return true;
        }
    }
    return false;
}

void MultiplicationGame::updateBest(AppContext& host) {
    if (streak_ > bestStreak_) {
        bestStreak_ = streak_;
        host.setScore(multiplicationAppMetadata().score->bestKey, bestStreak_);
    }
}

void MultiplicationGame::newQuestion() {
    const uint8_t currentLevel = level();
    selected_ = -1;
    answered_ = false;
    left_ = pickTable(currentLevel);
    right_ = random(1, currentLevel <= 2 ? 11 : 13);
    if (random(2) == 0) {
        const int16_t tmp = left_;
        left_ = right_;
        right_ = tmp;
    }
    answer_ = left_ * right_;
    makeOptions();
}

void MultiplicationGame::makeOptions() {
    correctButton_ = random(4);
    for (uint8_t i = 0; i < 4; ++i) {
        options_[i] = 0;
    }
    options_[correctButton_] = answer_;

    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctButton_) {
            continue;
        }
        int16_t candidate = answer_;
        uint8_t attempts = 0;
        while ((candidate == answer_ || optionExists(candidate, i)) && attempts < 50) {
            int16_t nearbyTable = left_ + random(-2, 3);
            int16_t nearbyFactor = right_ + random(-2, 3);
            if (nearbyTable < 1) {
                nearbyTable = 1;
            }
            if (nearbyFactor < 1) {
                nearbyFactor = 1;
            }
            candidate = nearbyTable * nearbyFactor;
            if (random(3) == 0) {
                candidate = answer_ + random(-12, 13);
            }
            if (candidate < 0) {
                candidate = abs(candidate);
            }
            ++attempts;
        }
        while (candidate == answer_ || optionExists(candidate, i)) {
            ++candidate;
        }
        options_[i] = candidate;
    }
}

void MultiplicationGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (answered_) {
        newQuestion();
        markDirty();
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            selected_ = i;
            answered_ = true;
            if (i == correctButton_) {
                ++score_;
                ++streak_;
                updateBest(host);
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

void MultiplicationGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Level ") + level(), 10, 35, 2);
    tft.drawString(String("Score ") + score_, 10, 52, 1);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Streak ") + streak_, GAME_CANVAS_WIDTH - 10, 35, 2);
    tft.drawString(bestStreak_ > 0 ? String("Best ") + bestStreak_ : "Best --", GAME_CANVAS_WIDTH - 10, 52, 1);

    const String equation = String(left_) + " x " + right_ + " = ?";
    tft.fillRoundRect(26, 76, 268, 54, 8, Ui::panel());
    tft.drawRoundRect(26, 76, 268, 54, 8, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(equation, GAME_CANVAS_WIDTH / 2, 103, 4);

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
        Ui::drawButton(tft, answerRect(i), String(options_[i]), fill, TFT_DARKGREY, text, false, 4);
    }

    if (answered_) {
        tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(selected_ == correctButton_ ? "Correct - tap for next" : "Green is correct - tap next", GAME_CANVAS_WIDTH / 2, 136, 2);
    } else {
        tft.setTextColor(YELLOW, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Tap the product", GAME_CANVAS_WIDTH / 2, 136, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
