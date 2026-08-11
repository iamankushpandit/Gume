#include "GeoGame.h"
#include "map_n_flag.h"

namespace {
constexpr uint16_t MAP_BG  = 0xFFFF;                  // white "paper"
constexpr uint16_t MAP_INK = 0x18F1;                  // navy silhouette

String fitLabel(TFT_eSPI& tft, const String& in, int16_t maxW, uint8_t font) {
    if (tft.textWidth(in, font) <= maxW) return in;
    String s = in;
    while (s.length() > 1 && tft.textWidth(s + ".", font) > maxW) {
        s.remove(s.length() - 1);
    }
    return s + ".";
}
}

const char* GeoGame::title() const { return "Guess the Country"; }

Rect GeoGame::mapRect() const  { return Rect{110, 46, 100, 124}; }
Rect GeoGame::tierRect() const { return Rect{132, 31, 56, 16}; }

uint8_t GeoGame::optionCount() const {
    return mode_ == Mode::WhichCountry ? 4 : CONTINENT_COUNT;
}

/* Country round: 2x2 wide buttons (names are long).
 * Continent round: 3x2 grid, six fixed continents. */
Rect GeoGame::answerRect(uint8_t i) const {
    if (mode_ == Mode::WhichCountry) {
        const int16_t col = i % 2;
        const int16_t row = i / 2;
        return Rect{static_cast<int16_t>(6 + col * 158),
                    static_cast<int16_t>(186 + row * 27), 150, 25};
    }
    const int16_t col = i % 3;
    const int16_t row = i / 3;
    return Rect{static_cast<int16_t>(6 + col * 104),
                static_cast<int16_t>(186 + row * 27), 100, 25};
}

bool GeoGame::recentlyUsed(const char* iso2) const {
    for (uint8_t i = 0; i < RECENT_COUNT; ++i) {
        if (recent_[i] != nullptr && strcmp(recent_[i], iso2) == 0) return true;
    }
    return false;
}

void GeoGame::rememberCurrent() {
    if (current_ == nullptr) return;
    recent_[recentPos_] = current_->iso2;
    recentPos_ = static_cast<uint8_t>((recentPos_ + 1) % RECENT_COUNT);
}

void GeoGame::makeCountryOptions() {
    const uint16_t pool = countryPoolSize(tier_, true);
    correctBtn_ = static_cast<uint8_t>(random(4));
    for (uint8_t i = 0; i < 4; ++i) countryOptions_[i] = current_;

    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctBtn_) continue;
        for (uint8_t attempt = 0; attempt < 60; ++attempt) {
            const CountryFact* cand = countryFromPool(
                static_cast<uint16_t>(random(pool)), tier_, true);
            if (cand == nullptr || cand == current_) continue;
            bool dup = false;
            for (uint8_t j = 0; j < i; ++j) {
                if (countryOptions_[j] == cand) { dup = true; break; }
            }
            if (dup) continue;
            countryOptions_[i] = cand;
            break;
        }

        // Deterministic fallback, so a slot never stays equal to current_ and
        // renders the correct answer twice.
        if (countryOptions_[i] == current_) {
            for (uint16_t k = 0; k < pool; ++k) {
                const CountryFact* cand = countryFromPool(k, tier_, true);
                if (cand == nullptr || cand == current_) continue;
                bool dup = false;
                for (uint8_t j = 0; j < 4; ++j) {
                    if (j != i && countryOptions_[j] == cand) { dup = true; break; }
                }
                if (dup) continue;
                countryOptions_[i] = cand;
                break;
            }
        }
    }
}

void GeoGame::makeContinentOptions() {
    // All six continents are always shown, in a fixed order so the buttons do
    // not jump around between rounds. That is easier for a child to learn.
    for (uint8_t i = 0; i < CONTINENT_COUNT; ++i) continentOptions_[i] = i;
    correctBtn_ = current_ != nullptr ? current_->continent : 0;
}

void GeoGame::newQuestion() {
    const uint16_t pool = countryPoolSize(tier_, true);
    if (pool == 0) { current_ = nullptr; return; }

    for (uint8_t attempt = 0; attempt < 40; ++attempt) {
        const CountryFact* c = countryFromPool(
            static_cast<uint16_t>(random(pool)), tier_, true);
        if (c == nullptr) continue;
        current_ = c;
        if (pool <= RECENT_COUNT || !recentlyUsed(c->iso2)) break;
    }
    rememberCurrent();

    // Alternate question types so both skills get practised.
    mode_ = (random(2) == 0) ? Mode::WhichCountry : Mode::WhichContinent;
    if (mode_ == Mode::WhichCountry) makeCountryOptions();
    else                             makeContinentOptions();

    selected_ = -1;
    phase_ = Phase::Asking;
    feedbackUntil_ = 0;
}

void GeoGame::begin(GameHost& host) {
    tier_ = static_cast<uint8_t>(host.board().getScore("geoTier", 1));
    if (tier_ < 1 || tier_ > 3) tier_ = 1;
    score_ = 0; rounds_ = 0;
    for (uint8_t i = 0; i < RECENT_COUNT; ++i) recent_[i] = nullptr;
    recentPos_ = 0;
    newQuestion();
    markDirty();
}

void GeoGame::update(GameHost& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (phase_ == Phase::Feedback && now >= feedbackUntil_) {
        newQuestion();
        markDirty();
        return;
    }

    if (!touch.justPressed) return;

    if (tierRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        tier_ = static_cast<uint8_t>(tier_ >= 3 ? 1 : tier_ + 1);
        host.board().setScore("geoTier", tier_);
        for (uint8_t i = 0; i < RECENT_COUNT; ++i) recent_[i] = nullptr;
        newQuestion();
        markDirty();
        return;
    }

    if (phase_ != Phase::Asking || current_ == nullptr) return;

    const uint8_t n = optionCount();
    for (uint8_t i = 0; i < n; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;

        selected_ = static_cast<int8_t>(i);
        lastCorrect_ = (i == correctBtn_);
        ++rounds_;
        if (lastCorrect_) { ++score_; host.board().beepOk(); }
        else              { host.board().beepError(); }
        feedbackUntil_ = now + 1700UL;
        phase_ = Phase::Feedback;
        markDirty();
        return;
    }
}

void GeoGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(tft, title());

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(String(score_) + "/" + rounds_, 8, 33, 2);

    static const char* const TIER_NAMES[4] = {"", "Easy", "Medium", "Hard"};
    Ui::drawButton(tft, tierRect(), TIER_NAMES[tier_], Ui::panel(), Ui::outline(), Ui::text(), false, 1);

    if (current_ == nullptr) {
        Ui::drawLabel(tft, Rect{20, 110, 280, 20}, "No countries available",
                      Ui::error(), 2, Align::Center);
        return;
    }

    // --- outline on a white card ----------------------------------------
    const Rect mr = mapRect();
    tft.fillRect(mr.x, mr.y, mr.w, mr.h, MAP_BG);
    tft.drawRect(mr.x, mr.y, mr.w, mr.h, Ui::outline());
    Ui::drawCountryImageTinted(tft, mnf_map(current_->iso2), mr, MAP_BG, MAP_INK);

    // In the continent round the country is named, since the question is about
    // where it sits rather than what it is.
    const char* countryName = mnf_name(current_->iso2);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    if (mode_ == Mode::WhichContinent && countryName != nullptr) {
        // Cap the width so long names cannot run into the tier button at x=188.
        tft.drawString(fitLabel(tft, countryName, 118, 2), SCREEN_WIDTH - 8, 33, 2);
    }

    // --- prompt / feedback ----------------------------------------------
    tft.setTextDatum(MC_DATUM);
    if (phase_ == Phase::Feedback) {
        tft.setTextColor(lastCorrect_ ? Ui::success() : Ui::error(), Ui::bg());
        String msg;
        if (lastCorrect_) {
            msg = "Yes! ";
            msg += countryName ? countryName : current_->iso2;
            if (mode_ == Mode::WhichContinent) {
                msg += " is in ";
                msg += continentName(current_->continent);
            }
        } else {
            msg = "It's ";
            msg += (mode_ == Mode::WhichCountry)
                       ? (countryName ? countryName : current_->iso2)
                       : continentName(current_->continent);
        }
        tft.drawString(fitLabel(tft, msg, SCREEN_WIDTH - 16, 2), SCREEN_WIDTH / 2, 176, 2);
    } else {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(mode_ == Mode::WhichCountry ? "Which country is this?"
                                                   : "Which continent is it in?",
                       SCREEN_WIDTH / 2, 176, 2);
    }

    // --- answers ---------------------------------------------------------
    const uint8_t n = optionCount();
    for (uint8_t i = 0; i < n; ++i) {
        uint16_t fill = Ui::panel();
        uint16_t tc   = Ui::text();
        if (phase_ == Phase::Feedback) {
            if (i == correctBtn_) { fill = Ui::success(); tc = TFT_BLACK; }
            else if (i == static_cast<uint8_t>(selected_)) { fill = Ui::error(); tc = TFT_BLACK; }
        }

        const Rect r = answerRect(i);
        String label;
        if (mode_ == Mode::WhichCountry) {
            const CountryFact* opt = countryOptions_[i];
            if (opt == nullptr) continue;
            const char* nm = mnf_name(opt->iso2);
            label = nm ? nm : opt->iso2;
        } else {
            label = continentName(continentOptions_[i]);
        }

        Ui::drawButton(tft, r, fitLabel(tft, label, r.w - 8, 2),
                       fill, Ui::outline(), tc, false, 2);
    }

    tft.setTextDatum(TL_DATUM);
}
