#include "MathGame.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t YELLOW = 0xFEC0;
}

const char* MathGame::title() const {
    return "Math";
}

void MathGame::begin(GameHost& host) {
    score_ = 0;
    streak_ = 0;
    bestCorrect_ = static_cast<uint16_t>(host.board().getScore("mathBest", 0));
    bestSeconds_ = static_cast<uint16_t>(host.board().getScore("mathTime", 0));
    startedAt_ = millis();
    newQuestion();
    markDirty();
}

uint8_t MathGame::level() const {
    return min<uint8_t>(5, 1 + score_ / 5);
}

Rect MathGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152), static_cast<int16_t>(144 + row * 46), 132, 38};
}

bool MathGame::optionExists(int16_t value, uint8_t upTo) const {
    for (uint8_t i = 0; i < upTo; ++i) {
        if (options_[i] == value) {
            return true;
        }
    }
    return false;
}

uint16_t MathGame::elapsedSeconds() const {
    return static_cast<uint16_t>((millis() - startedAt_) / 1000UL);
}

String MathGame::formatSeconds(uint16_t seconds) const {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%u:%02u", seconds / 60U, seconds % 60U);
    return String(buffer);
}

void MathGame::updateBest(GameHost& host) {
    const uint16_t elapsed = elapsedSeconds();
    if (score_ > bestCorrect_ || (score_ == bestCorrect_ && (bestSeconds_ == 0 || elapsed < bestSeconds_))) {
        bestCorrect_ = score_;
        bestSeconds_ = elapsed;
        host.board().setScore("mathBest", bestCorrect_);
        host.board().setScore("mathTime", bestSeconds_);
    }
}

void MathGame::newQuestion() {
    const uint8_t currentLevel = level();
    selected_ = -1;
    answered_ = false;

    switch (currentLevel) {
        case 1:
            operation_ = Operation::Add;
            left_ = random(1, 10);
            right_ = random(1, 10);
            break;
        case 2:
            operation_ = random(2) == 0 ? Operation::Add : Operation::Subtract;
            left_ = random(2, 13);
            right_ = random(1, 10);
            break;
        case 3:
            operation_ = random(2) == 0 ? Operation::Add : Operation::Subtract;
            left_ = random(10, 30);
            right_ = random(1, 10);
            break;
        case 4:
            operation_ = random(2) == 0 ? Operation::Add : Operation::Subtract;
            left_ = random(10, 50);
            right_ = random(10, 50);
            break;
        default:
            operation_ = random(2) == 0 ? Operation::Add : Operation::Subtract;
            left_ = random(10, 100);
            right_ = random(10, 100);
            break;
    }

    if (operation_ == Operation::Subtract && right_ > left_) {
        const int16_t tmp = left_;
        left_ = right_;
        right_ = tmp;
    }
    answer_ = operation_ == Operation::Add ? left_ + right_ : left_ - right_;
    makeOptions();
}

void MathGame::makeOptions() {
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
        while ((candidate == answer_ || optionExists(candidate, i)) && attempts < 40) {
            const int16_t spread = level() <= 2 ? 5 : (level() <= 4 ? 12 : 20);
            candidate = answer_ + random(-spread, spread + 1);
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

void MathGame::update(GameHost& host, const TouchPoint& touch) {
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
                host.board().beepOk();
            } else {
                streak_ = 0;
                host.board().beepError();
            }
            markDirty();
            return;
        }
    }
}

void MathGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Level ") + level(), 10, 35, 2);
    tft.drawString(String("Correct ") + score_ + "  " + formatSeconds(elapsedSeconds()), 10, 52, 1);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Streak ") + streak_, SCREEN_WIDTH - 10, 35, 2);
    tft.drawString(bestCorrect_ > 0 ? String("Best ") + bestCorrect_ + " / " + formatSeconds(bestSeconds_) : "Best --", SCREEN_WIDTH - 10, 52, 1);

    const char symbol = operation_ == Operation::Add ? '+' : '-';
    const String equation = String(left_) + " " + symbol + " " + right_ + " = ?";
    tft.fillRoundRect(26, 76, 268, 54, 8, Ui::panel());
    tft.drawRoundRect(26, 76, 268, 54, 8, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(equation, SCREEN_WIDTH / 2, 103, 4);

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
        tft.drawString(selected_ == correctButton_ ? "Correct - tap for next" : "Green is correct - tap next", SCREEN_WIDTH / 2, 136, 2);
    } else {
        tft.setTextColor(YELLOW, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Tap the answer", SCREEN_WIDTH / 2, 136, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
