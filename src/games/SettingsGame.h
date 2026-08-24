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
    enum class Mode : uint8_t { Locked, PinEntry, Unlocked };

    Rect deviceTabRect() const;
    Rect powerTabRect() const;
    Rect pinKeyRect(uint8_t row, uint8_t col) const;
    Rect pinDeleteRect() const;
    Rect pinConfirmRect() const;

    Rect themeRect() const;
    Rect layoutRect() const;
    Rect ntpRect() const;
    Rect bleRect() const;
    Rect wifiRect() const;
    Rect nearbyRect() const;
    Rect resetRect() const;
    Rect brightRect() const;

    Rect idleActionRect() const;
    Rect idleAfterRect() const;
    Rect sleepAfterRect() const;

    void cycleScreenSaver(Board& board);
    void cycleSleepSeconds(Board& board);
    void cycleIdleAction(Board& board);
    void renderDeviceTab(GameHost& host);
    void renderPowerTab(GameHost& host);
    void renderLockedScreen(GameHost& host);
    void renderPinEntry(GameHost& host);

    /* True when the Sleep-after row is inert: SaverOnly never blanks, and
     * SleepOnly blanks at the idle delay instead. A live-looking control that
     * does nothing is worse than one that says so. */
    bool sleepRowActive(Board& board) const;
    bool isAdmin(Board& board) const;

    void appendPinDigit(uint8_t digit);
    void deletePinDigit();

    Tab tab_ = Tab::Device;
    bool confirmReset_ = false;
    Mode mode_ = Mode::Locked;
    uint16_t enteredPin_ = 0;
};
