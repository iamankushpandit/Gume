#pragma once

#include "engine/Game.h"

class LauncherGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    void clampPage(GameHost& host);

    uint8_t page_ = 0;
};
