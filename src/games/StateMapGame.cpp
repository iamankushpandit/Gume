#include "StateMapGame.h"
#include "map_n_flag.h"
#include "engine/AppRegistry.h"

namespace {
constexpr int16_t ANSWER_TOP = TOP_BAR_HEIGHT + 156;

constexpr AppScoreInfo STATE_MAP_SCORE = {
    "statemaps", "State Maps", "smapBest", "pts", false
};

constexpr AppMetadata STATE_MAP_METADATA = {
    "statemaps",
    "State Maps",
    nullptr,
    "name the outline",
    "State Maps",
    "Name the state outline, then capital.",
    &STATE_MAP_SCORE,
    LauncherIcon::StateMap,
    25,
    true,
};
}

const AppMetadata& stateMapAppMetadata() {
    return STATE_MAP_METADATA;
}

const char* StateMapGame::title() const {
    return stateMapAppMetadata().screenTitle != nullptr
        ? stateMapAppMetadata().screenTitle
        : stateMapAppMetadata().title;
}

Rect StateMapGame::imageRect(const Ui::Frame& f) const {
    return Ui::centreIn(Rect{0, TOP_BAR_HEIGHT + 14, f.w, 96}, 96, 96);
}
Rect StateMapGame::tierRect(const Ui::Frame& f) const {
    return Ui::centreIn(Rect{0, TOP_BAR_HEIGHT + 1, f.w, 16}, 56, 16);
}

Rect StateMapGame::answerBand(const Ui::Frame& f) const {
    return Rect{6, ANSWER_TOP, static_cast<int16_t>(f.w - 12),
                static_cast<int16_t>(f.h - ANSWER_TOP - 0)};
}

/* Two 150px choices side by side need 316px of width. Landscape has it;
 * portrait stacks all four instead, which is also what the extra height is
 * for. */
Rect StateMapGame::answerRect(const Ui::Frame& f, uint8_t i) const {
    const uint8_t cols = Ui::answerColumns(f, 4);
    const uint8_t rows = static_cast<uint8_t>(4 / cols);
    return Ui::gridCell(answerBand(f), cols, rows, i, 2);
}

uint8_t StateMapGame::poolSize() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < STATE_COUNT; ++i) {
        if (STATE_FACTS[i].tier <= tier_) ++n;
    }
    return n;
}

const StateFact* StateMapGame::fromPool(uint8_t index) const {
    for (uint8_t i = 0; i < STATE_COUNT; ++i) {
        if (STATE_FACTS[i].tier > tier_) continue;
        if (index == 0) return &STATE_FACTS[i];
        --index;
    }
    return nullptr;
}

void StateMapGame::makeOptions() {
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

void StateMapGame::newQuestion() {
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

void StateMapGame::begin(AppContext& host) {
    tier_ = static_cast<uint8_t>(host.getScore("smapTier", 1));
    if (tier_ < 1 || tier_ > 3) tier_ = 1;
    recent_.reset();
    correctStreak_ = 0;
    score_ = 0; rounds_ = 0; capBonus_ = 0;
    newQuestion();
}

void StateMapGame::update(AppContext& host, const TouchPoint& touch) {
    const Ui::Frame f = Ui::frame(host.display());
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

    if (tierRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        tier_ = static_cast<uint8_t>(tier_ >= 3 ? 1 : tier_ + 1);
        host.setScore("smapTier", tier_);
        recent_.reset();
        newQuestion();
        return;
    }

    if (phase_ != Phase::State && phase_ != Phase::CapitalBonus) return;
    if (current_ == nullptr) return;

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (!answerRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;

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
                    host.setScore("smapTier", tier_);
                }
            } else {
                correctStreak_ = 0;
                host.beepError();
            }
            host.saveBestScore(stateMapAppMetadata().score->bestKey, score_, false);
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

void StateMapGame::render(AppContext& host) {
    const Ui::Frame f = Ui::frame(host.display());
    Ui::Renderer& tft = host.display();
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
    tft.drawString(bonusBuf, f.w - 8, 33, 2);

    static const char* const TIER_NAMES[4] = {"", "Easy", "Medium", "Hard"};
    Ui::drawButton(tft, tierRect(f), TIER_NAMES[tier_], Ui::panel(), Ui::outline(), Ui::text(), false, 1);

    if (current_ == nullptr) {
        Ui::drawLabel(tft, Rect{20, 110, 280, 20}, "No states available",
                      Ui::error(), 2, Align::Center);
        return;
    }

    const Rect ir = imageRect(f);
    tft.fillRect(ir.x - 2, ir.y - 2, ir.w + 4, ir.h + 4, Ui::bg());
    tft.drawRect(ir.x - 2, ir.y - 2, ir.w + 4, ir.h + 4, Ui::outline());
    Ui::drawCountryImageTinted(tft, mnf_state_map(current_->code), ir,
                               Ui::bg(), Ui::rgb(36, 132, 204));

    const bool capitalRound = (phase_ == Phase::CapitalBonus || phase_ == Phase::FeedbackCapital);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(capitalRound ? Ui::rgb(255, 200, 0) : Ui::text(), Ui::bg());

    if (capitalRound) {
        char prompt[48];
        snprintf(prompt, sizeof(prompt), "Bonus! Capital of %s?", current_->name);
        tft.drawString(prompt, f.cx(), 150, 2);
    } else {
        tft.drawString("Which state?", f.cx(), 176, 2);
    }

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
        const Rect r = answerRect(f, i);
        Ui::drawButton(tft, r, label, fill, Ui::outline(), tc, false, 2);
    }

    tft.setTextDatum(TL_DATUM);
}
