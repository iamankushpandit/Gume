#pragma once

#include "engine/Game.h"
#include "engine/Progress.h"
#include "ui/Ui.h"

// Agent C owns this file — see src/games/CLAUDE.md
class GreWordsGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;
    void end(GameHost& host) override;
};
