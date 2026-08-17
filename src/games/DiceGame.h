#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& diceAppMetadata();

/* A dice roller: pick one, two or three dice and throw them.
 *
 * There is no score. A "best total" here would be pure luck, reachable in a
 * handful of taps and then frozen at 18 forever, so it would only add a dead
 * row to the Scores app. The metadata leaves score null, as Trace does. */
class DiceGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    static constexpr uint8_t MAX_DICE = 3;
    /* How long the faces tumble, and how often they change while they do.
     * 90ms is fast enough to read as a throw rather than a slideshow, and slow
     * enough that a child can see individual faces go past. */
    static constexpr uint32_t TUMBLE_MS = 1000;
    static constexpr uint32_t TUMBLE_STEP_MS = 90;

    /* All four take the live panel rather than assuming 320x240. Roll anchors
     * to the bottom edge and the dice size themselves to the width, so the
     * same expressions give the authored landscape layout and a taller, more
     * spread-out portrait one. */
    Rect countRect(const Ui::Frame& f, uint8_t index) const;
    Rect rollRect(const Ui::Frame& f) const;
    Rect dieRect(const Ui::Frame& f, uint8_t index) const;
    /* The band that changes between throws. Everything outside it is chrome
     * and is painted only on a full render. */
    Rect activeBand(const Ui::Frame& f) const;
    /* Baseline for the total/status line: centred in the gap the dice and the
     * Roll button leave between them, which is 42px in landscape and 122 in
     * portrait. */
    int16_t statusBaseline(const Ui::Frame& f) const;

    void drawDie(Ui::Renderer& tft, const Rect& r, uint8_t face) const;
    void reroll();

    uint8_t count_ = 2;
    uint8_t faces_[MAX_DICE] = {1, 1, 1};
    bool rolling_ = false;
    bool thrown_ = false;   // false until the first throw, so no total is shown
    uint32_t rollUntilMs_ = 0;
    uint32_t nextTumbleMs_ = 0;
};
