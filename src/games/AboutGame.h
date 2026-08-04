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
    Rect prevRect() const;
    Rect nextRect() const;
    void drawLine(TFT_eSPI& tft, int16_t y, const String& text, uint8_t font = 2) const;

    uint8_t page_ = 0;
};
