#include "SimonGame.h"

namespace {
constexpr uint16_t DARK_RED = 0xA145;
constexpr uint16_t DARK_BLUE = 0x245C;
constexpr uint16_t DARK_GREEN = 0x0488;
constexpr uint16_t DARK_YELLOW = 0xBDE0;
constexpr uint16_t LIT_RED = 0xD986;
constexpr uint16_t LIT_BLUE = 0x451D;
constexpr uint16_t LIT_GREEN = 0x35CC;
constexpr uint16_t LIT_YELLOW = 0xEEC3;
}

const char* SimonGame::title() const {
    return "Simon Says";
}

void SimonGame::begin(GameHost& host) {
    bestScore_ = static_cast<uint16_t>(host.board().getScore("simonBest", 0));
    length_ = 0;
    score_ = 0;
    appendStep();
    startShowing();
    markDirty();
}

void SimonGame::appendStep() {
    if (length_ < sizeof(sequence_)) {
        sequence_[length_++] = random(4);
    }
}

void SimonGame::startShowing() {
    phase_ = Phase::Showing;
    showIndex_ = 0;
    inputIndex_ = 0;
    litPad_ = -1;
    nextAt_ = millis() + 450UL;
}

Rect SimonGame::padRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(30 + col * 150), static_cast<int16_t>(74 + row * 66), 112, 54};
}

int8_t SimonGame::touchedPad(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < 4; ++i) {
        if (padRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

uint16_t SimonGame::padColor(uint8_t index, bool lit) const {
    static constexpr uint16_t dark[4] = {DARK_RED, DARK_BLUE, DARK_GREEN, DARK_YELLOW};
    static constexpr uint16_t bright[4] = {LIT_RED, LIT_BLUE, LIT_GREEN, LIT_YELLOW};
    return lit ? bright[index] : dark[index];
}

void SimonGame::update(GameHost& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (phase_ == Phase::Showing && now >= nextAt_) {
        if (litPad_ >= 0) {
            litPad_ = -1;
            ++showIndex_;
            nextAt_ = now + 80UL;
            if (showIndex_ >= length_) {
                phase_ = Phase::Waiting;
            }
        } else if (showIndex_ < length_) {
            litPad_ = sequence_[showIndex_];
            nextAt_ = now + 360UL;
        }
        markDirty();
        return;
    }

    if (phase_ == Phase::Good && now >= nextAt_) {
        appendStep();
        startShowing();
        markDirty();
        return;
    }

    if (phase_ == Phase::Waiting && litPad_ >= 0 && now >= nextAt_) {
        litPad_ = -1;
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    if (phase_ == Phase::Failed) {
        begin(host);
        return;
    }

    if (litPad_ >= 0) {
        return;
    }

    if (phase_ != Phase::Waiting) {
        return;
    }

    const int8_t pad = touchedPad(touch.x, touch.y);
    if (pad < 0) {
        return;
    }

    litPad_ = pad;
    nextAt_ = now + 120UL;
    if (pad == sequence_[inputIndex_]) {
        ++inputIndex_;
        if (inputIndex_ >= length_) {
            score_ = length_;
            if (host.board().saveBestScore("simonBest", score_, false)) {
                bestScore_ = score_;
            }
            phase_ = Phase::Good;
            litPad_ = -1;
            nextAt_ = now + 520UL;
        }
    } else {
        phase_ = Phase::Failed;
        litPad_ = pad;
    }
    markDirty();
}

void SimonGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(tft, title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Score ") + score_, 10, 36, 2);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Best ") + bestScore_, SCREEN_WIDTH - 10, 36, 2);

    String status;
    if (phase_ == Phase::Showing) {
        status = "Watch";
    } else if (phase_ == Phase::Waiting) {
        status = String("Repeat ") + (inputIndex_ + 1) + "/" + length_;
    } else if (phase_ == Phase::Good) {
        status = "Nice";
    } else {
        status = "Oops - tap to retry";
    }
    Ui::drawLabel(tft, Rect{20, 52, 280, 18}, status, phase_ == Phase::Failed ? Ui::error() : Ui::text(), 2, Align::Center);

    for (uint8_t i = 0; i < 4; ++i) {
        const Rect r = padRect(i);
        const bool lit = litPad_ == i || (phase_ == Phase::Failed && i == sequence_[inputIndex_]);
        tft.fillRoundRect(r.x + 2, r.y + 3, r.w, r.h, 8, Ui::surface());
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, padColor(i, lit));
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, TFT_DARKGREY);
    }

    if (phase_ == Phase::Failed) {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Bright pad was next", SCREEN_WIDTH / 2, 214, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
