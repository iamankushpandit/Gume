#pragma once

#include <Arduino.h>
#include "engine/AppRegistry.h"
#include "engine/ContentLoader.h"
#include "engine/Game.h"
#include "engine/LauncherGame.h"
#include "games/GameInstances.h"
#include "hal/Board.h"
#include "ui/ScaledRenderer.h"
#include "ui/TftRenderer.h"
#include "ui/Ui.h"

class BrainoApp : public GameHost {
public:
    void begin();
    void loop();

    Ui::Renderer& display() override;

    /** Reset per-paint renderer state so no screen inherits another's. */
    void beginScreenPaint();
    Board& board() override;
    bool hasCapability(uint32_t capability) const override;
    bool requireCapability(uint32_t capability, const char* action) override;
    uint8_t launcherEntryCount() override;
    uint8_t launcherPageSize() override;
    const AppDefinition& launcherEntry(uint8_t filteredIndex) override;
    void openApp(const AppDefinition& app) override;
    ContentLoader& content() override;
    uint32_t getScore(const char* key, uint32_t fallback = 0) override;
    void setScore(const char* key, uint32_t value) override;
    bool saveBestScore(const char* key, uint32_t value, bool lowerIsBetter) override;
    void loadBlob(const char* key, void* dst, size_t len) override;
    void saveBlob(const char* key, const void* src, size_t len) override;
    void beepOk() override;
    void beepError() override;
    void playSound(Sound cue) override;
    void pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) override;
    void drawTopBar(const char* title) override;
    void goHome() override;
    void relaunchActiveGame() override;
    void openSettings() override;
    void openWifi() override;
    void openProfiles() override;

    /* Lock now: blank the panel immediately, and make the next wake land on
     * the wake lock instead of on the screen underneath.
     *
     * Same guard, reached deliberately rather than by timing out. It is for a
     * console being carried, handed over or dropped in a bag with a game still
     * open -- so it sleeps through the ordinary enterSleep(), leaves
     * activeGame_ alone, and a completed hold resumes exactly what was up.
     * Called from the runtime's own touch routing, above the active screen, so
     * the tap that locks is never also delivered to what was under it. */
    void lockAndSleepNow();

    static constexpr uint32_t FRAME_BUDGET_MS = 20;
    static constexpr uint32_t SLEEP_POLL_MS = 100;

    /* Long enough that a press through a bag cannot complete it, short enough
     * that a four-year-old holding a button does not give up. */
    static constexpr uint32_t LOCK_HOLD_MS = 900;
    /* Resistive contact drops out mid-hold. Forgive gaps up to this long. */
    static constexpr uint32_t LOCK_CONTACT_GRACE_MS = 150;
    /* Nobody unlocked: go back where we came from rather than sitting lit. */
    static constexpr uint32_t LOCK_TIMEOUT_MS = 12000;

private:
    enum class View {
        Game,
        ScreenSaver,
        Asleep,
        /* Between the saver/sleep and the screen underneath. The panel is lit
         * and showing an unlock target, but nothing beneath it can be
         * touched. See enterLock(). */
        Locked
    };

    uint8_t effectiveRotation(bool landscape);
    /** The rotation the screen that is up right now asks for. */
    uint8_t rotationForActiveScreen();
    void applyRotation(uint8_t rotation);
    void leaveActiveGame();
    const char* activeAppTitle() const;

    const AppDefinition& appById(const char* id) const;
    void launch(const AppDefinition& app);

    void enterScreenSaver();
    void enterSleep();
    void wakeFromSleep();
    void exitScreenSaver();

    /* The lock screen -- an accidental-touch guard, nothing to do with the
     * admin PIN. Coming out of the saver or out of panel sleep, a single
     * stray press used to land on whatever screen was underneath, live. Now
     * it lands here instead, and only a deliberate press-and-hold on the
     * unlock target hands the device back.
     *
     * enterLock() is reached from both View::ScreenSaver (panel already lit)
     * and View::Asleep (panel woken by the caller, inside its Watchdog::Pause
     * guard). resumeUnderlyingScreen() is the shared tail that both the old
     * direct-exit paths and the unlock use, so there is one place that decides
     * what you come back to and in which orientation. */
    void enterLock();
    /** Where the Lock button is on whatever screen is up right now. */
    Rect activeLockRect();
    void updateLock(const TouchPoint& touch, uint32_t nowMs);
    void renderLock();
    void resumeUnderlyingScreen();
    /** Unlock target, laid out against the live panel so portrait works. */
    Rect lockButtonRect();
    /** Progress bar under it, same width. */
    Rect lockProgressRect();
    /** 0-100, how far through the hold we are. */
    uint8_t lockProgressPercent(uint32_t nowMs) const;
    void onScreenSaverHit();
    void resetScreenSaverRally(int16_t effW, int16_t effH);
    void renderScreenSaver();
    /* Paint the current header notification, if there is one. Called after the
     * screen has drawn itself, so it lands on top. The battery warning wins
     * over a Nearby event: one of them is a nicety, the other is the reason
     * the console is about to switch off. */
    void drawHeaderBanner(bool screenRepainted);

    /* Ask for a header-only repaint. Cheap enough to call on every tick that
     * changes a badge; the flag collapses several in one frame into one
     * strip repaint, and a screen already due a full render absorbs it. */
    void requestChromeRender();

    /* Raise, escalate and retire the "charge me" strip. Cheap enough to call
     * every frame -- it reads the cached battery sample and returns early
     * until BATTERY_CHECK_MS has passed. */
    void tickBatteryWarning(uint32_t nowMs);

    /* Mark the strip as needing paint and make the screen under it redraw. */
    void requestBannerRepaint();

    /* Long enough to read at a glance, and repeated rarely enough that it
     * stays a warning rather than becoming furniture a player learns to ignore.
     * A low battery is not an emergency: it has tens of minutes left at the
     * point the strip first appears. */
    static constexpr uint32_t BATTERY_CHECK_MS = 2000;
    static constexpr uint32_t BATTERY_BANNER_MS = 6000;
    static constexpr uint32_t BATTERY_REPEAT_MS = 120000;

    Board board_;
    /* Playable games are drawn through scaledRenderer_; system screens draw
     * through renderer_ directly. See display().
     *
     * The split exists because the two kinds of screen solve the size problem
     * differently. System apps already lay themselves out against the live
     * tft.width()/height(), so a bigger panel simply gives them more room.
     * Playable games are authored against the fixed GAME_CANVAS and position
     * everything by arithmetic on it -- there is no responsive layout to fall
     * back on, so the canvas is stretched to fill the panel instead.
     *
     * textScale is 1, and the reasoning that said 2 was wrong on hardware.
     *
     * The argument for 2 was that this board exists for players who need
     * bigger, plainer text, so text should gain on its boxes rather than
     * merely keep pace. The flaw is that glyph scale is whole-number only:
     * the layout grows ~1.4x here, so 2x is not "a bit bigger", it is 43%
     * bigger than its box, and text overflowed or was truncated across the
     * games. A label that does not fit is not more legible.
     *
     * The value went untested for a while because TftRenderer::drawString
     * forced setTextSize(1), so this 2 never reached the panel -- the games
     * had always drawn at 1x. Fixing that so the launcher could scale its own
     * text let the 2 through for the first time, and it broke every screen at
     * once. Two lessons, both worth keeping: a setting that cannot take effect
     * is not a tested setting, and legibility on this board has to come from
     * layouts that can carry a larger font, not from multiplying the font
     * under a layout that cannot. The launcher does it properly -- it grows
     * its tiles first and only then asks for larger text, and falls back when
     * the label would not fit. */
    Ui::TftRenderer renderer_{board_.display()};
    Ui::ScaledRenderer scaledRenderer_{renderer_, /*textScale=*/1};
    ContentLoader content_;
    GameInstances games_;
    LauncherGame launcher_;
    Game* activeGame_ = nullptr;
    const AppDefinition* activeApp_ = nullptr;
    /* True only while a playable game is the thing on screen.
     *
     * view_ == View::Game is load-bearing, not redundant. activeApp_ keeps
     * pointing at the last-launched game while the saver, sleep or the lock
     * screen is up -- none of those goes through launch(), so nothing clears
     * it -- and all three lay themselves out against the real panel like any
     * other system view. Without the view check they would inherit the scale
     * of whichever game happened to be open beforehand. */
    bool activeAppIsPlayable() const {
        return view_ == View::Game && activeApp_ != nullptr && activeApp_->isCatalogApp();
    }

    View view_ = View::Game;
    uint32_t heapAtLaunch_ = 0;

    float ssav_bx_ = 160.0f;
    float ssav_by_ = 120.0f;
    float ssav_bvx_ = 2.8f;
    float ssav_bvy_ = 1.6f;
    float ssav_ly_ = 120.0f;
    float ssav_ry_ = 120.0f;
    bool ssav_initialized_ = false;
    View ssavPrevView_ = View::Game;
    bool swallowTouch_ = false;

    /* Lock-screen state.
     *
     * lockContactMs_ is what makes the hold usable on a resistive panel: a
     * held press drops below TOUCH_PRESSURE_THRESHOLD intermittently, which is
     * normal rather than a fault, so the timer survives up to
     * LOCK_CONTACT_GRACE_MS of no contact instead of resetting on the first
     * gap. lockPaintedPct_ keeps the frame cheap -- the progress fill is
     * repainted when its width actually changes, not every frame. */
    uint32_t lockHoldStartMs_ = 0;
    uint32_t lockContactMs_ = 0;
    uint32_t lockActivityMs_ = 0;
    /* Set by the Lock button, cleared by resumeUnderlyingScreen(). It is what
     * makes an explicit lock land on the lock screen even for an owner who has
     * switched the wake lock off: they asked for this one. */
    bool lockOnWake_ = false;
    bool lockHolding_ = false;
    bool lockFullPaint_ = true;
    int16_t lockPaintedPct_ = -1;
    uint8_t ssav_hits_ = 0;
    uint16_t ssav_color_ = 0;
    uint32_t ssav_lastFrameMs_ = 0;
    int16_t ssav_textCy_ = -1;
    /* What the badge last showed. The saver runs for hours, so anything it
     * repaints per frame it repaints a quarter of a million times an hour. */
    int8_t ssav_battPct_ = -2;
    uint8_t ssav_battPower_ = 0xFF;

    uint32_t lastBannerGeneration_ = 0;
    bool bannerNeedsPaint_ = false;

    /* Something in the header changed -- the clock, the battery badge, a
     * notification -- but nothing the screen itself draws. Answered by
     * Game::renderChrome(), which repaints the 30px strip instead of the
     * 240px panel. See requestChromeRender(). */
    bool chromeNeedsPaint_ = false;

    /* The battery warning currently on screen, or nullptr. Points at a string
     * literal, so there is no buffer here to keep in step. */
    const char* batteryBanner_ = nullptr;
    uint32_t batteryCheckMs_ = 0;
    uint32_t batteryShownMs_ = 0;
    uint32_t batteryHiddenMs_ = 0;
    bool batteryEverHidden_ = false;
    uint8_t batteryWarnLevel_ = 0;   // 0 none, 1 low, 2 critical
    uint32_t lastClockMinute_ = 0;
    uint32_t lastActivityMs_ = 0;
    uint32_t screenSaverStartMs_ = 0;

    /* Track battery state to invalidate screens when charging state or
     * percentage changes, so the battery badge updates in real-time without
     * waiting for a screen switch. */
    Board::ChargingState lastChargingState_ = Board::ChargingState::UNKNOWN;
    int8_t lastBatteryPercent_ = -1;
};
