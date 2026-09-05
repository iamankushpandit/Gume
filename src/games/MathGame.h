#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& mathAppMetadata();

class MathGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. The equation panel belongs to the question, so it is static:
     * answering recolours at most two of the four buttons and rewrites one
     * line, and the sum above them does not move. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    enum class Operation : uint8_t {
        Add,
        Subtract
    };

    Rect answerRect(uint8_t index) const;
    void newQuestion();
    void makeOptions();
    uint8_t level() const;
    bool optionExists(int16_t value, uint8_t upTo) const;
    uint16_t elapsedSeconds() const;
    String formatSeconds(uint16_t seconds) const;
    void updateBest(AppContext& host);

    int16_t left_ = 0;
    int16_t right_ = 0;
    int16_t answer_ = 0;
    int16_t options_[4] = {};

    /* What each button currently shows: 0 unanswered, 1 correct, 2 wrong.
     * Answering changes at most TWO of the four -- the right one turns green
     * and the chosen one turns red -- and only ONE when the player was right,
     * because then they are the same button. */
    uint8_t drawnButton_[4] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnStreak_ = 0xFFFF;
    bool drawnAnswered_ = false;
    bool drawnHeader_ = false;
    uint8_t correctButton_ = 0;
    Operation operation_ = Operation::Add;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestCorrect_ = 0;
    uint16_t bestSeconds_ = 0;
    uint32_t startedAt_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
