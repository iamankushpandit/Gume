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
    enum class Tab : uint8_t { Device, Power, Admin };

    /* Settings is readable by everyone and writable only by the admin, so
     * there is no lock screen here -- the PIN pad exists purely to set a new
     * PIN, which is entered twice. */
    enum class PinTask : uint8_t { None, SetNew, ConfirmNew };

    Rect deviceTabRect() const;
    Rect powerTabRect() const;
    Rect adminTabRect() const;
    Rect changePinRect() const;
    Rect pinCancelRect() const;
    Rect pinKeyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH) const;
    Rect pinDeleteRect(int16_t screenW, int16_t screenH) const;
    Rect pinConfirmRect(int16_t screenW, int16_t screenH) const;

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
    Rect wakeLockRect() const;

    void cycleScreenSaver(Board& board);
    void cycleSleepSeconds(Board& board);
    void cycleIdleAction(Board& board);
    void renderDeviceTab(GameHost& host);
    void renderPowerTab(GameHost& host);
    void renderAdminTab(GameHost& host);
    void renderPinPad(GameHost& host, const char* heading);

    /* Shared by the unlock screen and the change-PIN flow. Returns true if the
     * touch landed on the pad, so callers can stop looking. */
    bool handlePinPadTouch(GameHost& host, const TouchPoint& touch);

    /* True when the Sleep-after row is inert: SaverOnly never blanks, and
     * SleepOnly blanks at the idle delay instead. A live-looking control that
     * does nothing is worse than one that says so. */
    bool sleepRowActive(Board& board) const;
    bool isAdmin(Board& board) const;

    void appendPinDigit(uint8_t digit);
    void deletePinDigit();

    Tab tab_ = Tab::Device;
    bool confirmReset_ = false;
    uint16_t enteredPin_ = 0;
    /* Tracked separately from the value: "0000" and an empty field are the
     * same number, so the value alone cannot say how many digits are in. */
    uint8_t enteredPinDigits_ = 0;

    PinTask pinTask_ = PinTask::None;
    uint16_t pendingPin_ = 0;   // first entry of a new PIN, awaiting confirmation
};
