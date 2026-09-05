#pragma once

#include "BoardConfig.h"
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
    /* Two-phase. The tab bodies draw over each other's ground rather than
     * erasing themselves, so renderDynamic() clears the body REGION -- not the
     * screen. What that saves is the top bar and the tab strip, and the top bar
     * is not cheap: it reads the battery and draws five glyphs.
     * See docs/RENDER_AUDIT.md. */
    void renderStatic(GameHost& host) override;
    void renderDynamic(GameHost& host) override;

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
    /* The live panel, refreshed at the top of update() and render().
     *
     * The rect helpers below are shared by hit testing and drawing -- which is
     * what keeps a control's target under its glyph -- so they must all agree
     * about the panel. Passing two arguments through forty call sites would
     * have said the same thing forty times and given forty chances to say it
     * differently; caching it once per frame says it in one place. Seeded with
     * the board's landscape size so the helpers are sane before the first
     * paint, and refreshed on entry so a rotation is picked up. */
    int16_t panelW_ = SCREEN_WIDTH;
    int16_t panelH_ = SCREEN_HEIGHT;
    void syncPanel(GameHost& host);

    /* Shared grid, measured against the cached panel. Two columns for the
     * paired controls, one full-width row for the rest. */
    int16_t gridRowPitch() const;
    int16_t gridRowHeight() const;
    Rect gridCell(uint8_t row, uint8_t col) const;   // col 0/1
    Rect gridWide(uint8_t row) const;                // spans both columns

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
