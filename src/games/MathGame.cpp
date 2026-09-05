#include "MathGame.h"
#include "engine/AppRegistry.h"

namespace {
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
        /* A new sum, four new options and the prompt back to "Tap the answer".
         * That is a layout change -- the equation panel is static, so only a
         * full repaint replaces it. */
        markFullDirty();
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

void MathGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    /* The equation belongs to the question, and a new question is a full
     * repaint -- so this panel is static even though it is not constant. It is
     * also the largest single block on the screen, which is what made
     * repainting it to recolour one button worth stopping. */
    char equation[24];
    const char symbol = operation_ == Operation::Add ? '+' : '-';
    snprintf(equation, sizeof(equation), "%d %c %d = ?",
             static_cast<int>(left_), symbol, static_cast<int>(right_));
    tft.fillRoundRect(26, 76, 268, 54, 8, Ui::panel());
    tft.drawRoundRect(26, 76, 268, 54, 8, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(equation, GAME_CANVAS_WIDTH / 2, 103, 4);
    tft.setTextDatum(TL_DATUM);

    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;   // nothing painted there yet
    }
    drawnScore_ = 0xFFFF;
    drawnStreak_ = 0xFFFF;
    drawnAnswered_ = !answered_;   // force the feedback line on the first pass
    drawnHeader_ = false;
}

void MathGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    /* Four counters, two of them TR_DATUM. Level and the clock only move with
     * a new question, but Correct and Streak change on an answer, and Streak
     * resets to 0 -- so the pair is cleared before either is written and the
     * right-hand rect is measured leftward from the margin, where a shrinking
     * right-aligned string leaves its stale characters. */
    if (!drawnHeader_ || score_ != drawnScore_ || streak_ != drawnStreak_) {
        tft.fillRect(10, 33, 150, 36, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 10 - 170, 33, 170, 36, Ui::bg());
        char buf[32];
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(level()));
        tft.drawString(buf, 10, 35, 2);
        snprintf(buf, sizeof(buf), "Correct %u  %s", static_cast<unsigned>(score_),
                 formatSeconds(elapsedSeconds()).c_str());
        tft.drawString(buf, 10, 52, 1);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Streak %u", static_cast<unsigned>(streak_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 35, 2);
        if (bestCorrect_ > 0) {
            snprintf(buf, sizeof(buf), "Best %u / %s", static_cast<unsigned>(bestCorrect_),
                     formatSeconds(bestSeconds_).c_str());
        } else {
            snprintf(buf, sizeof(buf), "Best --");
        }
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 52, 1);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnStreak_ = streak_;
        drawnHeader_ = true;
    }

    /* Only the buttons whose colour changed. drawButton fills its rect
     * opaquely, so a recolour erases what was there and none of this needs a
     * clear of its own. */
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
        char label[12];
        snprintf(label, sizeof(label), "%d", static_cast<int>(options_[i]));
        Ui::drawButton(tft, answerRect(i), label, fill, TFT_DARKGREY, text, false, 4);
        drawnButton_[i] = state;
    }

    /* "Tap the answer" and "Green is correct - tap next" are different lengths,
     * so the line is cleared before either is written. */
    if (answered_ != drawnAnswered_) {
        /* Stops at 143, one row above answerRect(0) at y=144. A 20px rect from
         * 126 would reach into the top two buttons and erase two rows of them
         * -- and this block runs AFTER the button loop, so nothing would put
         * them back on a repaint where the buttons themselves had not changed. */
        tft.fillRect(20, 125, GAME_CANVAS_WIDTH - 40, 18, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        if (answered_) {
            tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
            tft.drawString(selected_ == correctButton_ ? "Correct - tap for next"
                                                       : "Green is correct - tap next",
                           GAME_CANVAS_WIDTH / 2, 136, 2);
        } else {
            tft.setTextColor(YELLOW, Ui::bg());
            tft.drawString("Tap the answer", GAME_CANVAS_WIDTH / 2, 136, 2);
        }
        tft.setTextDatum(TL_DATUM);
        drawnAnswered_ = answered_;
    }
}
