#include "NumberLineGame.h"

const char* NumberLineGame::title() const { return "Number Line"; }

int16_t NumberLineGame::numToX(uint8_t n) const {
    return static_cast<int16_t>(22 + (n * 276) / 10);
}

Rect NumberLineGame::answerRect(uint8_t i) const {
    return Rect{static_cast<int16_t>(14 + i * 74), 194, 64, 38};
}

void NumberLineGame::makeOptions() {
    const uint8_t answer = isAdd_ ? n1_ + n2_ : n1_ - n2_;
    correctBtn_ = static_cast<uint8_t>(random(4));
    for (uint8_t i = 0; i < 4; ++i) options_[i] = 0;
    options_[correctBtn_] = answer;
    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctBtn_) continue;
        uint8_t c;
        bool unique;
        do {
            const int lo = max(0, (int)answer - 3);
            const int hi = min(10, (int)answer + 3);
            c = static_cast<uint8_t>((lo == hi) ? lo : random(lo, hi + 1));
            unique = (c != answer);
            for (uint8_t j = 0; j < i; ++j) if (options_[j] == c) { unique = false; break; }
        } while (!unique);
        options_[i] = c;
    }
}

void NumberLineGame::newQuestion() {
    n1_        = static_cast<uint8_t>(1 + random(5)); // 1-5
    n2_        = static_cast<uint8_t>(1 + random(4)); // 1-4
    isAdd_     = static_cast<bool>(random(2));
    if (!isAdd_) {
        if (n1_ < n2_ + 1) n1_ = n2_ + 1;
        if (n1_ > 9) n1_ = 9;
    }
    animStep_  = 0;
    selected_  = -1;
    phase_     = Phase::Pause;
    nextAt_    = millis() + 1100UL;
    feedbackUntil_ = 0;
    makeOptions();
}

void NumberLineGame::begin(GameHost&) {
    score_ = 0; rounds_ = 0;
    newQuestion();
    markDirty();
}

void NumberLineGame::update(GameHost& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (phase_ == Phase::Pause && now >= nextAt_) {
        phase_ = Phase::Jumping;
        nextAt_ = now + 650UL;
        markDirty(); return;
    }
    if (phase_ == Phase::Jumping && now >= nextAt_) {
        ++animStep_;
        if (animStep_ >= n2_) { phase_ = Phase::Question; }
        else { nextAt_ = now + 650UL; }
        markDirty(); return;
    }
    if (phase_ == Phase::Feedback && now >= feedbackUntil_) {
        newQuestion(); markDirty(); return;
    }
    if (!touch.justPressed) return;
    if (phase_ != Phase::Question) return;
    for (uint8_t i = 0; i < 4; ++i) {
        if (answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            selected_ = static_cast<int8_t>(i);
            ++rounds_;
            lastCorrect_ = (i == correctBtn_);
            if (lastCorrect_) { ++score_; host.board().beepOk(); }
            else host.board().beepError();
            feedbackUntil_ = now + 1500UL;
            phase_ = Phase::Feedback;
            markDirty(); return;
        }
    }
}

void NumberLineGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(tft, title());

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(String(score_) + "/" + rounds_, SCREEN_WIDTH - 8, 34, 2);

    // Equation display
    const uint8_t curPos = (phase_ == Phase::Pause) ? n1_
                         : (isAdd_ ? n1_ + animStep_ : n1_ - animStep_);
    const uint8_t answer = isAdd_ ? n1_ + n2_ : n1_ - n2_;

    tft.setTextDatum(MC_DATUM);
    if (phase_ == Phase::Feedback && lastCorrect_) {
        tft.setTextColor(Ui::success(), Ui::bg());
        tft.drawString(String(n1_) + (isAdd_ ? " + " : " - ") + n2_ + " = " + answer, SCREEN_WIDTH/2, 52, 4);
    } else {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(String(n1_) + (isAdd_ ? " + " : " - ") + n2_ + " = ?", SCREEN_WIDTH/2, 52, 4);
    }

    // Instruction
    tft.setTextColor(Ui::muted(), Ui::bg());
    if (phase_ == Phase::Pause) {
        tft.drawString(isAdd_ ? "Watch the frog jump right!" : "Watch the frog jump left!", SCREEN_WIDTH/2, 78, 2);
    } else if (phase_ == Phase::Jumping) {
        tft.drawString(String("Jump ") + animStep_ + " of " + n2_, SCREEN_WIDTH/2, 78, 2);
    }

    // Jump arcs (show previous jumps)
    constexpr int16_t lineY = 162;
    if (phase_ == Phase::Jumping || phase_ == Phase::Question || phase_ == Phase::Feedback) {
        for (uint8_t step = 0; step < animStep_; ++step) {
            const uint8_t from = isAdd_ ? n1_ + step : n1_ - step;
            const uint8_t to   = isAdd_ ? from + 1   : from - 1;
            const int16_t x1 = numToX(from), x2 = numToX(to);
            // Real semicircular hop. This used to be two straight lines meeting
            // at a midpoint, which drew a triangle rather than an arc.
            Ui::drawHopArc(tft, x1, x2, lineY, 20, Ui::rgb(255, 200, 0));
        }
    }

    // Number line
    tft.drawLine(18, lineY, 302, lineY, Ui::text());
    tft.drawLine(300, static_cast<int16_t>(lineY - 5), 302, lineY, Ui::text());
    tft.drawLine(300, static_cast<int16_t>(lineY + 5), 302, lineY, Ui::text());
    tft.setTextDatum(TC_DATUM);
    for (uint8_t i = 0; i <= 10; ++i) {
        const int16_t tx = numToX(i);
        tft.drawLine(tx, static_cast<int16_t>(lineY - 6), tx, static_cast<int16_t>(lineY + 6), Ui::text());
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(String(i), tx, static_cast<int16_t>(lineY + 9), 1);
    }
    // Dot / frog at current position
    if (curPos <= 10) {
        const int16_t dx = numToX(curPos);
        tft.fillCircle(dx, static_cast<int16_t>(lineY - 14), 9, Ui::rgb(80, 230, 80));
        tft.drawCircle(dx, static_cast<int16_t>(lineY - 14), 9, Ui::text());
        tft.setTextColor(TFT_BLACK, Ui::rgb(80, 230, 80));
        tft.setTextDatum(MC_DATUM);
        tft.drawString(String(curPos), dx, static_cast<int16_t>(lineY - 14), 1);
    }

    // Answer buttons
    if (phase_ == Phase::Question || phase_ == Phase::Feedback) {
        for (uint8_t i = 0; i < 4; ++i) {
            uint16_t fill = Ui::rgb(36, 132, 204);
            uint16_t tc   = TFT_WHITE;
            if (phase_ == Phase::Feedback) {
                if (i == correctBtn_)                            { fill = Ui::success(); tc = TFT_BLACK; }
                else if (i == static_cast<uint8_t>(selected_)) { fill = Ui::error();   tc = TFT_BLACK; }
            }
            Ui::drawButton(tft, answerRect(i), String(options_[i]), fill, TFT_DARKGREY, tc, false, 4);
        }
    }
    tft.setTextDatum(TL_DATUM);
}
