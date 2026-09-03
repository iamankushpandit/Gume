#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

/*
 * Device preferences, in four tabs.
 *
 * Device holds what the console looks like; Power holds what it does when
 * nobody is touching it; Sound holds how loud it is, or whether it speaks at
 * all; Admin holds the PIN and the touch calibration. They were one screen
 * until the idle policy arrived -- the grid was already seven buttons and a
 * slider, with nowhere to put three more controls.
 *
 * The implementation is split across three .cpp files by concern -- see the
 * header comment in SettingsGame.cpp. Every rect accessor is declared here,
 * in one place, so a control's geometry and the hit test that reads it cannot
 * drift apart however the files are arranged.
 */
class SettingsGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Tab : uint8_t { Device, Power, Sound, Admin };
    static constexpr uint8_t TAB_COUNT = 4;

    /* Settings is readable by everyone and writable only by the admin, so
     * there is no lock screen here -- the PIN pad exists purely to set a new
     * PIN, which is entered twice. */
    enum class PinTask : uint8_t { None, SetNew, ConfirmNew };

    /* One accessor, four tabs: the strip is an even division of the width, so
     * stating it once is what keeps them the same size. Adding a fifth tab is
     * a change to TAB_COUNT and nothing else -- the previous version wrote
     * SCREEN_WIDTH / 3 out three times with the last one fudged to absorb the
     * rounding, which is a thing to get wrong once per tab. */
    Rect tabRect(uint8_t index) const;
    Rect tabRectFor(Tab tab) const;
    Rect changePinRect() const;
    /* Re-runs the three-point touch wizard. It lives on Admin rather than
     * Device because Device has no room left and because this is maintenance,
     * not preference -- and it is the only way back from a calibration that
     * is present but wrong, which `hasTouchCalibration()` reports as fine and
     * so never re-runs on its own. */
    Rect recalibrateRect() const;
    Rect pinCancelRect() const;
    Rect pinKeyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH) const;
    Rect pinDeleteRect(int16_t screenW, int16_t screenH) const;
    Rect pinConfirmRect(int16_t screenW, int16_t screenH) const;

    Rect themeRect() const;
    Rect layoutRect() const;
    Rect lightRect() const;
    Rect bleRect() const;
    Rect wifiRect() const;
    Rect ntpSyncRect() const;
    Rect nearbyRect() const;
    Rect resetRect() const;
    Rect brightRect() const;

    /* Sound tab. Mute is a switch of its own rather than volume zero, so the
     * level survives being silenced and comes back where it was. The two test
     * buttons exist because a volume control you cannot hear while you set it
     * is guesswork -- one plays a cue, the other says the boot phrase. */
    Rect muteRect() const;
    Rect volumeRect() const;
    Rect testCueRect() const;
    Rect testVoiceRect() const;

    Rect idleActionRect() const;
    Rect idleAfterRect() const;
    Rect sleepAfterRect() const;
    Rect wakeLockRect() const;

    void cycleScreenSaver(Board& board);
    void cycleSleepSeconds(Board& board);
    void cycleIdleAction(Board& board);
    void cycleNtpResyncHours(Board& board);
    void renderDeviceTab(GameHost& host);
    void renderPowerTab(GameHost& host);
    void renderSoundTab(GameHost& host);
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
