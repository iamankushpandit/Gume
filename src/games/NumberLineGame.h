#pragma once
#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& numberLineAppMetadata();

class NumberLineGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    enum class Phase : uint8_t { Pause, Jumping, Question, Feedback };

    Rect answerRect(uint8_t i) const;
    int16_t numToX(uint8_t n) const;
    void newQuestion();
    void makeOptions();

    Phase   phase_     = Phase::Pause;
    uint8_t n1_        = 2;
    uint8_t n2_        = 3;
    bool    isAdd_     = true;
    uint8_t animStep_  = 0;
    uint8_t options_[4]= {};
    uint8_t correctBtn_= 0;
    int8_t  selected_  = -1;
    bool    lastCorrect_= false;
    uint32_t nextAt_   = 0;
    uint32_t feedbackUntil_ = 0;
    uint16_t score_    = 0;
    uint16_t rounds_   = 0;
};
