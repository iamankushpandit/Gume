#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class PercentCircleGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class RoundType : uint8_t {
        ReadCircle,
        MakeCircle,
        PercentOfNumber
    };

    uint16_t score_;
    uint8_t streak_;
    int8_t correctButton_;
    int8_t flashIndex_;
    unsigned long flashUntil_;
    RoundType roundType_;
    uint8_t targetPercent_;
    uint8_t currentPercent_;
    uint8_t baseNumber_;
    uint8_t options_[4];
    char promptBuffer_[64];
    bool roundComplete_;

    uint8_t level() const {
        return min<uint8_t>(5, 1 + streak_ / 5);
    }

    RoundType nextRoundType() const;
    void newRound(GameHost& host);
    void markCorrect(GameHost& host);
    void markWrong();
    void generateOptions();

    Rect optionRect(uint8_t index) const {
        const int16_t col = index % 2;
        const int16_t row = index / 2;
        return Rect{static_cast<int16_t>(164 + col * 76), static_cast<int16_t>(96 + row * 44), 70, 38};
    }

    Rect minusRect() const {
        return Rect{164, 152, 34, 34};
    }

    Rect valueRect() const {
        return Rect{200, 152, 38, 34};
    }

    Rect plusRect() const {
        return Rect{240, 152, 34, 34};
    }

    Rect okRect() const {
        return Rect{276, 152, 34, 34};
    }

    void fillSlice(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t radius, float startAngle, float endAngle, uint16_t color) const;
    void drawCircle(TFT_eSPI& tft, uint8_t percent, bool highlight) const;
    void drawReadCircleMode(TFT_eSPI& tft) const;
    void drawMakeCircleMode(TFT_eSPI& tft) const;
    void drawPercentOfNumberMode(TFT_eSPI& tft) const;
};
