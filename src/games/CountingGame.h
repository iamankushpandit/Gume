#pragma once

#include "engine/Game.h"
#include "engine/ContentLoader.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& countingAppMetadata();

class CountingGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    /* Four small number buttons. Unlike Math these are compact enough to sit
     * in a single landscape row, so portrait folds them 2x2 rather than
     * stacking four -- Ui::answerColumns is tuned for wide buttons and would
     * give the wrong answer here. */
    uint8_t answerColumns(const Ui::Frame& f) const;
    Rect answerBand(const Ui::Frame& f) const;
    Rect answerRect(const Ui::Frame& f, uint8_t index) const;
    /* The panel the objects are counted in: everything between the stats line
     * and the buttons, which is 98px tall in landscape and 130 in portrait. */
    Rect objectArea(const Ui::Frame& f) const;
    void newQuestion();
    void makeOptions();

    CountingConfig config_;
    uint8_t count_ = 1;
    uint8_t options_[4] = {};
    uint8_t correctButton_ = 0;
    uint8_t score_ = 0;
    uint8_t rounds_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
