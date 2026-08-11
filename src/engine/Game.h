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
    virtual void openSettings() = 0;
    virtual void openWifi() = 0;
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
        fullRedraw_ = false;
    }

    void requestRender() {
        dirty_ = true;
        fullRedraw_ = true;   // coming back from elsewhere: repaint everything
    }

protected:
    /* Two levels of invalidation.
     *
     * Every game used to clear the whole 320x240 screen on any change. At
     * 40MHz SPI that is ~150KB pushed and roughly 30ms of visible wipe before
     * anything is drawn again, which is what made the UI flicker -- and in
     * Simon's case flash hard enough to be a photosensitivity concern.
     *
     * markDirty()     content changed; repaint the moving parts only.
     * markFullDirty() layout/structure changed; repaint the background too.
     *
     * A render() should paint its static chrome under `if (needsFullRender())`
     * and its dynamic parts unconditionally. */
    void markDirty() {
        dirty_ = true;
    }

    void markFullDirty() {
        dirty_ = true;
        fullRedraw_ = true;
    }

    bool needsFullRender() const {
        return fullRedraw_;
    }

private:
    bool dirty_ = true;
    bool fullRedraw_ = true;   // first paint is always a full one
};
