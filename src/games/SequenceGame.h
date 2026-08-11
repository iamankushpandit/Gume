#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class SequenceGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Mode : uint8_t { Days, Months };
    enum class QType : uint8_t { After, Before };

    static const char* DAY_NAMES[7];
    static const char* MONTH_NAMES[12];

    Rect modeBtn(uint8_t m) const;
    Rect answerTile(uint8_t i) const;
    void newQuestion();
    const char* itemName(uint8_t idx) const;
    uint8_t itemCount() const;

    Mode   mode_       = Mode::Days;
    QType  qtype_      = QType::After;
    uint8_t subject_   = 0;
    uint8_t correct_   = 0;
    uint8_t correctPos_= 0;
    uint8_t options_[4]= {};
    int8_t  selected_  = -1;
    bool    answered_  = false;
    uint32_t feedbackUntil_ = 0;
    uint16_t score_  = 0;
    uint16_t rounds_ = 0;
};
