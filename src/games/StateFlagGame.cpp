#include "StateFlagGame.h"
#include "map_n_flag.h"

namespace {
constexpr uint16_t FLAG_BG = 0xFFFF;
}

const char* StateFlagGame::title() const { return "State Flags"; }

Rect StateFlagGame::imageRect() const { return Rect{80, 48, 160, 120}; }
Rect StateFlagGame::tierRect() const  { return Rect{132, 31, 56, 16}; }

Rect StateFlagGame::answerRect(uint8_t i) const {
    const int16_t col = i % 2;
    const int16_t row = i / 2;
    return Rect{static_cast<int16_t>(6 + col * 158),
                static_cast<int16_t>(186 + row * 27), 150, 25};
}

uint8_t StateFlagGame::poolSize() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < STATE_COUNT; ++i) {
        if (STATE_FACTS[i].tier <= tier_) ++n;
    }
    return n;
}

const StateFact* StateFlagGame::fromPool(uint8_t index) const {
    for (uint8_t i = 0; i < STATE_COUNT; ++i) {
        if (STATE_FACTS[i].tier > tier_) continue;
        if (index == 0) return &STATE_FACTS[i];
        --index;
    }
    return nullptr;
}

void StateFlagGame::makeOptions() {
    const uint8_t pool = poolSize();
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
            if (!dup && strcmp(cand->capital, current_->capital) == 0) dup = true;
            if (dup) continue;
            options_[i] = cand;
            break;
        }
        if (options_[i] == current_) {
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
}

void StateFlagGame::newQuestion() {
    const uint8_t pool = poolSize();
    if (pool == 0) { current_ = nullptr; return; }

    const uint8_t idx = static_cast<uint8_t>(recent_.pickIndex(pool));
    current_ = fromPool(idx);
    if (current_ == nullptr) current_ = fromPool(0);

    selected_ = -1;
    phase_ = Phase::State;
    feedbackUntil_ = 0;
    makeOptions();
    markFullDirty();
}

void StateFlagGame::begin(AppContext& host) {
    tier_ = static_cast<uint8_t>(host.getScore("sflagTier", 1));
    if (tier_ < 1 || tier_ > 3) tier_ = 1;
    recent_.reset();
    correctStreak_ = 0;
    score_ = 0; rounds_ = 0; capBonus_ = 0;
    newQuestion();
}

void StateFlagGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if ((phase_ == Phase::FeedbackState || phase_ == Phase::FeedbackCapital) &&
        now >= feedbackUntil_) {
        if (phase_ == Phase::FeedbackState && lastCorrect_) {
            selected_ = -1;
            phase_ = Phase::CapitalBonus;
        } else {
            newQuestion();
            return;
        }
        markFullDirty();
        return;
    }

    if (!touch.justPressed) return;

    if (tierRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        tier_ = static_cast<uint8_t>(tier_ >= 3 ? 1 : tier_ + 1);
        host.setScore("sflagTier", tier_);
        recent_.reset();
        newQuestion();
        return;
    }

    if (phase_ != Phase::State && phase_ != Phase::CapitalBonus) return;
    if (current_ == nullptr) return;

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;

        selected_ = static_cast<int8_t>(i);
        lastCorrect_ = (i == correctBtn_);

        if (phase_ == Phase::State) {
            ++rounds_;
            if (lastCorrect_) {
                ++score_;
                ++correctStreak_;
                host.beepOk();
                if (correctStreak_ >= 6 && tier_ < 3) {
                    ++tier_;
                    correctStreak_ = 0;
                    host.setScore("sflagTier", tier_);
                }
            } else {
                correctStreak_ = 0;
                host.beepError();
            }
            host.saveBestScore("sflagBest", score_, false);
            feedbackUntil_ = now + 1500UL;
            phase_ = Phase::FeedbackState;
        } else {
            if (lastCorrect_) { ++capBonus_; host.beepOk(); }
            else              { host.beepError(); }
            feedbackUntil_ = now + 1600UL;
            phase_ = Phase::FeedbackCapital;
        }
        markFullDirty();
        return;
    }
}

void StateFlagGame::render(AppContext& host) {
    TFT_eSPI& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    char scoreBuf[16];
    snprintf(scoreBuf, sizeof(scoreBuf), "%u/%u", score_, rounds_);
    tft.drawString(scoreBuf, 8, 33, 2);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Ui::rgb(255, 200, 0), Ui::bg());
    char bonusBuf[8];
    snprintf(bonusBuf, sizeof(bonusBuf), "+%u", capBonus_);
    tft.drawString(bonusBuf, SCREEN_WIDTH - 8, 33, 2);

    static const char* const TIER_NAMES[4] = {"", "Easy", "Medium", "Hard"};
    Ui::drawButton(tft, tierRect(), TIER_NAMES[tier_], Ui::panel(), Ui::outline(), Ui::text(), false, 1);

    if (current_ == nullptr) {
        Ui::drawLabel(tft, Rect{20, 110, 280, 20}, "No states available",
                      Ui::error(), 2, Align::Center);
        return;
    }

    const Rect fr = imageRect();
    tft.fillRect(fr.x - 2, fr.y - 2, fr.w + 4, fr.h + 4, FLAG_BG);
    tft.drawRect(fr.x - 2, fr.y - 2, fr.w + 4, fr.h + 4, Ui::outline());
    Ui::drawCountryImageScaled(tft, mnf_state_flag(current_->code), fr, FLAG_BG, 2);

    const bool capitalRound = (phase_ == Phase::CapitalBonus || phase_ == Phase::FeedbackCapital);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(capitalRound ? Ui::rgb(255, 200, 0) : Ui::text(), Ui::bg());
    tft.drawString(capitalRound
                       ? String("Bonus! Capital of ") + current_->name + "?"
                       : String("Which state?"),
                   SCREEN_WIDTH / 2, 176, 2);

    const bool showingFeedback =
        (phase_ == Phase::FeedbackState || phase_ == Phase::FeedbackCapital);

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const StateFact* opt = options_[i];
        if (opt == nullptr) continue;

        uint16_t fill = Ui::panel();
        uint16_t tc   = Ui::text();
        if (showingFeedback) {
            if (i == correctBtn_) { fill = Ui::success(); tc = TFT_BLACK; }
            else if (i == static_cast<uint8_t>(selected_)) { fill = Ui::error(); tc = TFT_BLACK; }
        }

        const char* label = capitalRound ? opt->capital : opt->name;
        const Rect r = answerRect(i);
        Ui::drawButton(tft, r, label, fill, Ui::outline(), tc, false, 2);
    }

    tft.setTextDatum(TL_DATUM);
}
