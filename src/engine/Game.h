#pragma once

#include <Arduino.h>
#include "engine/ContentLoader.h"
#include "hal/Board.h"

class GameHost {
public:
    virtual ~GameHost() = default;
    virtual Board& board() = 0;
    virtual ContentLoader& content() = 0;
    virtual void goHome() = 0;
    virtual void relaunchActiveGame() = 0;
};

class Game {
public:
    virtual ~Game() = default;
    virtual const char* title() const = 0;
    virtual void begin(GameHost& host) = 0;
    virtual void update(GameHost& host, const TouchPoint& touch) = 0;
    virtual void render(GameHost& host) = 0;

    bool needsRender() const {
        return dirty_;
    }

    void clearDirty() {
        dirty_ = false;
    }

    void requestRender() {
        dirty_ = true;
    }

protected:
    void markDirty() {
        dirty_ = true;
    }

private:
    bool dirty_ = true;
};
