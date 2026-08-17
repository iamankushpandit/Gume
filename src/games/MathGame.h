#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& mathAppMetadata();

class MathGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    enum class Operation : uint8_t {
        Add,
        Subtract
    };

    /* The catalog's commonest shape: a prompt panel, then four choices.
     * Ui::answerColumns decides 2x2 or 1x4 from the panel proportions, so the
     * buttons stay finger-sized in both -- 320px of width wants two columns,
     * 240px with 80 more pixels of height wants one. */
    Rect equationRect(const Ui::Frame& f) const;
    Rect answerBand(const Ui::Frame& f) const;
    Rect answerRect(const Ui::Frame& f, uint8_t index) const;
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
