#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class WhackAMoleGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    Rect cellRect(uint8_t index) const;
    int8_t touchedCell(int16_t x, int16_t y) const;
    void spawnMole();
    void recordMiss();
    uint8_t level() const;
    uint16_t visibleMs() const;
    void drawSmile(TFT_eSPI& tft, const Rect& r) const;

    int8_t activeCell_ = -1;
    int8_t flashCell_ = -1;
    uint16_t score_ = 0;
    uint16_t bestScore_ = 0;
    uint8_t missStreak_ = 0;
    uint32_t expiresAt_ = 0;
    uint32_t nextSpawnAt_ = 0;
    uint32_t flashUntil_ = 0;
    bool flashSuccess_ = false;
    bool gameOver_ = false;
};
