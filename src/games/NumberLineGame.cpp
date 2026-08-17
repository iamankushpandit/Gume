#include "NumberLineGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr int16_t LINE_MARGIN = 22;
constexpr int16_t LINE_Y = TOP_BAR_HEIGHT + 132;
constexpr int16_t EQUATION_Y = TOP_BAR_HEIGHT + 22;
constexpr int16_t HINT_Y = TOP_BAR_HEIGHT + 48;

constexpr AppMetadata NUMBER_LINE_METADATA = {
    "numberline",
    "Number Line",
    nullptr,
    "jump to number",
    "Number Line",
    "Hop along a number line.",
    nullptr,
    LauncherIcon::NumberLine,
    20,
    true,
};
}

const AppMetadata& numberLineAppMetadata() {
    return NUMBER_LINE_METADATA;
}

const char* NumberLineGame::title() const {
    return numberLineAppMetadata().screenTitle != nullptr
        ? numberLineAppMetadata().screenTitle
        : numberLineAppMetadata().title;
}

int16_t NumberLineGame::numToX(const Ui::Frame& f, uint8_t n) const {
    /* The line spans the panel less a margin either end, so 0 and 10 keep
     * their ticks on screen at 240px as well as 320. */
    const int16_t span = static_cast<int16_t>(f.w - 2 * LINE_MARGIN);
    return static_cast<int16_t>(LINE_MARGIN + (static_cast<int32_t>(n) * span) / 10);
}

Rect NumberLineGame::answerBand(const Ui::Frame& f) const {
    const uint8_t cols = f.tall() ? 2 : 4;
    const uint8_t rows = static_cast<uint8_t>(4 / cols);
    const int16_t h = static_cast<int16_t>(rows * 38 + (rows - 1) * 8);
    return Rect{14, static_cast<int16_t>(f.h - h - 8),
                static_cast<int16_t>(f.w - 28), h};
}

Rect NumberLineGame::answerRect(const Ui::Frame& f, uint8_t i) const {
    /* Four 64px buttons on a 74px pitch need 296px. Landscape has it and
     * portrait does not, so portrait pairs them instead of shrinking them. */
    const uint8_t cols = f.tall() ? 2 : 4;
    const uint8_t rows = static_cast<uint8_t>(4 / cols);
    return Ui::gridCell(answerBand(f), cols, rows, i, 8);
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

void NumberLineGame::begin(AppContext&) {
    score_ = 0; rounds_ = 0;
    newQuestion();
    markDirty();
}

void NumberLineGame::update(AppContext& host, const TouchPoint& touch) {
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
        if (answerRect(Ui::frame(host.display()), i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            selected_ = static_cast<int8_t>(i);
            ++rounds_;
            lastCorrect_ = (i == correctBtn_);
            if (lastCorrect_) { ++score_; host.beepOk(); }
            else host.beepError();
            feedbackUntil_ = now + 1500UL;
            phase_ = Phase::Feedback;
            markDirty(); return;
        }
    }
}

void NumberLineGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(String(score_) + "/" + rounds_, f.w - 8, TOP_BAR_HEIGHT + 4, 2);

    // Equation display
    const uint8_t curPos = (phase_ == Phase::Pause) ? n1_
                         : (isAdd_ ? n1_ + animStep_ : n1_ - animStep_);
    const uint8_t answer = isAdd_ ? n1_ + n2_ : n1_ - n2_;

    tft.setTextDatum(MC_DATUM);
    if (phase_ == Phase::Feedback && lastCorrect_) {
        tft.setTextColor(Ui::success(), Ui::bg());
        tft.drawString(String(n1_) + (isAdd_ ? " + " : " - ") + n2_ + " = " + answer, f.cx(), EQUATION_Y, 4);
    } else {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(String(n1_) + (isAdd_ ? " + " : " - ") + n2_ + " = ?", f.cx(), EQUATION_Y, 4);
    }

    // Instruction
    tft.setTextColor(Ui::muted(), Ui::bg());
    if (phase_ == Phase::Pause) {
        tft.drawString(isAdd_ ? "Watch the frog jump right!" : "Watch the frog jump left!", f.cx(), HINT_Y, 2);
    } else if (phase_ == Phase::Jumping) {
        tft.drawString(String("Jump ") + animStep_ + " of " + n2_, f.cx(), HINT_Y, 2);
    }

    // Jump arcs (show previous jumps)
    const int16_t lineY = static_cast<int16_t>(
        f.tall() ? (TOP_BAR_HEIGHT + (answerBand(f).y - TOP_BAR_HEIGHT) / 2 + 20) : LINE_Y);
    if (phase_ == Phase::Jumping || phase_ == Phase::Question || phase_ == Phase::Feedback) {
        for (uint8_t step = 0; step < animStep_; ++step) {
            const uint8_t from = isAdd_ ? n1_ + step : n1_ - step;
            const uint8_t to   = isAdd_ ? from + 1   : from - 1;
            const int16_t x1 = numToX(f, from), x2 = numToX(f, to);
            // Real semicircular hop. This used to be two straight lines meeting
            // at a midpoint, which drew a triangle rather than an arc.
            Ui::drawHopArc(tft, x1, x2, lineY, 20, Ui::rgb(255, 200, 0));
        }
    }

    // Number line
    const int16_t lineRight = static_cast<int16_t>(f.w - 18);
    tft.drawLine(18, lineY, lineRight, lineY, Ui::text());
    tft.drawLine(lineRight - 2, static_cast<int16_t>(lineY - 5), lineRight, lineY, Ui::text());
    tft.drawLine(lineRight - 2, static_cast<int16_t>(lineY + 5), lineRight, lineY, Ui::text());
    tft.setTextDatum(TC_DATUM);
    for (uint8_t i = 0; i <= 10; ++i) {
        const int16_t tx = numToX(f, i);
        tft.drawLine(tx, static_cast<int16_t>(lineY - 6), tx, static_cast<int16_t>(lineY + 6), Ui::text());
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(String(i), tx, static_cast<int16_t>(lineY + 9), 1);
    }
    // Dot / frog at current position
    if (curPos <= 10) {
        const int16_t dx = numToX(f, curPos);
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
            Ui::drawButton(tft, answerRect(f, i), String(options_[i]), fill, TFT_DARKGREY, tc, false, 4);
        }
    }
    tft.setTextDatum(TL_DATUM);
}
