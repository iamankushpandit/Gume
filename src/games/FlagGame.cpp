#include "FlagGame.h"
#include "map_n_flag.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t FLAG_BG = 0xFFFF;   // white card behind the flag

/* Shrink a label until it fits the given width, adding an ellipsis. Country
 * names like "United Arab Emirates" do not fit a half-width button at font 2. */
String fitLabel(Ui::Renderer& tft, const String& in, int16_t maxW, uint8_t font) {
    if (tft.textWidth(in, font) <= maxW) return in;
    String s = in;
    while (s.length() > 1 && tft.textWidth(s + ".", font) > maxW) {
        s.remove(s.length() - 1);
    }
    return s + ".";
}

constexpr AppMetadata FLAG_METADATA = {
    "flags",
    "Flags",
    "Guess the Flag",
    "guess the flag",
    "Flags",
    "Name the flag, then its capital.",
    nullptr,
    LauncherIcon::Flag,
    21,
    true,
};
}

const AppMetadata& flagAppMetadata() {
    return FLAG_METADATA;
}

const char* FlagGame::title() const {
    return flagAppMetadata().screenTitle != nullptr
        ? flagAppMetadata().screenTitle
        : flagAppMetadata().title;
}

Rect FlagGame::flagRect() const  { return Rect{80, 48, 160, 120}; }
Rect FlagGame::tierRect() const  { return Rect{132, 31, 56, 16}; }

/* 2x2 grid of wide buttons. Four buttons across the bottom would only be 72px
 * each, which truncates most country names into uselessness. */
Rect FlagGame::answerRect(uint8_t i) const {
    const int16_t col = i % 2;
    const int16_t row = i / 2;
    return Rect{static_cast<int16_t>(6 + col * 158),
                static_cast<int16_t>(186 + row * 27), 150, 25};
}

bool FlagGame::recentlyUsed(const char* iso2) const {
    for (uint8_t i = 0; i < RECENT_COUNT; ++i) {
        if (recent_[i] != nullptr && strcmp(recent_[i], iso2) == 0) return true;
    }
    return false;
}

void FlagGame::rememberCurrent() {
    if (current_ == nullptr) return;
    recent_[recentPos_] = current_->iso2;
    recentPos_ = static_cast<uint8_t>((recentPos_ + 1) % RECENT_COUNT);
}

void FlagGame::makeOptions() {
    const uint16_t pool = countryPoolSize(tier_, false);
    correctBtn_ = static_cast<uint8_t>(random(OPTION_COUNT));

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) options_[i] = current_;

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (i == correctBtn_) continue;
        for (uint8_t attempt = 0; attempt < 60; ++attempt) {
            const CountryFact* cand = countryFromPool(
                static_cast<uint16_t>(random(pool)), tier_, false);
            if (cand == nullptr || cand == current_) continue;
            bool dup = false;
            for (uint8_t j = 0; j < i; ++j) {
                if (options_[j] == cand) { dup = true; break; }
            }
            // Capitals must be distinct too, since the bonus round reuses these.
            if (!dup && strcmp(cand->capital, current_->capital) == 0) dup = true;
            if (dup) continue;
            options_[i] = cand;
            break;
        }

        // Deterministic fallback: random sampling can in principle exhaust its
        // attempts, and leaving the slot as current_ would show the same
        // country on two buttons.
        if (options_[i] == current_) {
            for (uint16_t k = 0; k < pool; ++k) {
                const CountryFact* cand = countryFromPool(k, tier_, false);
                if (cand == nullptr || cand == current_) continue;
                bool dup = false;
                for (uint8_t j = 0; j < OPTION_COUNT; ++j) {
                    if (j != i && options_[j] == cand) { dup = true; break; }
                }
                if (dup) continue;
                options_[i] = cand;
                break;
            }
        }
    }
}

void FlagGame::shuffleOptionsForBonus() {
    const uint8_t oldCorrect = correctBtn_;
    for (int8_t i = static_cast<int8_t>(OPTION_COUNT - 1); i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(random(i + 1));
        const CountryFact* tmp = options_[i];
        options_[i] = options_[j];
        options_[j] = tmp;
    }

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (options_[i] == current_) {
            correctBtn_ = i;
            break;
        }
    }

    if (correctBtn_ == oldCorrect && OPTION_COUNT > 1) {
        const uint8_t swapWith = static_cast<uint8_t>(
            (correctBtn_ + 1 + random(OPTION_COUNT - 1)) % OPTION_COUNT);
        const CountryFact* tmp = options_[correctBtn_];
        options_[correctBtn_] = options_[swapWith];
        options_[swapWith] = tmp;
        correctBtn_ = swapWith;
    }
}

namespace {
struct PoolFilter { uint8_t tier; bool needsMap; };
bool poolAllows(uint16_t i, void* ctx) {
    const PoolFilter* f = static_cast<const PoolFilter*>(ctx);
    return countryQualifies(i, f->tier, f->needsMap);
}
}

void FlagGame::newQuestion() {
    const uint16_t pool = countryPoolSize(tier_, false);
    if (pool == 0) { current_ = nullptr; return; }

    /* Weighted by mastery rather than uniform: a flag that was missed recently
     * carries up to 8x the weight of one that is already known, so practice
     * concentrates where it is needed without ever fully dropping the rest. */
    PoolFilter filter{tier_, false};
    for (uint8_t attempt = 0; attempt < 24; ++attempt) {
        const uint16_t idx = progress_.pickWeighted(poolAllows, &filter);
        if (idx == 0xFFFF) break;
        current_ = &COUNTRY_FACTS[idx];
        if (pool <= RECENT_COUNT || !recentlyUsed(current_->iso2)) break;
    }
    if (current_ == nullptr) {
        current_ = countryFromPool(static_cast<uint16_t>(random(pool)), tier_, false);
    }
    rememberCurrent();

    selected_ = -1;
    phase_ = Phase::Country;
    feedbackUntil_ = 0;
    makeOptions();
}

void FlagGame::begin(AppContext& host) {
    tier_ = static_cast<uint8_t>(host.getScore("geoTier", 1));
    if (tier_ < 1 || tier_ > 3) tier_ = 1;
    progress_.begin(host, "flagSrs", COUNTRY_FACT_COUNT);
    correctStreak_ = 0;
    score_ = 0; rounds_ = 0; capBonus_ = 0;
    for (uint8_t i = 0; i < RECENT_COUNT; ++i) recent_[i] = nullptr;
    recentPos_ = 0;
    newQuestion();
    markDirty();
}

void FlagGame::end(AppContext& host) {
    (void)host;
    progress_.flush();
}

void FlagGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if ((phase_ == Phase::FeedbackCountry || phase_ == Phase::FeedbackCapital) &&
        now >= feedbackUntil_) {
        if (phase_ == Phase::FeedbackCountry && lastCorrect_) {
            // Earned the capital bonus round for this same country.
            shuffleOptionsForBonus();
            selected_ = -1;
            phase_ = Phase::CapitalBonus;
        } else {
            newQuestion();
        }
        markFullDirty();
        return;
    }

    if (!touch.justPressed) return;

    // Difficulty cycles Easy -> Medium -> Hard and restarts the round.
    if (tierRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        tier_ = static_cast<uint8_t>(tier_ >= 3 ? 1 : tier_ + 1);
        host.setScore("geoTier", tier_);
        for (uint8_t i = 0; i < RECENT_COUNT; ++i) recent_[i] = nullptr;
        newQuestion();
        markDirty();
        return;
    }

    if (phase_ != Phase::Country && phase_ != Phase::CapitalBonus) return;
    if (current_ == nullptr) return;

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;

        selected_ = static_cast<int8_t>(i);
        lastCorrect_ = (i == correctBtn_);

        if (phase_ == Phase::Country) {
            ++rounds_;
            progress_.record(countryIndex(current_), lastCorrect_);
            progress_.maybeFlush();
            if (lastCorrect_) {
                ++score_;
                ++correctStreak_;
                host.beepOk();
                /* Auto-promote: six in a row means the current pool is too easy.
                 * The tier button still overrides this by hand. */
                if (correctStreak_ >= 6 && tier_ < 3) {
                    ++tier_;
                    correctStreak_ = 0;
                    host.setScore("geoTier", tier_);
                }
            } else {
                correctStreak_ = 0;
                host.beepError();
            }
            feedbackUntil_ = now + 1500UL;
            phase_ = Phase::FeedbackCountry;
        } else {
            if (lastCorrect_) { ++capBonus_; host.beepOk(); }
            else              { host.beepError(); }
            feedbackUntil_ = now + 1600UL;
            phase_ = Phase::FeedbackCapital;
        }
        markDirty();
        return;
    }
}

void FlagGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    // --- status row ------------------------------------------------------
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(String(score_) + "/" + rounds_, 8, 33, 2);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Ui::rgb(255, 200, 0), Ui::bg());
    tft.drawString(String("+") + capBonus_, GAME_CANVAS_WIDTH - 8, 33, 2);

    static const char* const TIER_NAMES[4] = {"", "Easy", "Medium", "Hard"};
    Ui::drawButton(tft, tierRect(), TIER_NAMES[tier_], Ui::panel(), Ui::outline(), Ui::text(), false, 1);

    if (current_ == nullptr) {
        Ui::drawLabel(tft, Rect{20, 110, 280, 20}, "No countries available",
                      Ui::error(), 2, Align::Center);
        return;
    }

    // --- flag ------------------------------------------------------------
    const Rect fr = flagRect();
    tft.fillRect(fr.x - 2, fr.y - 2, fr.w + 4, fr.h + 4, FLAG_BG);
    tft.drawRect(fr.x - 2, fr.y - 2, fr.w + 4, fr.h + 4, Ui::outline());
    // 80x60 source drawn at 2x fills the 160x120 card exactly.
    Ui::drawCountryImageScaled(tft, mnf_flag(current_->iso2), fr, FLAG_BG, 2);

    // --- prompt ----------------------------------------------------------
    const bool capitalRound = (phase_ == Phase::CapitalBonus || phase_ == Phase::FeedbackCapital);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(capitalRound ? Ui::rgb(255, 200, 0) : Ui::text(), Ui::bg());
    const char* countryName = mnf_name(current_->iso2);
    tft.drawString(capitalRound
                       ? String("Bonus! Capital of ") + (countryName ? countryName : current_->iso2) + "?"
                       : String("Which country?"),
                   GAME_CANVAS_WIDTH / 2, 176, 2);

    // --- answers ---------------------------------------------------------
    const bool showingFeedback =
        (phase_ == Phase::FeedbackCountry || phase_ == Phase::FeedbackCapital);

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const CountryFact* opt = options_[i];
        if (opt == nullptr) continue;

        uint16_t fill = Ui::panel();
        uint16_t tc   = Ui::text();
        if (showingFeedback) {
            if (i == correctBtn_) { fill = Ui::success(); tc = TFT_BLACK; }
            else if (i == static_cast<uint8_t>(selected_)) { fill = Ui::error(); tc = TFT_BLACK; }
        }

        const char* raw = capitalRound ? opt->capital : mnf_name(opt->iso2);
        const Rect r = answerRect(i);
        Ui::drawButton(tft, r, fitLabel(tft, raw ? raw : opt->iso2, r.w - 10, 2),
                       fill, Ui::outline(), tc, false, 2);
    }

    tft.setTextDatum(TL_DATUM);
}
