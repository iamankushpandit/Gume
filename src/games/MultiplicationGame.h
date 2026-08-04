#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class MultiplicationGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    Rect answerRect(uint8_t index) const;
    void newQuestion();
    void makeOptions();
    uint8_t level() const;
    uint8_t pickTable(uint8_t currentLevel) const;
    bool optionExists(int16_t value, uint8_t upTo) const;
    void updateBest(GameHost& host);

    int16_t left_ = 0;
    int16_t right_ = 0;
    int16_t answer_ = 0;
    int16_t options_[4] = {};
    uint8_t correctButton_ = 0;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
