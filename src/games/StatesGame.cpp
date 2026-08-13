#include "StatesGame.h"

namespace {
String fitLabel(TFT_eSPI& tft, const String& in, int16_t maxW, uint8_t font) {
    if (tft.textWidth(in, font) <= maxW) return in;
    String s = in;
    while (s.length() > 1 && tft.textWidth(s + ".", font) > maxW) {
        s.remove(s.length() - 1);
    }
    return s + ".";
}
}

const char* StatesGame::title() const { return "US States"; }

Rect StatesGame::tierRect() const { return Rect{132, 31, 56, 16}; }

Rect StatesGame::answerRect(uint8_t i) const {
    const int16_t col = i % 2;
    const int16_t row = i / 2;
    return Rect{static_cast<int16_t>(6 + col * 158),
                static_cast<int16_t>(150 + row * 40), 150, 36};
}

uint8_t StatesGame::poolSize() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < STATE_COUNT; ++i) {
        if (STATE_FACTS[i].tier <= tier_) ++n;
    }
    return n;
}

const StateFact* StatesGame::fromPool(uint8_t index) const {
    for (uint8_t i = 0; i < STATE_COUNT; ++i) {
        if (STATE_FACTS[i].tier > tier_) continue;
        if (index == 0) return &STATE_FACTS[i];
        --index;
    }
    return nullptr;
}

void StatesGame::newQuestion() {
    const uint8_t pool = poolSize();
    if (pool == 0) { current_ = nullptr; return; }

    /* Draw at random but reject anything still in the recent window, so a state
     * cannot come back within ten questions while the order stays unpredictable.
     * The window is skipped when the pool is too small to satisfy it. */
    const StateFact* chosen = nullptr;
    for (uint8_t attempt = 0; attempt < 24; ++attempt) {
        const StateFact* cand = fromPool(static_cast<uint8_t>(random(pool)));
        if (cand == nullptr) continue;
        const uint32_t token = static_cast<uint32_t>(cand - STATE_FACTS);
        if (pool > RecentQuestions::DEPTH && recent_.recentlyUsed(token)) continue;
        chosen = cand;
        recent_.remember(token);
        break;
    }
    current_ = (chosen != nullptr) ? chosen : fromPool(0);

    mode_ = (random(2) == 0) ? Mode::CapitalOf : Mode::WhichState;

    correctBtn_ = static_cast<uint8_t>(random(OPTION_COUNT));
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) options_[i] = current_;

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (i == correctBtn_) continue;
        for (uint8_t attempt = 0; attempt < 40; ++attempt) {
            const StateFact* cand = fromPool(static_cast<uint8_t>(random(pool)));
            if (cand == nullptr || cand == current_) continue;
            bool dup = false;
            for (uint8_t j = 0; j < OPTION_COUNT; ++j) {
                if (j != i && options_[j] == cand) { dup = true; break; }
            }
            if (dup) continue;
            options_[i] = cand;
            break;
        }
        if (options_[i] == current_) {
            // Deterministic fallback so two buttons never show the same answer.
            for (uint8_t k = 0; k < pool; ++k) {
                const StateFact* cand = fromPool(k);
                if (cand == nullptr || cand == current_) continue;
                bool used = false;
                for (uint8_t j = 0; j < OPTION_COUNT; ++j) {
                    if (j != i && options_[j] == cand) { used = true; break; }
                }
                if (!used) { options_[i] = cand; break; }
            }
        }
    }

    selected_ = -1;
    phase_ = Phase::Asking;
    feedbackUntil_ = 0;
    markFullDirty();
}

void StatesGame::begin(GameHost& host) {
    tier_ = static_cast<uint8_t>(host.board().getScore("stateTier", 1));
    if (tier_ < 1 || tier_ > 3) tier_ = 1;
    progress_.begin(host.board(), "stateSrs", STATE_COUNT);
    recent_.reset();
    correctStreak_ = 0;
    score_ = 0;
    rounds_ = 0;
    current_ = nullptr;
    newQuestion();
}

void StatesGame::update(GameHost& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (phase_ == Phase::Feedback && now >= feedbackUntil_) {
        newQuestion();
        return;
    }
    if (!touch.justPressed) return;

    if (tierRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        tier_ = static_cast<uint8_t>(tier_ >= 3 ? 1 : tier_ + 1);
        host.board().setScore("stateTier", tier_);
        recent_.reset();
        newQuestion();
        return;
    }
    if (phase_ != Phase::Asking || current_ == nullptr) return;

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;

        selected_ = static_cast<int8_t>(i);
        lastCorrect_ = (i == correctBtn_);
        ++rounds_;
        progress_.record(static_cast<uint16_t>(current_ - STATE_FACTS), lastCorrect_);
        progress_.flush();

        if (lastCorrect_) {
            ++score_;
            ++correctStreak_;
            host.board().beepOk();
            // Six in a row means the pool is too easy; the tier button still wins.
            if (correctStreak_ >= 6 && tier_ < 3) {
                ++tier_;
                correctStreak_ = 0;
                host.board().setScore("stateTier", tier_);
            }
        } else {
            correctStreak_ = 0;
            host.board().beepError();
        }
        host.board().saveBestScore("stateBest", score_, false);
        feedbackUntil_ = now + 1600UL;
        phase_ = Phase::Feedback;
        markFullDirty();
        return;
    }
}

void StatesGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(tft, title());

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(String(score_) + "/" + rounds_, 8, 33, 2);

    static const char* const TIER_NAMES[4] = {"", "Easy", "Medium", "Hard"};
    Ui::drawButton(tft, tierRect(), TIER_NAMES[tier_], Ui::panel(), Ui::outline(), Ui::text(), false, 1);

    if (current_ == nullptr) {
        Ui::drawLabel(tft, Rect{20, 110, 280, 20}, "No states available",
                      Ui::error(), 2, Align::Center);
        return;
    }

    // Subject first and large, question underneath.
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::rgb(90, 170, 255), Ui::bg());
    tft.drawString(mode_ == Mode::CapitalOf ? current_->name : current_->capital,
                   SCREEN_WIDTH / 2, 72, 4);

    const bool done = (phase_ == Phase::Feedback);
    tft.setTextColor(done ? (lastCorrect_ ? Ui::success() : Ui::error()) : Ui::text(), Ui::bg());

    String prompt;
    if (done) {
        prompt = lastCorrect_ ? String("Yes! ") : String("It is ");
        prompt += (mode_ == Mode::CapitalOf) ? current_->capital : current_->name;
    } else {
        prompt = (mode_ == Mode::CapitalOf) ? String("What is its capital?")
                                            : String("is the capital of which state?");
    }
    tft.drawString(fitLabel(tft, prompt, SCREEN_WIDTH - 16, 2), SCREEN_WIDTH / 2, 112, 2);

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const StateFact* opt = options_[i];
        if (opt == nullptr) continue;

        uint16_t fill = Ui::panel();
        uint16_t tc   = Ui::text();
        if (done) {
            if (i == correctBtn_) { fill = Ui::success(); tc = TFT_BLACK; }
            else if (i == static_cast<uint8_t>(selected_)) { fill = Ui::error(); tc = TFT_BLACK; }
        }
        const Rect r = answerRect(i);
        const char* label = (mode_ == Mode::CapitalOf) ? opt->capital : opt->name;
        Ui::drawButton(tft, r, fitLabel(tft, label, r.w - 10, 2), fill, Ui::outline(), tc, false, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
