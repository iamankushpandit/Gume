#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

// Agent B owns this file — see src/games/CLAUDE.md
class PercentCircleGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;
};
