#pragma once

#include "engine/Game.h"
#include "engine/Progress.h"
#include "engine/RecentQuestions.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"
#include "StateData.h"

struct AppMetadata;

const AppMetadata& statesAppMetadata();

/*
 * US States and capitals.
 *
 * Alternates both directions -- "capital of Texas?" and "Austin is the capital
 * of?" -- because recognising a capital is not the same skill as recalling one.
 *
 * Difficulty tiers mirror the flag game: Easy 9, Medium 27, Hard all 50.
 */
class StatesGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;
    void end(AppContext& host) override;

private:
    enum class Mode  : uint8_t { CapitalOf, WhichState };
    enum class Phase : uint8_t { Asking, Feedback };

    static constexpr uint8_t OPTION_COUNT = 4;

    Rect answerBand(const Ui::Frame& f) const;
    Rect answerRect(const Ui::Frame& f, uint8_t i) const;
    Rect tierRect(const Ui::Frame& f) const;
    uint8_t poolSize() const;
    const StateFact* fromPool(uint8_t index) const;
    void newQuestion();

    Progress progress_;
    RecentQuestions recent_;
    uint8_t tier_ = 1;
    uint8_t correctStreak_ = 0;

    Mode  mode_  = Mode::CapitalOf;
    Phase phase_ = Phase::Asking;

    const StateFact* current_ = nullptr;
    const StateFact* options_[OPTION_COUNT] = {};
    uint8_t correctBtn_ = 0;
    int8_t  selected_   = -1;
    bool    lastCorrect_ = false;

    uint32_t feedbackUntil_ = 0;
    uint16_t score_  = 0;
    uint16_t rounds_ = 0;
};
