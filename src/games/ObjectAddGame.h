#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class ObjectAddGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Phase : uint8_t { Showing, AnimIn, Flashing, Question, Feedback };
    enum class OpType : uint8_t { Add, Subtract };

    Rect leftPanel() const;
    Rect rightPanel() const;
    Rect answerRect(uint8_t i) const;
    void objPos(const Rect& panel, uint8_t idx, int16_t& cx, int16_t& cy) const;
    void drawObject(TFT_eSPI& tft, int16_t cx, int16_t cy, uint16_t color) const;
    void newQuestion();
    void makeOptions();

    Phase phase_ = Phase::Showing;
    OpType op_   = OpType::Add;
    uint8_t n1_  = 2;
    uint8_t n2_  = 3;
    uint8_t shape_ = 0;
    uint8_t animCount_ = 0;
    bool     flashOn_    = false;
    uint32_t animNextAt_ = 0;
    uint32_t feedbackUntil_ = 0;
    uint8_t options_[4] = {};
    uint8_t correctBtn_ = 0;
    int8_t  selected_   = -1;
    uint16_t score_  = 0;
    uint16_t rounds_ = 0;
};
