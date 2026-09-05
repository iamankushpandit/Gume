#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& multiplicationAppMetadata();

class MultiplicationGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. Same shape as MathGame, which is the reference: the equation
     * panel belongs to the question and is static; answering recolours at most
     * two of the four buttons. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    Rect answerRect(uint8_t index) const;
    void newQuestion();
    void makeOptions();
    uint8_t level() const;
    uint8_t pickTable(uint8_t currentLevel) const;
    bool optionExists(int16_t value, uint8_t upTo) const;
    void updateBest(AppContext& host);

    int16_t left_ = 0;
    int16_t right_ = 0;
    int16_t answer_ = 0;
    int16_t options_[4] = {};

    /* What each button shows: 0 unanswered, 1 correct, 2 wrong. */
    uint8_t drawnButton_[4] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnStreak_ = 0xFFFF;
    bool drawnAnswered_ = false;
    bool drawnHeader_ = false;
    uint8_t correctButton_ = 0;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
