#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& cinnamonAppMetadata();

class CinnamonGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    enum class Phase : uint8_t {
        Showing,
        Waiting,
        Good,
        Failed
    };

    Rect padBand(const Ui::Frame& f) const;
    Rect padRect(const Ui::Frame& f, uint8_t index) const;
    void drawPad(Ui::Renderer& tft, const Ui::Frame& f, uint8_t index, bool lit) const;

    /* Cinnamon used to repaint the whole screen on every step, so each colour in
     * the sequence produced a full-screen flash -- uncomfortable generally and
     * a real risk for anyone photosensitive. We now redraw only the pads whose
     * state actually changed, plus the one-line status strip. */
    bool fullRedraw_ = true;
    bool litDrawn_[4] = {false, false, false, false};
    String statusDrawn_;
    void appendStep();
    void startShowing();
    int8_t touchedPad(const Ui::Frame& f, int16_t x, int16_t y) const;
    uint16_t padColor(uint8_t index, bool lit) const;

    uint8_t sequence_[32] = {};
    uint8_t length_ = 0;
    uint8_t showIndex_ = 0;
    uint8_t inputIndex_ = 0;
    int8_t litPad_ = -1;
    Phase phase_ = Phase::Showing;
    uint32_t nextAt_ = 0;
    uint16_t score_ = 0;
    uint16_t bestScore_ = 0;
};

