#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class AboutApp : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    Rect panelRect(int16_t w, int16_t h) const;
    Rect prevRect(int16_t w, int16_t h) const;
    Rect nextRect(int16_t w, int16_t h) const;
    void drawLine(Ui::Renderer& tft, int16_t y, const String& text, uint8_t font = 2) const;
    void renderIntro(Ui::Renderer& tft);
    void renderGames(Ui::Renderer& tft, int16_t w);
    void renderRadios(Ui::Renderer& tft, Board& board);
    void renderPassword(Ui::Renderer& tft);
    void renderWarranty(Ui::Renderer& tft);
    /* Only reached on a board whose profile wires a BOOT key; see
     * CONTROLS_PAGES in the .cpp. */
    void renderControls(Ui::Renderer& tft);
    void renderCredits(Ui::Renderer& tft);
    void renderBuild(Ui::Renderer& tft);
    /* Takes the board because every value on it is read from the device --
     * what is installed, what the last check found, whether the check is even
     * switched on. Nothing on this page is a stored sentence. */
    void renderUpdates(Ui::Renderer& tft, Board& board);

    uint8_t page_ = 0;
};
