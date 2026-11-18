// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"
#include "CountryData.h"
#include "engine/Course.h"
#include "FlagCourse.h"

struct AppMetadata;

const AppMetadata& flagAppMetadata();

/*
 * Guess the Flag.
 *
 * Shows a real flag image from the map-n-flag library and asks which country
 * it belongs to. Answer correctly and you get a bonus question for that
 * country's capital city.
 *
 * A course of nine levels (FlagCourse.h): the player's level is kept with
 * courseLevel(), and the old "geoTier" is read once, for a player who has
 * no level yet.
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
    /* The tally is the game: there is no end to a round, so a flag answered
     * is a game a player would want back. A tap on the level alone is
     * not. */
    Keep keepsGame() const override {
        return (current_ != nullptr && rounds_ > 0) ? Keep::Ask : Keep::No;
    }
    void resume(AppContext& host, uint32_t awayMs) override;

private:
    enum class Phase : uint8_t {
        Country,          // "Which country is this flag?"
        FeedbackCountry,
        CapitalBonus,     // "What is its capital?"
        FeedbackCapital
    };

    static constexpr uint8_t OPTION_COUNT = 4;

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
    /* The flag card and the level button are dynamic, not static: a new
     * question changes the first and the ladder changes the second, and
     * neither should cost a full repaint. See renderDynamic(). */
    bool drawnFlag_ = false;
    uint8_t drawnLevel_ = 0xFF;

    Rect answerRect(uint8_t i) const;
    Rect tierRect() const;
    Rect flagRect() const;

    void startLevel();
    bool canOffer(const CountryFact* cand) const;
    void makeOptions();
    void shuffleOptionsForBonus();
    void newQuestion();
    bool climb(AppContext& host, bool right);

    static constexpr uint16_t MAX_POOL = 200;
    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    uint8_t pool_[MAX_POOL] = {};   // COUNTRY_FACTS rows of the level
    uint16_t poolSize_ = 0;
    uint16_t item_ = 0;
    bool announce_ = false;         // the prompt names the new level
    Phase phase_ = Phase::Country;

    const CountryFact* current_ = nullptr;
    const CountryFact* options_[OPTION_COUNT] = {};
    uint8_t correctBtn_ = 0;
    int8_t  selected_   = -1;
    bool    lastCorrect_ = false;


    uint32_t feedbackUntil_ = 0;
    uint16_t score_    = 0;
    uint16_t rounds_   = 0;
    uint16_t capBonus_ = 0;
};
