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
#include "engine/Course.h"
#include "StateImageCourse.h"
#include "ui/Ui.h"
#include "StateData.h"

struct AppMetadata;

const AppMetadata& stateMapAppMetadata();

/*
 * Guess the US state from its outline.
 *
 * Shows a state map silhouette from the map-n-flag library and asks which
 * state it belongs to. Answer correctly for a bonus capital question.
 *
 * Difficulty tiers reuse the StateData tiers:
 *   1 = 9 most familiar, 2 = 27, 3 = all 50.
 */
class StateMapGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;
    /* No round ends, so the tally is the game: one answer is worth coming
     * back to, a tap on the difficulty alone is not. */
    Keep keepsGame() const override {
        return (current_ != nullptr && rounds_ > 0) ? Keep::Ask : Keep::No;
    }
    void resume(AppContext& host, uint32_t awayMs) override;

private:
    enum class Phase : uint8_t {
        State,
        FeedbackState,
        CapitalBonus,
        FeedbackCapital
    };

    static constexpr uint8_t OPTION_COUNT = 4;

    Rect answerRect(uint8_t i) const;
    Rect tierRect() const;
    Rect imageRect() const;

    void startLevel();
    bool canOffer(const StateFact* cand) const;
    void makeOptions();
    void shuffleOptionsForBonus();
    void newQuestion();
    bool climb(AppContext& host, bool right);

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    uint8_t pool_[64] = {};        // STATE_FACTS rows of the level
    uint8_t poolSize_ = 0;
    uint16_t item_ = 0;
    bool announce_ = false;        // the prompt names the new level
    Phase phase_ = Phase::State;

    const StateFact* current_ = nullptr;
    const StateFact* options_[OPTION_COUNT] = {};
    uint8_t correctBtn_ = 0;
    int8_t  selected_   = -1;
    bool    lastCorrect_ = false;

    uint32_t feedbackUntil_ = 0;
    uint16_t score_    = 0;
    uint16_t rounds_   = 0;
    uint16_t capBonus_ = 0;
};
