#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct ColorMixDefinition {
    const char* left;
    const char* right;
    const char* result;
    uint16_t leftColor;
    uint16_t rightColor;
    uint16_t resultColor;
};

struct AppMetadata;

const AppMetadata& colorMixAppMetadata();

class ColorMixGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    Rect answerBand(const Ui::Frame& f) const;
    Rect answerRect(const Ui::Frame& f, uint8_t index) const;
    /* The two colours being mixed, either side of the "+". */
    Rect swatchRect(const Ui::Frame& f, bool rightSwatch) const;
    void newQuestion();
    int8_t touchedAnswer(const Ui::Frame& f, int16_t x, int16_t y) const;

    const ColorMixDefinition* active_ = nullptr;
    const char* labels_[4] = {};
    uint16_t colors_[4] = {};
    uint8_t correct_ = 0;
    uint16_t score_ = 0;
    uint16_t attempts_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    bool correctFlash_ = false;
    bool wrongFlash_ = false;
    uint32_t flashUntil_ = 0;
};
