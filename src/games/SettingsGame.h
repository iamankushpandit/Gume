#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
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

    Rect deviceTabRect(const Ui::Frame& f) const;
    Rect powerTabRect(const Ui::Frame& f) const;

    /* Full-width band for settings row `row`, counting from under the tab
     * baseline. Paired controls take half of one via Ui::gridCell. */
    Rect rowBand(const Ui::Frame& f, uint8_t row) const;

    Rect themeRect(const Ui::Frame& f) const;
    Rect layoutRect(const Ui::Frame& f) const;
    Rect ntpRect(const Ui::Frame& f) const;
    Rect bleRect(const Ui::Frame& f) const;
    Rect wifiRect(const Ui::Frame& f) const;
    Rect nearbyRect(const Ui::Frame& f) const;
    Rect resetRect(const Ui::Frame& f) const;
    Rect brightRect(const Ui::Frame& f) const;

    Rect idleActionRect(const Ui::Frame& f) const;
    Rect idleAfterRect(const Ui::Frame& f) const;
    Rect sleepAfterRect(const Ui::Frame& f) const;

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
