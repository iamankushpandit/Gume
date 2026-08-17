#include "FingerCountGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t SKIN_UP   = 0xFDB6;   // warm skin tone, raised
constexpr uint16_t SKIN_DOWN = 0x9CD3;   // dimmed, folded down
constexpr uint16_t NAIL      = 0xFEDA;

/* Finger lengths, thumb -> little. The middle finger is tallest, which makes
 * the hand read as a hand and separates the tips much further apart than the
 * old flat row of circles ever did. */
constexpr int16_t FINGER_LEN[5] = {34, 46, 52, 46, 36};
constexpr int16_t STUB_LEN = 12;         // folded-down finger

constexpr AppMetadata FINGER_COUNT_METADATA = {
    "fingers",
    "Fingers",
    "Finger Counting",
    "count on hands",
    "Fingers",
    "Count on two hands.",
    nullptr,
    LauncherIcon::FingerCount,
    18,
    true,
};
}

const AppMetadata& fingerCountAppMetadata() {
    return FINGER_COUNT_METADATA;
}

const char* FingerCountGame::title() const {
    return fingerCountAppMetadata().screenTitle != nullptr
        ? fingerCountAppMetadata().screenTitle
        : fingerCountAppMetadata().title;
}

int16_t FingerCountGame::fingerPitch(const Ui::Frame& f) const {
    /* Budget: two hands of 4*pitch + width, a 16px gap between them and 10px
     * margins, so 8*pitch + 80 has to fit the width. */
    int16_t pitch = static_cast<int16_t>((f.w - 80) / 8);
    if (pitch > MAX_PITCH) {
        pitch = MAX_PITCH;
    }
    return pitch < 12 ? 12 : pitch;
}

int16_t FingerCountGame::fingerWidth(const Ui::Frame& f) const {
    return static_cast<int16_t>(fingerPitch(f) - 4);
}

int16_t FingerCountGame::handX0(const Ui::Frame& f, uint8_t hand) const {
    const int16_t span = static_cast<int16_t>(4 * fingerPitch(f) + fingerWidth(f));
    return hand == 0 ? static_cast<int16_t>(f.cx() - 8 - span)
                     : static_cast<int16_t>(f.cx() + 8);
}

/* The palms hang off the answer row rather than off y=240, so the hands keep
 * the same distance above the buttons on any panel. */
int16_t FingerCountGame::palmBottom(const Ui::Frame& f) const {
    return static_cast<int16_t>(answerBand(f).y - 14);
}

int16_t FingerCountGame::palmTop(const Ui::Frame& f) const {
    return static_cast<int16_t>(palmBottom(f) - PALM_H);
}

Rect FingerCountGame::answerBand(const Ui::Frame& f) const {
    /* Four 68px buttons on a 76px pitch need 304px in a row; portrait pairs
     * them instead, as Counting and Number Line already do. */
    const uint8_t cols = f.tall() ? 2 : OPTION_COUNT;
    const uint8_t rows = static_cast<uint8_t>(OPTION_COUNT / cols);
    const int16_t h = static_cast<int16_t>(rows * 34 + (rows - 1) * 8);
    return Rect{12, static_cast<int16_t>(f.h - h - 10),
                static_cast<int16_t>(f.w - 24), h};
}

int16_t FingerCountGame::fingerTop(const Ui::Frame& f, uint8_t idx) const {
    const uint8_t hand = idx / 5;
    const uint8_t digit = idx % 5;
    // Mirror the right hand so the two face each other like a real pair.
    const uint8_t shape = (hand == 0) ? digit : static_cast<uint8_t>(4 - digit);
    const int16_t len = raised_[idx] ? FINGER_LEN[shape] : STUB_LEN;
    return static_cast<int16_t>(palmTop(f) - len);
}

Rect FingerCountGame::fingerRect(const Ui::Frame& f, uint8_t idx) const {
    const uint8_t hand = idx / 5;
    const uint8_t digit = idx % 5;
    const int16_t x = static_cast<int16_t>(handX0(f, hand) + digit * fingerPitch(f));
    // The touch target always spans the full possible finger height, so a
    // folded-down finger is just as easy to tap back up as a raised one.
    const int16_t top = static_cast<int16_t>(palmTop(f) - 52);
    return Rect{x, top, fingerWidth(f), static_cast<int16_t>(palmBottom(f) - top)};
}

Rect FingerCountGame::answerRect(const Ui::Frame& f, uint8_t i) const {
    const uint8_t cols = f.tall() ? 2 : OPTION_COUNT;
    const uint8_t rows = static_cast<uint8_t>(OPTION_COUNT / cols);
    return Ui::gridCell(answerBand(f), cols, rows, i, 8);
}

uint8_t FingerCountGame::raisedCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < FINGER_COUNT; ++i) {
        if (raised_[i]) ++n;
    }
    return n;
}

void FingerCountGame::makeOptions() {
    correctBtn_ = static_cast<uint8_t>(random(OPTION_COUNT));
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) options_[i] = target_;

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (i == correctBtn_) continue;
        for (uint8_t attempt = 0; attempt < 40; ++attempt) {
            // Keep distractors near the answer so careful counting beats guessing.
            int16_t v = static_cast<int16_t>(target_) + (random(2) ? 1 : -1) * (1 + random(3));
            if (v < 1) v = 1;
            if (v > 10) v = 10;
            bool dup = (v == static_cast<int16_t>(target_));
            for (uint8_t j = 0; j < i && !dup; ++j) {
                if (options_[j] == static_cast<uint8_t>(v)) dup = true;
            }
            if (dup) continue;
            options_[i] = static_cast<uint8_t>(v);
            break;
        }
        if (options_[i] == target_) {
            // Deterministic fallback so no two buttons ever show the same number.
            for (uint8_t v = 1; v <= 10; ++v) {
                bool used = (v == target_);
                for (uint8_t j = 0; j < OPTION_COUNT && !used; ++j) {
                    if (j != i && options_[j] == v) used = true;
                }
                if (!used) { options_[i] = v; break; }
            }
        }
    }
}

void FingerCountGame::newQuestion() {
    mode_ = (random(2) == 0) ? Mode::Count : Mode::ShowMe;
    target_ = static_cast<uint8_t>(1 + random(10));   // 1..10

    for (uint8_t i = 0; i < FINGER_COUNT; ++i) raised_[i] = false;

    if (mode_ == Mode::Count) {
        // Raise exactly target_ fingers at random, then ask how many there are.
        uint8_t placed = 0;
        while (placed < target_) {
            const uint8_t idx = static_cast<uint8_t>(random(FINGER_COUNT));
            if (raised_[idx]) continue;
            raised_[idx] = true;
            ++placed;
        }
        makeOptions();
    }

    selected_ = -1;
    phase_ = Phase::Active;
    feedbackUntil_ = 0;
    markDirty();
}

void FingerCountGame::begin(AppContext&) {
    score_ = 0; rounds_ = 0;
    newQuestion();
}

void FingerCountGame::update(AppContext& host, const TouchPoint& touch) {
    const Ui::Frame f = Ui::frame(host.display());
    const uint32_t now = millis();

    if (phase_ == Phase::Feedback && now >= feedbackUntil_) {
        newQuestion();
        return;
    }
    if (!touch.justPressed || phase_ != Phase::Active) return;

    if (mode_ == Mode::Count) {
        for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
            if (!answerRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
            selected_ = static_cast<int8_t>(i);
            lastCorrect_ = (i == correctBtn_);
            ++rounds_;
            if (lastCorrect_) { ++score_; host.beepOk(); }
            else              { host.beepError(); }
            feedbackUntil_ = now + 1500UL;
            phase_ = Phase::Feedback;
            markDirty();
            return;
        }
        return;
    }

    // ShowMe: toggle fingers until the raised count matches the target.
    for (uint8_t i = 0; i < FINGER_COUNT; ++i) {
        if (!fingerRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        raised_[i] = !raised_[i];
        markDirty();
        if (raisedCount() == target_) {
            ++rounds_; ++score_;
            lastCorrect_ = true;
            host.beepOk();
            feedbackUntil_ = now + 1300UL;
            phase_ = Phase::Feedback;
        }
        return;
    }
}

void FingerCountGame::drawHand(Ui::Renderer& tft, const Ui::Frame& f, uint8_t hand) const {
    const int16_t x0 = handX0(f, hand);
    const int16_t FINGER_PITCH = fingerPitch(f);
    const int16_t FINGER_W = fingerWidth(f);
    const int16_t PALM_TOP = palmTop(f);
    const int16_t PALM_BOTTOM = palmBottom(f);

    // Palm first, so the fingers sit on top of it.
    const int16_t palmX = static_cast<int16_t>(x0 - 4);
    const int16_t palmW = static_cast<int16_t>(4 * FINGER_PITCH + FINGER_W + 8);
    const int16_t palmH = static_cast<int16_t>(PALM_BOTTOM - PALM_TOP + 6);
    tft.fillRoundRect(palmX, static_cast<int16_t>(PALM_TOP - 6), palmW, palmH, 8, SKIN_UP);
    tft.drawRoundRect(palmX, static_cast<int16_t>(PALM_TOP - 6), palmW, palmH, 8, Ui::outline());

    for (uint8_t d = 0; d < 5; ++d) {
        const uint8_t idx = static_cast<uint8_t>(hand * 5 + d);
        const int16_t x = static_cast<int16_t>(x0 + d * FINGER_PITCH);
        const int16_t top = fingerTop(f, idx);
        const int16_t h = static_cast<int16_t>(PALM_TOP + 4 - top);
        const uint16_t col = raised_[idx] ? SKIN_UP : SKIN_DOWN;

        tft.fillRoundRect(x, top, FINGER_W, h, 9, col);
        tft.drawRoundRect(x, top, FINGER_W, h, 9, Ui::outline());
        if (raised_[idx]) {
            // A nail on the tip reads as "up" at a glance.
            tft.fillRoundRect(static_cast<int16_t>(x + 5), static_cast<int16_t>(top + 5),
                              static_cast<int16_t>(FINGER_W - 10), 8, 3, NAIL);
        }
    }
}

void FingerCountGame::render(AppContext& host) {
    const Ui::Frame f = Ui::frame(host.display());
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String(score_) + "/" + rounds_, f.w - 8, TOP_BAR_HEIGHT + 3, 2);

    const bool done = (phase_ == Phase::Feedback);
    const uint8_t up = raisedCount();

    // --- prompt ----------------------------------------------------------
    tft.setTextDatum(MC_DATUM);
    if (mode_ == Mode::Count) {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString("How many fingers?", f.cx(), TOP_BAR_HEIGHT + 20, 4);
        if (done) {
            tft.setTextColor(lastCorrect_ ? Ui::success() : Ui::error(), Ui::bg());
            tft.drawString(lastCorrect_ ? String("Yes! ") + target_
                                        : String("It was ") + target_,
                           f.cx(), TOP_BAR_HEIGHT + 48, 2);
        } else {
            tft.setTextColor(Ui::muted(), Ui::bg());
            tft.drawString("Count them, then tap the number", f.cx(), TOP_BAR_HEIGHT + 48, 2);
        }
    } else {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(String("Show me ") + target_ + (target_ == 1 ? " finger" : " fingers"),
                       f.cx(), TOP_BAR_HEIGHT + 20, 4);
        if (done) {
            tft.setTextColor(Ui::success(), Ui::bg());
            tft.drawString("That's it!", f.cx(), TOP_BAR_HEIGHT + 48, 2);
        } else {
            tft.setTextColor(up == target_ ? Ui::success() : Ui::muted(), Ui::bg());
            tft.drawString(String("Up: ") + up, f.cx(), TOP_BAR_HEIGHT + 48, 2);
        }
    }

    // --- hands -----------------------------------------------------------
    drawHand(tft, f, 0);
    drawHand(tft, f, 1);

    /* Labels sit ON the palms. Above the hands they collided with the middle
     * fingertip, which reaches y=100. */
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::rgb(150, 90, 88), SKIN_UP);
    const int16_t labelY = static_cast<int16_t>(palmTop(f) + 17);
    tft.drawString("Left",
                   static_cast<int16_t>(handX0(f, 0) + 2 * fingerPitch(f) + fingerWidth(f) / 2),
                   labelY, 2);
    tft.drawString("Right",
                   static_cast<int16_t>(handX0(f, 1) + 2 * fingerPitch(f) + fingerWidth(f) / 2),
                   labelY, 2);

    // --- answers (Count mode only) ---------------------------------------
    if (mode_ == Mode::Count) {
        for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
            uint16_t fill = Ui::panel();
            uint16_t tc   = Ui::text();
            if (done) {
                if (i == correctBtn_) { fill = Ui::success(); tc = TFT_BLACK; }
                else if (i == static_cast<uint8_t>(selected_)) { fill = Ui::error(); tc = TFT_BLACK; }
            }
            Ui::drawButton(tft, answerRect(f, i), String(options_[i]), fill, Ui::outline(), tc, false, 4);
        }
    } else {
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Tap a finger to raise or lower it", f.cx(),
                       static_cast<int16_t>(answerBand(f).y + 6), 2);
    }

    tft.setTextDatum(TL_DATUM);
}
