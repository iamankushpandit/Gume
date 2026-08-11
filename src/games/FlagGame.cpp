#include "FlagGame.h"
#include "map_n_flag.h"

namespace {
constexpr uint16_t FLAG_BG = 0xFFFF;   // white card behind the flag

/* Shrink a label until it fits the given width, adding an ellipsis. Country
 * names like "United Arab Emirates" do not fit a half-width button at font 2. */
String fitLabel(TFT_eSPI& tft, const String& in, int16_t maxW, uint8_t font) {
    if (tft.textWidth(in, font) <= maxW) return in;
    String s = in;
    while (s.length() > 1 && tft.textWidth(s + ".", font) > maxW) {
        s.remove(s.length() - 1);
    }
    return s + ".";
}
}

const char* FlagGame::title() const { return "Guess the Flag"; }

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

void FlagGame::newQuestion() {
    const uint16_t pool = countryPoolSize(tier_, false);
    if (pool == 0) { current_ = nullptr; return; }

    // Avoid repeating anything from the last few rounds when the pool allows.
    for (uint8_t attempt = 0; attempt < 40; ++attempt) {
        const CountryFact* c = countryFromPool(
            static_cast<uint16_t>(random(pool)), tier_, false);
        if (c == nullptr) continue;
        current_ = c;
        if (pool <= RECENT_COUNT || !recentlyUsed(c->iso2)) break;
    }
    rememberCurrent();

    selected_ = -1;
    phase_ = Phase::Country;
    feedbackUntil_ = 0;
    makeOptions();
}

void FlagGame::begin(GameHost& host) {
    tier_ = static_cast<uint8_t>(host.board().getScore("geoTier", 1));
    if (tier_ < 1 || tier_ > 3) tier_ = 1;
    score_ = 0; rounds_ = 0; capBonus_ = 0;
    for (uint8_t i = 0; i < RECENT_COUNT; ++i) recent_[i] = nullptr;
    recentPos_ = 0;
    newQuestion();
    markDirty();
}

void FlagGame::update(GameHost& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if ((phase_ == Phase::FeedbackCountry || phase_ == Phase::FeedbackCapital) &&
        now >= feedbackUntil_) {
        if (phase_ == Phase::FeedbackCountry && lastCorrect_) {
            // Earned the capital bonus round for this same country.
            selected_ = -1;
            phase_ = Phase::CapitalBonus;
        } else {
            newQuestion();
        }
        markDirty();
        return;
    }

    if (!touch.justPressed) return;

    // Difficulty cycles Easy -> Medium -> Hard and restarts the round.
    if (tierRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        tier_ = static_cast<uint8_t>(tier_ >= 3 ? 1 : tier_ + 1);
        host.board().setScore("geoTier", tier_);
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
            if (lastCorrect_) { ++score_; host.board().beepOk(); }
            else              { host.board().beepError(); }
            feedbackUntil_ = now + 1500UL;
            phase_ = Phase::FeedbackCountry;
        } else {
            if (lastCorrect_) { ++capBonus_; host.board().beepOk(); }
            else              { host.board().beepError(); }
            feedbackUntil_ = now + 1600UL;
            phase_ = Phase::FeedbackCapital;
        }
        markDirty();
        return;
    }
}

void FlagGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(tft, title());

    // --- status row ------------------------------------------------------
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(String(score_) + "/" + rounds_, 8, 33, 2);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Ui::rgb(255, 200, 0), Ui::bg());
    tft.drawString(String("+") + capBonus_, SCREEN_WIDTH - 8, 33, 2);

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
                   SCREEN_WIDTH / 2, 176, 2);

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
