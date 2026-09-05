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
    /* Two-phase. The flag is a 160x120 scaled blit and it does NOT change when
     * the round moves on to the capital bonus -- same country, same flag -- so
     * it belongs to the question rather than to the phase. See
     * docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
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

    /* What is on the panel. drawnCapital_ matters as much as the button
     * states: moving from the country round to the capital bonus keeps every
     * button in place and changes every LABEL, which a fill-colour comparison
     * alone would miss. */
    uint8_t drawnBtn_[OPTION_COUNT] = {};
    bool drawnCapital_ = false;
    bool drawnPrompt_ = false;
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnRounds_ = 0xFFFF;
    uint16_t drawnCapBonus_ = 0xFFFF;

    Rect answerRect(uint8_t i) const;
    Rect tierRect() const;
    Rect flagRect() const;

    bool recentlyUsed(const char* iso2) const;
    void rememberCurrent();
    void makeOptions();
    void shuffleOptionsForBonus();
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
