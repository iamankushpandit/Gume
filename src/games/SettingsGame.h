#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

/*
 * Device preferences, in two tabs.
 *
 * Device holds what the console looks like; Power holds what it does when
 * nobody is touching it. They were one screen until the idle policy arrived --
 * the grid was already seven buttons and a slider, with nowhere to put three
 * more controls.
 */
class SettingsGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Tab : uint8_t { Device, Power };

    Rect deviceTabRect(int16_t W) const;
    Rect powerTabRect(int16_t W) const;

    Rect themeRect(int16_t W) const;
    Rect layoutRect(int16_t W) const;
    Rect ntpRect(int16_t W) const;
    Rect bleRect(int16_t W) const;
    Rect wifiRect(int16_t W) const;
    Rect nearbyRect(int16_t W) const;
    Rect resetRect(int16_t W) const;
    Rect brightRect(int16_t W, int16_t H) const;

    Rect idleActionRect(int16_t W) const;
    Rect idleAfterRect(int16_t W) const;
    Rect sleepAfterRect(int16_t W) const;

    void cycleScreenSaver(Board& board);
    void cycleSleepSeconds(Board& board);
    void cycleIdleAction(Board& board);
    void renderDeviceTab(GameHost& host);
    void renderPowerTab(GameHost& host);

    /* True when the Sleep-after row is inert: SaverOnly never blanks, and
     * SleepOnly blanks at the idle delay instead. A live-looking control that
     * does nothing is worse than one that says so. */
    bool sleepRowActive(Board& board) const;

    Tab tab_ = Tab::Device;
    bool confirmReset_ = false;
};
