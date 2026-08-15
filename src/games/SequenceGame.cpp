#include "SequenceGame.h"

const char* SequenceGame::DAY_NAMES[7] = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};

const char* SequenceGame::MONTH_NAMES[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

const char* SequenceGame::title() const { return "Calendar"; }

Rect SequenceGame::modeBtn(uint8_t m) const {
    return Rect{static_cast<int16_t>(8 + m * 124), 32, 120, 26};
}

Rect SequenceGame::answerTile(uint8_t i) const {
    const uint8_t col = i % 2;
    const uint8_t row = i / 2;
    return Rect{static_cast<int16_t>(8 + col * 159), static_cast<int16_t>(152 + row * 48), 152, 40};
}

uint8_t SequenceGame::itemCount() const {
    return mode_ == Mode::Days ? 7 : 12;
}

const char* SequenceGame::itemName(uint8_t idx) const {
    return mode_ == Mode::Days ? DAY_NAMES[idx] : MONTH_NAMES[idx];
}

void SequenceGame::newQuestion() {
    const uint8_t n = itemCount();
    qtype_ = static_cast<QType>(random(2));
    if (qtype_ == QType::After) {
        subject_ = static_cast<uint8_t>(random(n - 1));
        correct_ = subject_ + 1;
    } else {
        subject_ = static_cast<uint8_t>(1 + random(n - 1));
        correct_ = subject_ - 1;
    }
    correctPos_ = static_cast<uint8_t>(random(4));
    for (uint8_t i = 0; i < 4; ++i) options_[i] = 0xFF;
    options_[correctPos_] = correct_;
    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctPos_) continue;
        uint8_t c;
        bool unique;
        do {
            c = static_cast<uint8_t>(random(n));
            unique = (c != correct_);
            for (uint8_t j = 0; j < i; ++j) if (options_[j] == c) { unique = false; break; }
        } while (!unique);
        options_[i] = c;
    }
    selected_ = -1;
    answered_ = false;
    feedbackUntil_ = 0;
}

void SequenceGame::begin(AppContext&) {
    score_ = 0; rounds_ = 0;
    mode_ = Mode::Days;
    newQuestion();
    markDirty();
}

void SequenceGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (answered_ && now >= feedbackUntil_) {
        newQuestion();
        markDirty();
        return;
    }

    if (!touch.justPressed) return;

    // Mode toggle (always interactive)
    if (modeBtn(0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (mode_ != Mode::Days) { mode_ = Mode::Days; newQuestion(); markDirty(); }
        return;
    }
    if (modeBtn(1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (mode_ != Mode::Months) { mode_ = Mode::Months; newQuestion(); markDirty(); }
        return;
    }

    if (answered_) return;

    // Answer tiles
    for (uint8_t i = 0; i < 4; ++i) {
        if (answerTile(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            selected_ = static_cast<int8_t>(i);
            answered_ = true;
            ++rounds_;
            if (i == correctPos_) { ++score_; host.beepOk(); }
            else { host.beepError(); }
            feedbackUntil_ = millis() + 1400UL;
            markDirty();
            return;
        }
    }
}

void SequenceGame::render(AppContext& host) {
    TFT_eSPI& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    // Mode toggle buttons
    for (uint8_t m = 0; m < 2; ++m) {
        const bool active = (mode_ == static_cast<Mode>(m));
        Ui::drawButton(tft, modeBtn(m), m == 0 ? "Days" : "Months",
            active ? Ui::rgb(36, 132, 204) : Ui::panel(),
            Ui::outline(),
            active ? TFT_WHITE : Ui::text());
    }

    // Score
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TR_DATUM);
    char scoreBuf[16];
    snprintf(scoreBuf, sizeof(scoreBuf), "%u/%u", score_, rounds_);
    tft.drawString(scoreBuf, SCREEN_WIDTH - 8, 34, 2);

    // Question text
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    const char* qtypeStr = qtype_ == QType::After ? "AFTER" : "BEFORE";
    char prompt[32];
    snprintf(prompt, sizeof(prompt), "What comes %s:", qtypeStr);
    tft.drawString(prompt, SCREEN_WIDTH / 2, 70, 2);

    // Subject highlight box
    tft.fillRoundRect(60, 84, 200, 52, 8, Ui::surface());
    tft.drawRoundRect(60, 84, 200, 52, 8, Ui::rgb(36, 132, 204));
    tft.setTextColor(Ui::text(), Ui::surface());
    const char* subj = itemName(subject_);
    // Fit long names
    uint8_t subjectFont = 4;
    if (tft.textWidth(subj, 4) > 180) subjectFont = 2;
    tft.drawString(subj, SCREEN_WIDTH / 2, 110, subjectFont);

    // "?" label
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString("?", SCREEN_WIDTH / 2, 140, 2);

    // Answer tiles
    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t fill = Ui::panel();
        uint16_t tc   = Ui::text();
        if (answered_) {
            if (i == correctPos_)                       { fill = Ui::success(); tc = TFT_BLACK; }
            else if (i == static_cast<uint8_t>(selected_)) { fill = Ui::error();   tc = TFT_BLACK; }
        }
        const Rect tile = answerTile(i);
        Ui::drawButton(tft, tile, itemName(options_[i]), fill, Ui::outline(), tc, false, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
