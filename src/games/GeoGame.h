#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"
#include "CountryData.h"
#include "engine/Progress.h"

/*
 * Guess the Country.
 *
 * Shows a real country outline from the map-n-flag library and alternates
 * between two questions:
 *   - "Which country is this?"      (4 country names)
 *   - "Which continent is it in?"   (up to 6 continents)
 *
 * The continent round deliberately reuses the country outline rather than
 * drawing a continent shape, so every image on screen is real geography.
 *
 * Difficulty tier is shared with FlagGame and persisted as "geoTier":
 *   1 = ~30 most familiar countries, 2 = ~62, 3 = all 195.
 */
class GeoGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Mode : uint8_t { WhichCountry, WhichContinent };
    enum class Phase : uint8_t { Asking, Feedback };

    static constexpr uint8_t MAX_OPTIONS = 6;
    static constexpr uint8_t RECENT_COUNT = 8;

    Rect answerRect(uint8_t i) const;
    Rect tierRect() const;
    Rect mapRect() const;
    uint8_t optionCount() const;

    bool recentlyUsed(const char* iso2) const;
    void rememberCurrent();
    void makeCountryOptions();
    void makeContinentOptions();
    void newQuestion();

    Progress progress_;
    uint8_t correctStreak_ = 0;
    uint8_t tier_ = 1;
    Mode  mode_  = Mode::WhichCountry;
    Phase phase_ = Phase::Asking;

    const CountryFact* current_ = nullptr;
    const CountryFact* countryOptions_[MAX_OPTIONS] = {};
    uint8_t continentOptions_[MAX_OPTIONS] = {};
    uint8_t correctBtn_ = 0;
    int8_t  selected_   = -1;
    bool    lastCorrect_ = false;

    const char* recent_[RECENT_COUNT] = {};
    uint8_t recentPos_ = 0;

    uint32_t feedbackUntil_ = 0;
    uint16_t score_  = 0;
    uint16_t rounds_ = 0;
};
