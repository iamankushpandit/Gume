#include "PercentCircleGame.h"

namespace {
constexpr uint16_t PERCENT_FILL = 0xFCA8;     // Ui::rgb(255, 202, 84) - golden/orange
constexpr uint16_t PERCENT_EMPTY = 0x39EF;    // similar to surface
constexpr uint16_t PERCENT_OUTLINE = 0x2478;  // Ui::rgb(36, 132, 204) - blue
constexpr int16_t CIRCLE_CX = 96;
constexpr int16_t CIRCLE_CY = 130;
constexpr int16_t CIRCLE_RADIUS = 62;
}

const char* PercentCircleGame::title() const {
    return "Percent";
}

void PercentCircleGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    newRound(host);
    markFullDirty();
}

PercentCircleGame::RoundType PercentCircleGame::nextRoundType() const {
    const uint8_t currentLevel = level();
    const uint8_t cycleIndex = score_ % 3;

    if (currentLevel == 1) {
        return RoundType::ReadCircle;
    }

    if (currentLevel == 2) {
        return cycleIndex == 0 ? RoundType::ReadCircle : RoundType::MakeCircle;
    }

    return static_cast<RoundType>(cycleIndex);
}

void PercentCircleGame::generateOptions() {
    correctButton_ = random(4);
    const uint8_t currentLevel = level();

    for (uint8_t i = 0; i < 4; ++i) {
        options_[i] = 0;
    }
    options_[correctButton_] = targetPercent_;

    const uint8_t* possiblePercents = nullptr;
    uint8_t percentCount = 0;

    if (currentLevel == 1) {
        static constexpr uint8_t L1[] = {25, 50, 75, 100};
        possiblePercents = L1;
        percentCount = 4;
    } else if (currentLevel == 2) {
        static constexpr uint8_t L2[] = {10, 20, 25, 30, 40, 50, 60, 70, 75, 80, 90, 100};
        possiblePercents = L2;
        percentCount = 12;
    } else {
        static constexpr uint8_t L3[] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100};
        possiblePercents = L3;
        percentCount = 20;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctButton_) {
            continue;
        }

        uint8_t candidate;
        uint8_t attempts = 0;
        do {
            candidate = possiblePercents[random(percentCount)];
            ++attempts;
        } while ((candidate == targetPercent_ || candidate == options_[i]) && attempts < 20);

        bool exists = false;
        for (uint8_t j = 0; j < i; ++j) {
            if (options_[j] == candidate) {
                exists = true;
                break;
            }
        }

        if (!exists && candidate != targetPercent_) {
            options_[i] = candidate;
        } else if (candidate != targetPercent_) {
            options_[i] = candidate;
        }
    }
}

void PercentCircleGame::newRound(AppContext& host) {
    (void)host;
    roundType_ = nextRoundType();
    correctButton_ = -1;
    flashIndex_ = -1;
    flashUntil_ = 0;
    roundComplete_ = false;
    currentPercent_ = 0;

    if (roundType_ == RoundType::ReadCircle) {
        targetPercent_ = (level() == 1) ?
            ((random(4) + 1) * 25) :
            ((random(10) + 1) * 10);
        generateOptions();
        snprintf(promptBuffer_, sizeof(promptBuffer_), "What percent?");
    } else if (roundType_ == RoundType::MakeCircle) {
        targetPercent_ = (random(20) + 1) * 5;
        snprintf(promptBuffer_, sizeof(promptBuffer_), "Shade %u%%", targetPercent_);
    } else {
        static constexpr uint8_t BASE_NUMBERS[] = {10, 20, 40, 50, 100};
        static constexpr uint8_t PERCENTS[] = {10, 25, 50, 75, 100};

        baseNumber_ = BASE_NUMBERS[random(5)];
        targetPercent_ = PERCENTS[random(5)];
        uint8_t correctAnswer = (baseNumber_ * targetPercent_) / 100;

        correctButton_ = random(4);
        for (uint8_t i = 0; i < 4; ++i) {
            options_[i] = 0;
        }
        options_[correctButton_] = correctAnswer;

        for (uint8_t i = 0; i < 4; ++i) {
            if (i == correctButton_) {
                continue;
            }
            uint8_t candidate;
            uint8_t attempts = 0;
            do {
                candidate = random(baseNumber_ + 1);
                ++attempts;
            } while (candidate == correctAnswer && attempts < 10);
            options_[i] = candidate;
        }

        snprintf(promptBuffer_, sizeof(promptBuffer_), "%u%% of %u = ?", targetPercent_, baseNumber_);
    }
}

void PercentCircleGame::markCorrect(AppContext& host) {
    ++score_;
    ++streak_;
    host.saveBestScore("pctBest", score_, false);
    roundComplete_ = true;
    flashIndex_ = -1;
    host.beepOk();
}

void PercentCircleGame::markWrong() {
    streak_ = 0;
    flashIndex_ = correctButton_;
    flashUntil_ = millis() + 500UL;
}

void PercentCircleGame::fillSlice(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t radius, float startAngle, float endAngle, uint16_t color) const {
    const float step = PI / 24.0f;
    float angle = startAngle;
    while (angle < endAngle) {
        const float next = min(endAngle, angle + step);
        const int16_t x1 = cx + static_cast<int16_t>(cosf(angle) * radius);
        const int16_t y1 = cy + static_cast<int16_t>(sinf(angle) * radius);
        const int16_t x2 = cx + static_cast<int16_t>(cosf(next) * radius);
        const int16_t y2 = cy + static_cast<int16_t>(sinf(next) * radius);
        tft.fillTriangle(cx, cy, x1, y1, x2, y2, color);
        angle = next;
    }
}

void PercentCircleGame::drawCircle(TFT_eSPI& tft, uint8_t percent, bool highlight) const {
    tft.fillCircle(CIRCLE_CX, CIRCLE_CY, CIRCLE_RADIUS + 3, highlight ? Ui::warning() : Ui::surface());
    tft.fillCircle(CIRCLE_CX, CIRCLE_CY, CIRCLE_RADIUS, PERCENT_EMPTY);

    if (percent > 0) {
        const float fillAngle = (percent / 100.0f) * TWO_PI;
        fillSlice(tft, CIRCLE_CX, CIRCLE_CY, CIRCLE_RADIUS, -PI / 2.0f, -PI / 2.0f + fillAngle, PERCENT_FILL);
    }

    tft.drawCircle(CIRCLE_CX, CIRCLE_CY, CIRCLE_RADIUS, PERCENT_OUTLINE);

    for (uint8_t i = 0; i < 10; ++i) {
        const float angle = -PI / 2.0f + i * TWO_PI / 10.0f;
        const int16_t x = CIRCLE_CX + static_cast<int16_t>(cosf(angle) * CIRCLE_RADIUS);
        const int16_t y = CIRCLE_CY + static_cast<int16_t>(sinf(angle) * CIRCLE_RADIUS);
        tft.drawLine(CIRCLE_CX, CIRCLE_CY, x, y, Ui::rgb(100, 150, 200));
    }
}

void PercentCircleGame::drawReadCircleMode(TFT_eSPI& tft) const {
    drawCircle(tft, targetPercent_, false);

    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t fill = Ui::panel();
        uint16_t text = Ui::text();
        uint16_t outline = Ui::outline();

        if (roundComplete_ && i == correctButton_) {
            fill = Ui::success();
            text = TFT_BLACK;
            outline = Ui::success();
        } else if (flashIndex_ == i) {
            fill = Ui::error();
            text = TFT_BLACK;
            outline = Ui::error();
        }

        char label[16];
        snprintf(label, sizeof(label), "%u%%", options_[i]);
        Ui::drawButton(tft, optionRect(i), label, fill, outline, text, false, 2);
    }
}

void PercentCircleGame::drawMakeCircleMode(TFT_eSPI& tft) const {
    drawCircle(tft, currentPercent_, false);

    tft.fillRoundRect(200, 152, 38, 34, 4, Ui::panel());
    tft.drawRoundRect(200, 152, 38, 34, 4, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    char valueStr[8];
    snprintf(valueStr, sizeof(valueStr), "%u%%", currentPercent_);
    tft.drawString(valueStr, 219, 169, 1);

    Ui::drawPagerButton(tft, minusRect(), "−", currentPercent_ > 0);
    Ui::drawPagerButton(tft, plusRect(), "+", currentPercent_ < 100);

    uint16_t okFill = currentPercent_ == targetPercent_ ? Ui::success() : Ui::panel();
    uint16_t okText = currentPercent_ == targetPercent_ ? TFT_BLACK : Ui::text();
    Ui::drawButton(tft, okRect(), "✓", okFill, Ui::outline(), okText, false, 2);

    tft.setTextDatum(TL_DATUM);
}

void PercentCircleGame::drawPercentOfNumberMode(TFT_eSPI& tft) const {
    drawCircle(tft, targetPercent_, false);

    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t fill = Ui::panel();
        uint16_t text = Ui::text();
        uint16_t outline = Ui::outline();

        if (roundComplete_ && i == correctButton_) {
            fill = Ui::success();
            text = TFT_BLACK;
            outline = Ui::success();
        } else if (flashIndex_ == i) {
            fill = Ui::error();
            text = TFT_BLACK;
            outline = Ui::error();
        }

        char label[16];
        snprintf(label, sizeof(label), "%u", options_[i]);
        Ui::drawButton(tft, optionRect(i), label, fill, outline, text, false, 2);
    }

    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    char circleLabel[32];
    snprintf(circleLabel, sizeof(circleLabel), "%u", (baseNumber_ * targetPercent_) / 100);
    tft.drawString(circleLabel, CIRCLE_CX, CIRCLE_CY, 2);
    tft.setTextDatum(TL_DATUM);
}

void PercentCircleGame::update(AppContext& host, const TouchPoint& touch) {
    if (flashIndex_ >= 0 && millis() > flashUntil_) {
        flashIndex_ = -1;
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    if (roundComplete_) {
        newRound(host);
        markDirty();
        return;
    }

    if (roundType_ == RoundType::ReadCircle || roundType_ == RoundType::PercentOfNumber) {
        for (uint8_t i = 0; i < 4; ++i) {
            if (optionRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                if (i == correctButton_) {
                    markCorrect(host);
                } else {
                    markWrong();
                    host.beepError();
                }
                markDirty();
                return;
            }
        }
    } else if (roundType_ == RoundType::MakeCircle) {
        if (minusRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (currentPercent_ > 0) {
                currentPercent_ -= 5;
                markDirty();
            }
            return;
        }

        if (plusRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (currentPercent_ < 100) {
                currentPercent_ += 5;
                markDirty();
            }
            return;
        }

        if (okRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (currentPercent_ == targetPercent_) {
                markCorrect(host);
            } else {
                markWrong();
                host.beepError();
            }
            markDirty();
            return;
        }
    }
}

void PercentCircleGame::render(AppContext& host) {
    TFT_eSPI& tft = host.display();

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());

        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(promptBuffer_, 200, 60, 4);
        tft.setTextDatum(TL_DATUM);
    }

    if (roundType_ == RoundType::ReadCircle) {
        drawReadCircleMode(tft);
    } else if (roundType_ == RoundType::MakeCircle) {
        drawMakeCircleMode(tft);
    } else {
        drawPercentOfNumberMode(tft);
    }

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    char scoreStr[48];
    snprintf(scoreStr, sizeof(scoreStr), "Score: %u  Streak: %u  Lvl: %u", score_, streak_, level());
    tft.drawString(scoreStr, 8, 226, 1);

    if (roundComplete_) {
        tft.fillRoundRect(52, 96, 216, 68, 8, Ui::panel());
        tft.drawRoundRect(52, 96, 216, 68, 8, Ui::success());
        tft.setTextColor(Ui::success(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Correct!", 160, 120, 4);
        tft.drawString("Tap next", 160, 150, 2);
        tft.setTextDatum(TL_DATUM);
    } else if (flashIndex_ >= 0) {
        tft.setTextColor(Ui::error(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Try again", 160, 60, 2);
        tft.setTextDatum(TL_DATUM);
    }

    clearDirty();
}
