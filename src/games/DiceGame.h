#pragma once

#include "engine/Game.h"
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
     * enough that a player can see individual faces go past. */
    static constexpr uint32_t TUMBLE_MS = 1000;
    static constexpr uint32_t TUMBLE_STEP_MS = 90;

    Rect countRect(uint8_t index) const;
    Rect rollRect() const;
    Rect dieRect(uint8_t index) const;
    /* The band that changes between throws. Everything outside it is chrome
     * and is painted only on a full render. */
    Rect activeBand() const;

    void drawDie(Ui::Renderer& tft, const Rect& r, uint8_t face) const;
    void reroll();

    uint8_t count_ = 2;
    uint8_t faces_[MAX_DICE] = {1, 1, 1};
    bool rolling_ = false;
    bool thrown_ = false;   // false until the first throw, so no total is shown
    uint32_t rollUntilMs_ = 0;
    uint32_t nextTumbleMs_ = 0;
};
