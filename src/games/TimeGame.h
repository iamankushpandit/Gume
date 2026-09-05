#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& timeGameAppMetadata();

class TimeGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase render. renderStatic() is the clear and the top bar;
     * renderDynamic() is everything else. Split mechanically by
     * tools/split_render.py -- see the note in the .cpp. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    Rect answerRect(uint8_t index) const;
    void newQuestion();
    void makeOptions();
    bool optionExists(uint16_t minutes, uint8_t upTo) const;
    String formatTime(uint16_t minutes) const;
    uint8_t level() const;
    void drawClock(Ui::Renderer& tft) const;

    uint16_t answerMinutes_ = 0;
    uint16_t options_[4] = {};
    uint8_t correctButton_ = 0;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
