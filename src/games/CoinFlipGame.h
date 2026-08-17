#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& coinFlipAppMetadata();

/* Best of one, three or five coin spins.
 *
 * Like Dice, this keeps no score: the outcome is a fair coin, so a stored
 * "best" would record luck and nothing else. Metadata leaves score null. */
class CoinFlipGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    static constexpr uint8_t MAX_FLIPS = 5;
    static constexpr uint8_t CHOICES = 3;             // best of 1, 3 or 5
    /* One spin, then a beat on the settled face before the next coin goes up.
     * SPIN_STEP_MS drives the edge-on squash; 45ms is about as slow as it can
     * go and still read as spinning rather than flickering. */
    static constexpr uint32_t SPIN_MS = 620;
    static constexpr uint32_t SPIN_STEP_MS = 45;
    static constexpr uint32_t HOLD_MS = 320;

    static uint8_t flipsFor(uint8_t choice) { return static_cast<uint8_t>(1 + choice * 2); }

    Rect choiceRect(const Ui::Frame& f, uint8_t index) const;
    Rect flipRect(const Ui::Frame& f) const;
    /* Coin, result pips and verdict -- everything that changes mid-sequence. */
    Rect activeBand(const Ui::Frame& f) const;
    /* The coin, the pip row and the verdict line share the band between the
     * choice buttons and the Flip button. That band is 120px tall in landscape
     * and 200 in portrait, so the three are placed as fractions of it rather
     * than at the fixed 116/166/187 the game was authored with. */
    int16_t coinCentreY(const Ui::Frame& f) const;
    int16_t pipRowY(const Ui::Frame& f) const;
    int16_t verdictY(const Ui::Frame& f) const;

    void drawCoin(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t halfWidth,
                  bool heads, bool edgeOn) const;
    uint8_t headsCount() const;

    uint8_t choice_ = 1;              // default best of 3
    bool heads_[MAX_FLIPS] = {false, false, false, false, false};
    uint8_t revealed_ = 0;            // how many of heads_ are settled
    bool busy_ = false;               // a sequence is running
    bool spinning_ = false;           // this coin is still in the air
    bool finished_ = false;           // a full sequence has been played
    uint8_t spinFrame_ = 0;
    uint32_t phaseUntilMs_ = 0;
    uint32_t nextSpinStepMs_ = 0;
};
