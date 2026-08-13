#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class AboutGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    Rect panelRect(int16_t w, int16_t h) const;
    Rect prevRect(int16_t w, int16_t h) const;
    Rect nextRect(int16_t w, int16_t h) const;
    void drawLine(TFT_eSPI& tft, int16_t y, const String& text, uint8_t font = 2) const;
    void renderIntro(TFT_eSPI& tft);
    void renderGames(TFT_eSPI& tft, int16_t w);
    void renderRadios(TFT_eSPI& tft, Board& board);
    void renderCredits(TFT_eSPI& tft);

    uint8_t page_ = 0;
};
