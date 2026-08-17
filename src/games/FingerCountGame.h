#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& fingerCountAppMetadata();

/*
 * Finger Counting.
 *
 * Trains counting on hands in BOTH directions, alternating each round:
 *
 *   Count  -- a number of fingers is already raised; the child counts them and
 *             taps the matching number.
 *   ShowMe -- a number is given; the child raises that many fingers.
 *
 * The old version asked "3 + 4 = ?" and expected the child to tap 7 fingers,
 * which required knowing the answer before touching the hands -- it tested
 * arithmetic rather than teaching finger counting.
 */
class FingerCountGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    enum class Mode  : uint8_t { Count, ShowMe };
    enum class Phase : uint8_t { Active, Feedback };

    static constexpr uint8_t FINGER_COUNT = 10;
    static constexpr uint8_t OPTION_COUNT = 4;

    /* Hand geometry derived from the panel rather than fixed at the 320x240
     * numbers it was drawn with. Two hands of five need 8*pitch + 80px of
     * width; 320 allows the authored 26px pitch and 240 does not, so the right
     * hand used to start at x=172 and run to 298 -- straight off a narrower
     * panel. */
    static constexpr int16_t PALM_H = 30;
    static constexpr int16_t MAX_PITCH = 26;

    int16_t fingerPitch(const Ui::Frame& f) const;
    int16_t fingerWidth(const Ui::Frame& f) const;
    int16_t handX0(const Ui::Frame& f, uint8_t hand) const;
    int16_t palmTop(const Ui::Frame& f) const;
    int16_t palmBottom(const Ui::Frame& f) const;

    Rect answerBand(const Ui::Frame& f) const;
    Rect fingerRect(const Ui::Frame& f, uint8_t idx) const;
    Rect answerRect(const Ui::Frame& f, uint8_t i) const;
    int16_t fingerTop(const Ui::Frame& f, uint8_t idx) const;

    uint8_t raisedCount() const;
    void drawHand(Ui::Renderer& tft, const Ui::Frame& f, uint8_t hand) const;
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
