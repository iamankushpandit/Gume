#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& multiplicationAppMetadata();

class MultiplicationGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    /* Same shape as Math, and deliberately the same geometry: a prompt panel
     * over four choices, arranged 2x2 in landscape and stacked in portrait. */
    Rect equationRect(const Ui::Frame& f) const;
    Rect answerBand(const Ui::Frame& f) const;
    Rect answerRect(const Ui::Frame& f, uint8_t index) const;
    void newQuestion();
    void makeOptions();
    uint8_t level() const;
    uint8_t pickTable(uint8_t currentLevel) const;
    bool optionExists(int16_t value, uint8_t upTo) const;
    void updateBest(AppContext& host);

    int16_t left_ = 0;
    int16_t right_ = 0;
    int16_t answer_ = 0;
    int16_t options_[4] = {};
    uint8_t correctButton_ = 0;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
