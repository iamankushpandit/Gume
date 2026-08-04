#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class SimonGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Phase : uint8_t {
        Showing,
        Waiting,
        Good,
        Failed
    };

    Rect padRect(uint8_t index) const;
    void appendStep();
    void startShowing();
    int8_t touchedPad(int16_t x, int16_t y) const;
    uint16_t padColor(uint8_t index, bool lit) const;

    uint8_t sequence_[32] = {};
    uint8_t length_ = 0;
    uint8_t showIndex_ = 0;
    uint8_t inputIndex_ = 0;
    int8_t litPad_ = -1;
    Phase phase_ = Phase::Showing;
    uint32_t nextAt_ = 0;
    uint16_t score_ = 0;
    uint16_t bestScore_ = 0;
};

