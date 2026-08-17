#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& percentCircleAppMetadata();

class PercentCircleGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

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
    void newRound(AppContext& host);
    void markCorrect(AppContext& host);
    void markWrong();
    void generateOptions();

    /* The only screen in the catalog that genuinely needs two layouts.
     *
     * It is a circle beside a column of controls, and that column needs about
     * 146px starting at x=164 -- 310px in total. A 240px panel cannot give it
     * that at any inset, so portrait puts the controls *under* the circle
     * instead of beside it. Everything else derives from this one rect. */
    Rect controlsRect(const Ui::Frame& f) const;
    Rect stepperBand(const Ui::Frame& f) const;
    Rect optionRect(const Ui::Frame& f, uint8_t index) const;
    Rect minusRect(const Ui::Frame& f) const;
    Rect valueRect(const Ui::Frame& f) const;
    Rect plusRect(const Ui::Frame& f) const;
    Rect okRect(const Ui::Frame& f) const;
    int16_t circleCx(const Ui::Frame& f) const;

    void fillSlice(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, float startAngle, float endAngle, uint16_t color) const;
    void drawCircle(Ui::Renderer& tft, const Ui::Frame& f, uint8_t percent, bool highlight) const;
    void drawReadCircleMode(Ui::Renderer& tft, const Ui::Frame& f) const;
    void drawMakeCircleMode(Ui::Renderer& tft, const Ui::Frame& f) const;
    void drawPercentOfNumberMode(Ui::Renderer& tft, const Ui::Frame& f) const;
};
