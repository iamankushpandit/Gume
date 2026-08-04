#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class MathGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Operation : uint8_t {
        Add,
        Subtract
    };

    Rect answerRect(uint8_t index) const;
    void newQuestion();
    void makeOptions();
    uint8_t level() const;
    bool optionExists(int16_t value, uint8_t upTo) const;
    uint16_t elapsedSeconds() const;
    String formatSeconds(uint16_t seconds) const;
    void updateBest(GameHost& host);

    int16_t left_ = 0;
    int16_t right_ = 0;
    int16_t answer_ = 0;
    int16_t options_[4] = {};
    uint8_t correctButton_ = 0;
    Operation operation_ = Operation::Add;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestCorrect_ = 0;
    uint16_t bestSeconds_ = 0;
    uint32_t startedAt_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
