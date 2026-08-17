#include "MathGame.h"
#include "engine/AppRegistry.h"

namespace {
/* Rows measured down from the top bar; the answer grid takes everything left
 * over, so it grows with the panel instead of ending at a hardcoded 232. */
constexpr int16_t HUD_TOP = TOP_BAR_HEIGHT + 5;
constexpr int16_t HUD_SECOND = TOP_BAR_HEIGHT + 22;
constexpr int16_t EQUATION_TOP = TOP_BAR_HEIGHT + 46;
constexpr int16_t EQUATION_H = 54;
constexpr int16_t HINT_Y = TOP_BAR_HEIGHT + 106;
constexpr int16_t ANSWER_TOP = TOP_BAR_HEIGHT + 114;
constexpr uint8_t ANSWER_COUNT = 4;

constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t YELLOW = 0xFEC0;

constexpr AppScoreInfo MATH_SCORE = {
    "math", "Math", "mathBest", "pts", false
};

constexpr AppMetadata MATH_METADATA = {
    "math",
    "Math",
    nullptr,
    "addition & subtraction",
    "Math",
    "Practice arithmetic with timed rounds.",
    &MATH_SCORE,
    LauncherIcon::Math,
    2,
    true,
};
}

const AppMetadata& mathAppMetadata() {
    return MATH_METADATA;
}

const char* MathGame::title() const {
    return mathAppMetadata().title;
}

void MathGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestCorrect_ = static_cast<uint16_t>(host.getScore(mathAppMetadata().score->bestKey, 0));
    bestSeconds_ = static_cast<uint16_t>(host.getScore("mathTime", 0));
    startedAt_ = millis();
    newQuestion();
    markDirty();
}

uint8_t MathGame::level() const {
    return min<uint8_t>(5, 1 + score_ / 5);
}

Rect MathGame::equationRect(const Ui::Frame& f) const {
    return Rect{26, EQUATION_TOP, static_cast<int16_t>(f.w - 52), EQUATION_H};
}

Rect MathGame::answerBand(const Ui::Frame& f) const {
    return Rect{18, ANSWER_TOP, static_cast<int16_t>(f.w - 36),
                static_cast<int16_t>(f.h - ANSWER_TOP - 8)};
}

Rect MathGame::answerRect(const Ui::Frame& f, uint8_t index) const {
    const uint8_t cols = Ui::answerColumns(f, ANSWER_COUNT);
    const uint8_t rows = static_cast<uint8_t>(ANSWER_COUNT / cols);
    return Ui::gridCell(answerBand(f), cols, rows, index, 8);
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

void MathGame::updateBest(AppContext& host) {
    const uint16_t elapsed = elapsedSeconds();
    if (score_ > bestCorrect_ || (score_ == bestCorrect_ && (bestSeconds_ == 0 || elapsed < bestSeconds_))) {
        bestCorrect_ = score_;
        bestSeconds_ = elapsed;
        host.setScore(mathAppMetadata().score->bestKey, bestCorrect_);
        host.setScore("mathTime", bestSeconds_);
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

void MathGame::update(AppContext& host, const TouchPoint& touch) {
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

void MathGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Level ") + level(), 10, HUD_TOP, 2);
    tft.drawString(String("Correct ") + score_ + "  " + formatSeconds(elapsedSeconds()), 10, HUD_SECOND, 1);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Streak ") + streak_, f.w - 10, HUD_TOP, 2);
    tft.drawString(bestCorrect_ > 0 ? String("Best ") + bestCorrect_ + " / " + formatSeconds(bestSeconds_) : "Best --", f.w - 10, HUD_SECOND, 1);

    const char symbol = operation_ == Operation::Add ? '+' : '-';
    const String equation = String(left_) + " " + symbol + " " + right_ + " = ?";
    const Rect eq = equationRect(f);
    tft.fillRoundRect(eq.x, eq.y, eq.w, eq.h, 8, Ui::panel());
    tft.drawRoundRect(eq.x, eq.y, eq.w, eq.h, 8, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(equation, f.cx(), eq.y + eq.h / 2, 4);

    for (uint8_t i = 0; i < ANSWER_COUNT; ++i) {
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
        Ui::drawButton(tft, answerRect(f, i), String(options_[i]), fill, TFT_DARKGREY, text, false, 4);
    }

    if (answered_) {
        tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(selected_ == correctButton_ ? "Correct - tap for next" : "Green is correct - tap next", f.cx(), HINT_Y, 2);
    } else {
        tft.setTextColor(YELLOW, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Tap the answer", f.cx(), HINT_Y, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
