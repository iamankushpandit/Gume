#include "PercentCircleGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t PERCENT_FILL = 0xFCA8;     // Ui::rgb(255, 202, 84) - golden/orange
constexpr uint16_t PERCENT_EMPTY = 0x39EF;    // similar to surface
constexpr uint16_t PERCENT_OUTLINE = 0x2478;  // Ui::rgb(36, 132, 204) - blue
constexpr int16_t CIRCLE_CX_WIDE = 96;
constexpr int16_t CIRCLE_CY = 130;
constexpr int16_t CIRCLE_RADIUS = 62;

constexpr AppScoreInfo PERCENT_SCORE = {
    "percent", "Percent", "pctBest", "pts", false
};

constexpr AppMetadata PERCENT_METADATA = {
    "percent",
    "Percent",
    nullptr,
    "circle parts",
    "Percent",
    "Percentages on a circle.",
    &PERCENT_SCORE,
    LauncherIcon::Percent,
    26,
    true,
};
}

const AppMetadata& percentCircleAppMetadata() {
    return PERCENT_METADATA;
}

const char* PercentCircleGame::title() const {
    return percentCircleAppMetadata().screenTitle != nullptr
        ? percentCircleAppMetadata().screenTitle
        : percentCircleAppMetadata().title;
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
    host.saveBestScore(percentCircleAppMetadata().score->bestKey, score_, false);
    roundComplete_ = true;
    flashIndex_ = -1;
    host.beepOk();
}

void PercentCircleGame::markWrong() {
    streak_ = 0;
    flashIndex_ = correctButton_;
    flashUntil_ = millis() + 500UL;
}

int16_t PercentCircleGame::circleCx(const Ui::Frame& f) const {
    /* Landscape keeps the circle left of the controls; portrait centres it,
     * because the controls have moved underneath. */
    return f.tall() ? f.cx() : CIRCLE_CX_WIDE;
}

Rect PercentCircleGame::controlsRect(const Ui::Frame& f) const {
    if (f.tall()) {
        return Rect{10, static_cast<int16_t>(CIRCLE_CY + CIRCLE_RADIUS + 16),
                    static_cast<int16_t>(f.w - 20), 88};
    }
    return Rect{164, static_cast<int16_t>(TOP_BAR_HEIGHT + 66),
                static_cast<int16_t>(f.w - 174), 88};
}

Rect PercentCircleGame::stepperBand(const Ui::Frame& f) const {
    const Rect c = controlsRect(f);
    return Rect{c.x, static_cast<int16_t>(c.y + 56), c.w, 34};
}

Rect PercentCircleGame::optionRect(const Ui::Frame& f, uint8_t index) const {
    return Ui::gridCell(controlsRect(f), 2, 2, index, 6);
}

/* minus / value / plus / ok share one four-wide row, which lands back on the
 * authored x=164/200/240/276 at y=152 in landscape. */
Rect PercentCircleGame::minusRect(const Ui::Frame& f) const {
    return Ui::gridCell(stepperBand(f), 4, 1, 0, 2);
}

Rect PercentCircleGame::valueRect(const Ui::Frame& f) const {
    return Ui::gridCell(stepperBand(f), 4, 1, 1, 2);
}

Rect PercentCircleGame::plusRect(const Ui::Frame& f) const {
    return Ui::gridCell(stepperBand(f), 4, 1, 2, 2);
}

Rect PercentCircleGame::okRect(const Ui::Frame& f) const {
    return Ui::gridCell(stepperBand(f), 4, 1, 3, 2);
}

void PercentCircleGame::fillSlice(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, float startAngle, float endAngle, uint16_t color) const {
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

void PercentCircleGame::drawCircle(Ui::Renderer& tft, const Ui::Frame& f, uint8_t percent, bool highlight) const {
    tft.fillCircle(circleCx(f), CIRCLE_CY, CIRCLE_RADIUS + 3, highlight ? Ui::warning() : Ui::surface());
    tft.fillCircle(circleCx(f), CIRCLE_CY, CIRCLE_RADIUS, PERCENT_EMPTY);

    if (percent > 0) {
        const float fillAngle = (percent / 100.0f) * TWO_PI;
        fillSlice(tft, circleCx(f), CIRCLE_CY, CIRCLE_RADIUS, -PI / 2.0f, -PI / 2.0f + fillAngle, PERCENT_FILL);
    }

    tft.drawCircle(circleCx(f), CIRCLE_CY, CIRCLE_RADIUS, PERCENT_OUTLINE);

    for (uint8_t i = 0; i < 10; ++i) {
        const float angle = -PI / 2.0f + i * TWO_PI / 10.0f;
        const int16_t x = circleCx(f) + static_cast<int16_t>(cosf(angle) * CIRCLE_RADIUS);
        const int16_t y = CIRCLE_CY + static_cast<int16_t>(sinf(angle) * CIRCLE_RADIUS);
        tft.drawLine(circleCx(f), CIRCLE_CY, x, y, Ui::rgb(100, 150, 200));
    }
}

void PercentCircleGame::drawReadCircleMode(Ui::Renderer& tft, const Ui::Frame& f) const {
    drawCircle(tft, f, targetPercent_, false);

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
        Ui::drawButton(tft, optionRect(f, i), label, fill, outline, text, false, 2);
    }
}

void PercentCircleGame::drawMakeCircleMode(Ui::Renderer& tft, const Ui::Frame& f) const {
    drawCircle(tft, f, currentPercent_, false);

    tft.fillRoundRect(200, 152, 38, 34, 4, Ui::panel());
    tft.drawRoundRect(200, 152, 38, 34, 4, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    char valueStr[8];
    snprintf(valueStr, sizeof(valueStr), "%u%%", currentPercent_);
    tft.drawString(valueStr, 219, 169, 1);

    Ui::drawPagerButton(tft, minusRect(f), "âˆ’", currentPercent_ > 0);
    Ui::drawPagerButton(tft, plusRect(f), "+", currentPercent_ < 100);

    uint16_t okFill = currentPercent_ == targetPercent_ ? Ui::success() : Ui::panel();
    uint16_t okText = currentPercent_ == targetPercent_ ? TFT_BLACK : Ui::text();
    Ui::drawButton(tft, okRect(f), "âœ“", okFill, Ui::outline(), okText, false, 2);

    tft.setTextDatum(TL_DATUM);
}

void PercentCircleGame::drawPercentOfNumberMode(Ui::Renderer& tft, const Ui::Frame& f) const {
    drawCircle(tft, f, targetPercent_, false);

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
        Ui::drawButton(tft, optionRect(f, i), label, fill, outline, text, false, 2);
    }

    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    char circleLabel[32];
    snprintf(circleLabel, sizeof(circleLabel), "%u", (baseNumber_ * targetPercent_) / 100);
    tft.drawString(circleLabel, circleCx(f), CIRCLE_CY, 2);
    tft.setTextDatum(TL_DATUM);
}

void PercentCircleGame::update(AppContext& host, const TouchPoint& touch) {
    const Ui::Frame f = Ui::frame(host.display());
    if (flashIndex_ >= 0 && millis() > flashUntil_) {
        flashIndex_ = -1;
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    if (roundComplete_) {
        newRound(host);
        markFullDirty();
        return;
    }

    if (roundType_ == RoundType::ReadCircle || roundType_ == RoundType::PercentOfNumber) {
        for (uint8_t i = 0; i < 4; ++i) {
            if (optionRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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
        if (minusRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (currentPercent_ > 0) {
                currentPercent_ -= 5;
                markDirty();
            }
            return;
        }

        if (plusRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (currentPercent_ < 100) {
                currentPercent_ += 5;
                markDirty();
            }
            return;
        }

        if (okRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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
    const Ui::Frame f = Ui::frame(host.display());
    Ui::Renderer& tft = host.display();

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());
    }

    tft.fillRect(0, TOP_BAR_HEIGHT + 18, f.w, 34, Ui::bg());
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(promptBuffer_, f.tall() ? f.cx() : 200, TOP_BAR_HEIGHT + 30, 4);
    tft.setTextDatum(TL_DATUM);

    if (roundType_ == RoundType::ReadCircle) {
        drawReadCircleMode(tft, f);
    } else if (roundType_ == RoundType::MakeCircle) {
        drawMakeCircleMode(tft, f);
    } else {
        drawPercentOfNumberMode(tft, f);
    }

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.fillRect(0, static_cast<int16_t>(f.h - 18), f.w, 18, Ui::bg());
    char scoreStr[48];
    snprintf(scoreStr, sizeof(scoreStr), "Score: %u  Streak: %u  Lvl: %u", score_, streak_, level());
    tft.drawString(scoreStr, 8, static_cast<int16_t>(f.h - 14), 1);

    if (roundComplete_) {
        const Rect done = Ui::centreIn(Rect{0, 0, f.w, f.h}, 216, 68);
        tft.fillRoundRect(done.x, done.y, done.w, done.h, 8, Ui::panel());
        tft.drawRoundRect(done.x, done.y, done.w, done.h, 8, Ui::success());
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
