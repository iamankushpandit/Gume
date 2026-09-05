#pragma once

#include "engine/Game.h"
#include "engine/ContentLoader.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& countingAppMetadata();

class CountingGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. The objects to be counted belong to the round, so the panel
     * and every dot in it are static; answering recolours at most two of the
     * four buttons. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    Rect answerRect(uint8_t index) const;
    void newQuestion();
    void makeOptions();

    CountingConfig config_;
    uint8_t count_ = 1;
    uint8_t options_[4] = {};

    /* What each button shows: 0 unanswered, 1 correct, 2 wrong. */
    uint8_t drawnButton_[4] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnStreak_ = 0xFFFF;
    bool drawnAnswered_ = false;
    bool drawnStats_ = false;
    uint8_t correctButton_ = 0;
    uint8_t score_ = 0;
    uint8_t rounds_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
