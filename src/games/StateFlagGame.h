#pragma once

#include "engine/Game.h"
#include "engine/RecentQuestions.h"
#include "ui/Ui.h"
#include "StateData.h"

/*
 * Guess the US state from its flag.
 *
 * Shows a real state flag image from the map-n-flag library and asks which
 * state it belongs to. Answer correctly for a bonus capital question.
 *
 * Difficulty tiers reuse the StateData tiers:
 *   1 = 9 most familiar, 2 = 27, 3 = all 50.
 */
class StateFlagGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

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

    uint8_t poolSize() const;
    const StateFact* fromPool(uint8_t index) const;
    void makeOptions();
    void newQuestion();

    RecentQuestions recent_;
    uint8_t tier_ = 1;
    uint8_t correctStreak_ = 0;
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
