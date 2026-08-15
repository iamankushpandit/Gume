#include "ObjectAddGame.h"

namespace {
constexpr uint16_t COL_FIRST   = 0x24BD; // blue
constexpr uint16_t COL_SECOND  = 0x37F0; // green
constexpr uint16_t COL_REMOVED = 0xF9EA; // red/orange for subtraction
constexpr uint16_t OBJ_BTN     = 0x24BD;
constexpr uint16_t OBJ_CORRECT = 0x37F0;
constexpr uint16_t OBJ_WRONG   = 0xF9EA;
}

const char* ObjectAddGame::title() const { return "Shape Arith"; }

Rect ObjectAddGame::leftPanel()  const { return Rect{8,   50, 132, 110}; }
Rect ObjectAddGame::rightPanel() const { return Rect{180, 50, 132, 110}; }
Rect ObjectAddGame::answerRect(uint8_t i) const {
    return Rect{static_cast<int16_t>(15 + i * 76), 172, 62, 42};
}

void ObjectAddGame::objPos(const Rect& p, uint8_t idx, int16_t& cx, int16_t& cy) const {
    const uint8_t col = idx % 4;
    const uint8_t row = idx / 4;
    cx = static_cast<int16_t>(p.x + 18 + col * 28);
    cy = static_cast<int16_t>(p.y + 26 + row * 32);
}

void ObjectAddGame::drawObject(TFT_eSPI& tft, int16_t cx, int16_t cy, uint16_t color) const {
    switch (shape_ % 4) {
        case 0:
            tft.fillCircle(cx, cy, 12, color);
            tft.drawCircle(cx, cy, 12, TFT_DARKGREY);
            break;
        case 1:
            tft.fillRoundRect(cx - 12, cy - 12, 24, 24, 3, color);
            tft.drawRoundRect(cx - 12, cy - 12, 24, 24, 3, TFT_DARKGREY);
            break;
        case 2:
            Ui::drawTriangleShape(tft, cx, cy, 13, color, true);
            break;
        case 3:
            Ui::drawStarShape(tft, cx, cy, 12, color, true);
            break;
    }
}

void ObjectAddGame::makeOptions() {
    const uint8_t answer = (op_ == OpType::Add) ? (n1_ + n2_) : (n1_ - n2_);
    correctBtn_ = static_cast<uint8_t>(random(4));
    for (uint8_t i = 0; i < 4; ++i) options_[i] = 0;
    options_[correctBtn_] = answer;
    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctBtn_) continue;
        uint8_t c;
        bool unique;
        do {
            const int lo = max(1, static_cast<int>(answer) - 3);
            const int hi = min(12, static_cast<int>(answer) + 4);
            c = static_cast<uint8_t>(random(lo, hi + 1));
            unique = (c != answer);
            for (uint8_t j = 0; j < i; ++j) if (options_[j] == c) { unique = false; break; }
        } while (!unique);
        options_[i] = c;
    }
}

void ObjectAddGame::newQuestion() {
    op_    = static_cast<OpType>(random(2));
    shape_ = (shape_ + 1) % 4;
    if (op_ == OpType::Add) {
        n1_ = static_cast<uint8_t>(1 + random(4));  // 1-4
        n2_ = static_cast<uint8_t>(1 + random(4));  // 1-4
    } else {
        n2_ = static_cast<uint8_t>(1 + random(4));  // 1-4
        n1_ = static_cast<uint8_t>(n2_ + 1 + random(4)); // n1 > n2
        if (n1_ > 8) n1_ = 8;
    }
    animCount_   = 0;
    flashOn_     = false;
    animNextAt_  = millis() + 900UL;
    feedbackUntil_ = 0;
    selected_    = -1;
    if (op_ == OpType::Add) {
        phase_ = Phase::Showing;
    } else {
        // Subtraction: show all n1 shapes first, then flash n2 to remove
        phase_ = Phase::Showing;
    }
    makeOptions();
}

void ObjectAddGame::begin(AppContext&) {
    score_ = 0; rounds_ = 0; shape_ = 0;
    newQuestion();
    markDirty();
}

void ObjectAddGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (phase_ == Phase::Showing && now >= animNextAt_) {
        phase_      = (op_ == OpType::Add) ? Phase::AnimIn : Phase::Flashing;
        animNextAt_ = now + 450UL;
        markDirty();
        return;
    }
    if (phase_ == Phase::AnimIn && now >= animNextAt_) {
        ++animCount_;
        if (animCount_ >= n2_) {
            phase_ = Phase::Question;
        } else {
            animNextAt_ = now + 450UL;
        }
        markDirty();
        return;
    }
    // Subtraction: flash the shapes to be removed (toggle every 300ms, 3 times)
    if (phase_ == Phase::Flashing && now >= animNextAt_) {
        flashOn_ = !flashOn_;
        ++animCount_;
        if (animCount_ >= 6) { // 3 full flashes, ends with flashOn_ == false
            phase_ = Phase::Question;
            flashOn_ = false;
            animNextAt_ = now + 1800UL;   // rest before the first reminder blink
        } else {
            animNextAt_ = now + 300UL;
        }
        markDirty();
        return;
    }

    /* Keep the "these ones went away" blink looping for as long as the question
     * is on screen. A 4-year-old who looks away mid-animation would otherwise
     * never see it again. Long rest, short blink: the shapes stay gone most of
     * the time so counting what is LEFT is still easy. */
    if (phase_ == Phase::Question && op_ == OpType::Subtract && now >= animNextAt_) {
        flashOn_ = !flashOn_;
        animNextAt_ = now + (flashOn_ ? 400UL : 1800UL);
        markDirty();
        return;
    }
    if (phase_ == Phase::Feedback && now >= feedbackUntil_) {
        newQuestion();
        markDirty();
        return;
    }

    if (!touch.justPressed) return;

    if (phase_ == Phase::Question) {
        for (uint8_t i = 0; i < 4; ++i) {
            if (answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                selected_ = static_cast<int8_t>(i);
                ++rounds_;
                if (i == correctBtn_) { ++score_; host.beepOk(); }
                else { host.beepError(); }
                feedbackUntil_ = now + 1400UL;
                phase_ = Phase::Feedback;
                markDirty();
                return;
            }
        }
    }
}

void ObjectAddGame::render(AppContext& host) {
    TFT_eSPI& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    // Score + problem text
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String(score_) + "/" + rounds_, SCREEN_WIDTH - 8, 34, 2);

    const char* shapeName = (shape_ % 4 == 0) ? "circles" :
                            (shape_ % 4 == 1) ? "squares" :
                            (shape_ % 4 == 2) ? "triangles" : "stars";
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String(n1_) + (op_ == OpType::Add ? " + " : " - ") + n2_ + " " + shapeName, 8, 34, 2);

    // Left panel (first group - always shown fully)
    const Rect lp = leftPanel();
    tft.fillRoundRect(lp.x, lp.y, lp.w, lp.h, 6, Ui::panel());
    tft.drawRoundRect(lp.x, lp.y, lp.w, lp.h, 6, Ui::outline());
    for (uint8_t i = 0; i < n1_; ++i) {
        int16_t cx, cy; objPos(lp, i, cx, cy);
        // For subtraction: shapes that will be removed flash red/surface, others blue
        uint16_t col = COL_FIRST;
        if (op_ == OpType::Subtract) {
            const bool isRemoved = (i >= n1_ - n2_);
            if (isRemoved && (phase_ == Phase::Flashing || phase_ == Phase::Question)) {
                // Gone by default; blinks red briefly on the reminder loop.
                col = flashOn_ ? COL_REMOVED : Ui::surface();
            } else if (isRemoved) {
                col = Ui::surface(); // already gone
            }
        }
        drawObject(tft, cx, cy, col);
    }

    // Operator symbol
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(op_ == OpType::Add ? "+" : "-", 160, 106, 4);

    // Right panel (for addition: animated second group; for subtraction: just = ?)
    const Rect rp = rightPanel();
    tft.fillRoundRect(rp.x, rp.y, rp.w, rp.h, 6, Ui::panel());
    tft.drawRoundRect(rp.x, rp.y, rp.w, rp.h, 6, Ui::outline());
    if (op_ == OpType::Add) {
        const uint8_t showN2 = (phase_ == Phase::Showing) ? 0 :
                               (phase_ == Phase::AnimIn)   ? animCount_ : n2_;
        for (uint8_t i = 0; i < showN2; ++i) {
            int16_t cx, cy; objPos(rp, i, cx, cy);
            drawObject(tft, cx, cy, COL_SECOND);
        }
    } else {
        // For subtraction the right panel shows the expected result count
        if (phase_ == Phase::Question || phase_ == Phase::Feedback) {
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(Ui::muted(), Ui::panel());
            tft.drawString("?", rp.x + rp.w/2, rp.y + rp.h/2, 4);
        }
    }

    // "= ?" while animating, buttons when question/feedback
    if (phase_ == Phase::Showing || phase_ == Phase::AnimIn || phase_ == Phase::Flashing) {
        tft.setTextDatum(MC_DATUM);
        tft.drawString("= ?", SCREEN_WIDTH / 2, 162, 4);
    }
    if (phase_ == Phase::Question || phase_ == Phase::Feedback) {
        for (uint8_t i = 0; i < 4; ++i) {
            uint16_t fill = OBJ_BTN;
            uint16_t tc   = TFT_WHITE;
            if (phase_ == Phase::Feedback) {
                if (i == correctBtn_)                 { fill = OBJ_CORRECT; tc = TFT_BLACK; }
                else if (i == static_cast<uint8_t>(selected_)) { fill = OBJ_WRONG;   tc = TFT_BLACK; }
            }
            Ui::drawButton(tft, answerRect(i), String(options_[i]), fill, TFT_DARKGREY, tc, false, 4);
        }
    }
    tft.setTextDatum(TL_DATUM);
}
