#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"
#include "CountryData.h"
#include "engine/Progress.h"

struct AppMetadata;

const AppMetadata& flagAppMetadata();

/*
 * Guess the Flag.
 *
 * Shows a real flag image from the map-n-flag library and asks which country
 * it belongs to. Answer correctly and you get a bonus question for that
 * country's capital city.
 *
 * Difficulty tier is shared with GeoGame and persisted as "geoTier":
 *   1 = ~30 most familiar countries, 2 = ~62, 3 = all 195.
 */
class FlagGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;
    void end(AppContext& host) override;

private:
    enum class Phase : uint8_t {
        Country,          // "Which country is this flag?"
        FeedbackCountry,
        CapitalBonus,     // "What is its capital?"
        FeedbackCapital
    };

    static constexpr uint8_t OPTION_COUNT = 4;
    static constexpr uint8_t RECENT_COUNT = 8;

    Rect answerRect(uint8_t i, int16_t screenW) const;
    Rect tierRect() const;
    Rect flagRect() const;

    bool recentlyUsed(const char* iso2) const;
    void rememberCurrent();
    void makeOptions();
    void newQuestion();

    /* Spaced repetition: countries answered wrong come back sooner. Shared
     * key with GeoGame would conflate two different skills, so each has its
     * own record. */
    Progress progress_;
    uint8_t correctStreak_ = 0;
    uint8_t tier_ = 1;
    Phase phase_ = Phase::Country;

    const CountryFact* current_ = nullptr;
    const CountryFact* options_[OPTION_COUNT] = {};
    uint8_t correctBtn_ = 0;
    int8_t  selected_   = -1;
    bool    lastCorrect_ = false;

    const char* recent_[RECENT_COUNT] = {};
    uint8_t recentPos_ = 0;

    uint32_t feedbackUntil_ = 0;
    uint16_t score_    = 0;
    uint16_t rounds_   = 0;
    uint16_t capBonus_ = 0;
};
