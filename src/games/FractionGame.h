#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class FractionGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    enum class Mode : uint8_t {
        PickText,
        PickPie,
        Compare
    };

    struct Fraction {
        uint8_t numerator = 1;
        uint8_t denominator = 2;
    };

    uint8_t level() const;
    Mode nextMode() const;
    Fraction randomFraction() const;
    Fraction randomComparableFraction(const Fraction& other, bool allowSameDenominator) const;
    bool sameFraction(const Fraction& a, const Fraction& b) const;
    bool greaterThan(const Fraction& a, const Fraction& b) const;
    String fractionText(const Fraction& fraction) const;
    bool optionExists(const Fraction& fraction, uint8_t upTo) const;
    void newRound();
    void makeOptions(const Fraction& correct);
    void markCorrect(AppContext& host);
    void markWrong(int8_t index);

    Rect optionRect(uint8_t index) const;
    Rect pieOptionRect(uint8_t index) const;
    Rect compareRect(uint8_t index) const;

    void drawPie(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t radius, const Fraction& fraction, bool selected = false) const;
    void fillSlice(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t radius, float startAngle, float endAngle, uint16_t color) const;
    void drawTextOptions(TFT_eSPI& tft) const;
    void drawPieOptions(TFT_eSPI& tft) const;
    void drawCompare(TFT_eSPI& tft) const;

    Mode mode_ = Mode::PickText;
    Fraction target_;
    Fraction other_;
    Fraction options_[4];
    uint8_t correctButton_ = 0;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    int8_t flashIndex_ = -1;
    uint32_t flashUntil_ = 0;
    bool roundComplete_ = false;
};
