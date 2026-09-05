#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& fingerCountAppMetadata();

/*
 * Finger Counting.
 *
 * Trains counting on hands in BOTH directions, alternating each round:
 *
 *   Count  -- a number of fingers is already raised; the player counts them and
 *             taps the matching number.
 *   ShowMe -- a number is given; the player raises that many fingers.
 *
 * The old version asked "3 + 4 = ?" and expected the player to tap 7 fingers,
 * which required knowing the answer before touching the hands -- it tested
 * arithmetic rather than teaching finger counting.
 */
class FingerCountGame : public AppGame {
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
    enum class Mode  : uint8_t { Count, ShowMe };
    enum class Phase : uint8_t { Active, Feedback };

    static constexpr uint8_t FINGER_COUNT = 10;
    static constexpr uint8_t OPTION_COUNT = 4;

    // Hand geometry (landscape 320x240).
    static constexpr int16_t PALM_TOP    = 152;
    static constexpr int16_t PALM_BOTTOM = 182;
    static constexpr int16_t FINGER_W    = 22;
    static constexpr int16_t FINGER_PITCH = 26;
    static constexpr int16_t LEFT_X0     = 22;
    static constexpr int16_t RIGHT_X0    = 172;

    Rect fingerRect(uint8_t idx) const;
    Rect answerRect(uint8_t i) const;
    int16_t fingerTop(uint8_t idx) const;

    uint8_t raisedCount() const;
    void drawHand(Ui::Renderer& tft, uint8_t hand) const;
    void newQuestion();
    void makeOptions();

    Mode  mode_  = Mode::Count;
    Phase phase_ = Phase::Active;

    bool raised_[FINGER_COUNT] = {};
    uint8_t target_ = 5;               // the number being counted or shown
    uint8_t options_[OPTION_COUNT] = {};
    uint8_t correctBtn_ = 0;
    int8_t  selected_   = -1;
    bool    lastCorrect_ = false;

    uint32_t feedbackUntil_ = 0;
    uint16_t score_  = 0;
    uint16_t rounds_ = 0;
};
