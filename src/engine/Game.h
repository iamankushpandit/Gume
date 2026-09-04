#pragma once

#include <Arduino.h>
#include "engine/AppCapabilities.h"
#include "hal/Sound.h"
#include "hal/TouchTypes.h"
#include "ui/Renderer.h"

struct AppDefinition;
class Board;
class ContentLoader;

class AppContext {
public:
    virtual ~AppContext() = default;
    virtual Ui::Renderer& display() = 0;
    virtual ContentLoader& content() = 0;
    virtual uint32_t getScore(const char* key, uint32_t fallback = 0) = 0;
    virtual void setScore(const char* key, uint32_t value) = 0;
    virtual bool saveBestScore(const char* key, uint32_t value, bool lowerIsBetter) = 0;
    virtual void loadBlob(const char* key, void* dst, size_t len) = 0;
    virtual void saveBlob(const char* key, const void* src, size_t len) = 0;
    virtual void beepOk() = 0;
    virtual void beepError() = 0;
    /* The rest of the console's sound vocabulary -- hal/Sound.h says what
     * each cue means, and a game should pick the one that matches what just
     * happened rather than the one that sounds nicest. Silent on a board with
     * no codec, so nothing may depend on it having been heard. */
    virtual void playSound(Sound cue) = 0;
    virtual void pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) = 0;
    virtual void drawTopBar(const char* title) = 0;
    virtual void goHome() = 0;
    virtual void relaunchActiveGame() = 0;
};

class GameHost : public AppContext {
public:
    virtual ~GameHost() = default;
    // Reserved for system screens that genuinely need full device authority.
    virtual Board& board() = 0;
    virtual bool hasCapability(uint32_t capability) const = 0;
    virtual bool requireCapability(uint32_t capability, const char* action) = 0;
    virtual uint8_t launcherEntryCount() = 0;
    virtual uint8_t launcherPageSize() = 0;
    virtual const AppDefinition& launcherEntry(uint8_t filteredIndex) = 0;
    virtual void openApp(const AppDefinition& app) = 0;
    virtual void openSettings() = 0;
    virtual void openWifi() = 0;
    virtual void openProfiles() = 0;
};

class Game {
public:
    virtual ~Game() = default;
    virtual const char* title() const = 0;
    virtual void begin(GameHost& host) = 0;
    virtual void update(GameHost& host, const TouchPoint& touch) = 0;
    virtual void render(GameHost& host) = 0;

    /* Called exactly once when the screen is left, before the next screen's
     * begin(). Default does nothing, which is correct for the games -- they
     * hold only their own member state and it is reset by begin().
     *
     * Override it if a screen acquires anything that outlives a frame: a
     * cached buffer, a sampling cadence, a radio or file handle. Nothing here
     * runs off a task or timer today, so nothing keeps burning cycles after
     * you leave it; this hook exists so that stays true as screens grow. */
    virtual void end(GameHost& host) { (void)host; }

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

    /* Repaint the header strip alone, leaving the screen under it untouched.
     *
     * The battery badge, the clock and the notification banner all change on
     * their own schedule rather than the screen's, and the runtime used to
     * answer that with requestRender() -- a full 320x240 wipe, ~150KB over SPI
     * and roughly 30ms of visible blanking, for a change confined to the top
     * 30 pixels. The strip is an eighth of the panel, so this costs about 4ms
     * and fits inside the frame budget where a full repaint is 150% of it.
     *
     * It mattered most for the battery: one percent is about 2mV on the LiPo
     * plateau, under two ADC counts, so the reading crosses a boundary every
     * couple of seconds -- and with the BLE beacon advertising, its supply
     * ripple made that constant. The whole screen flashed each time.
     *
     * The default is the standard top bar, which is what every screen that has
     * one draws, always with title(). A screen carrying its own header --
     * LauncherGame, ProfileGame -- overrides this. Returning false means "I
     * cannot repaint my chrome in isolation"; the runtime falls back to a full
     * repaint, so a screen that is unsure should say so rather than guess. */
    virtual bool renderChrome(GameHost& host) {
        host.drawTopBar(title());
        return true;
    }

protected:
    /* Two levels of invalidation.
     *
     * Every game used to clear the whole 320x240 screen on any change. At
     * 40MHz SPI that is ~150KB pushed and roughly 30ms of visible wipe before
     * anything is drawn again, which is what made the UI flicker -- and in
     * Cinnamon's case flash hard enough to be a photosensitivity concern.
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

class AppGame : public Game {
public:
    virtual void begin(AppContext& host) = 0;
    virtual void update(AppContext& host, const TouchPoint& touch) = 0;
    virtual void render(AppContext& host) = 0;
    virtual void end(AppContext& host) { (void)host; }

    void begin(GameHost& host) final {
        begin(static_cast<AppContext&>(host));
    }

    void update(GameHost& host, const TouchPoint& touch) final {
        update(static_cast<AppContext&>(host), touch);
    }

    void render(GameHost& host) final {
        render(static_cast<AppContext&>(host));
    }

    void end(GameHost& host) final {
        end(static_cast<AppContext&>(host));
    }
};
