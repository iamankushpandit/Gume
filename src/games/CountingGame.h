#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class CountingGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    Rect answerRect(uint8_t index) const;
    void newQuestion();
    void makeOptions();

    CountingConfig config_;
    uint8_t count_ = 1;
    uint8_t options_[4] = {};
    uint8_t correctButton_ = 0;
    uint8_t score_ = 0;
    uint8_t rounds_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
