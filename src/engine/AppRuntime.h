#pragma once

#include <Arduino.h>
#include "engine/AppRegistry.h"
#include "engine/ContentLoader.h"
#include "engine/Game.h"
#include "engine/LauncherGame.h"
#include "games/GameInstances.h"
#include "hal/Board.h"
#include "ui/TftRenderer.h"
#include "ui/Ui.h"

class KidsPlatformApp : public GameHost {
public:
    void begin();
    void loop();

    Ui::Renderer& display() override;
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
    void pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) override;
    void drawTopBar(const char* title) override;
    void goHome() override;
    void relaunchActiveGame() override;
    void openSettings() override;
    void openWifi() override;
    void openProfiles() override;

    static constexpr uint32_t FRAME_BUDGET_MS = 20;
    static constexpr uint32_t SLEEP_POLL_MS = 100;

private:
    enum class View {
        Game,
        ScreenSaver,
        Asleep
    };

    static constexpr uint8_t CYD_PORTRAIT_ROTATION = 0;

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
    void onScreenSaverHit();
    void resetScreenSaverRally(int16_t effW, int16_t effH);
    void renderScreenSaver();
    /* Paint the current header notification, if there is one. Called after the
     * screen has drawn itself, so it lands on top. The battery warning wins
     * over a Nearby event: one of them is a nicety, the other is the reason
     * the console is about to switch off. */
    void drawHeaderBanner(bool screenRepainted);

    /* Raise, escalate and retire the "charge me" strip. Cheap enough to call
     * every frame -- it reads the cached battery sample and returns early
     * until BATTERY_CHECK_MS has passed. */
    void tickBatteryWarning(uint32_t nowMs);

    /* Mark the strip as needing paint and make the screen under it redraw. */
    void requestBannerRepaint();

    /* Long enough to read at a glance, and repeated rarely enough that it
     * stays a warning rather than becoming furniture a child learns to ignore.
     * A low battery is not an emergency: it has tens of minutes left at the
     * point the strip first appears. */
    static constexpr uint32_t BATTERY_CHECK_MS = 2000;
    static constexpr uint32_t BATTERY_BANNER_MS = 6000;
    static constexpr uint32_t BATTERY_REPEAT_MS = 120000;

    Board board_;
    Ui::TftRenderer renderer_{board_.display()};
    ContentLoader content_;
    GameInstances games_;
    LauncherGame launcher_;
    Game* activeGame_ = nullptr;
    const AppDefinition* activeApp_ = nullptr;
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
    uint8_t ssav_hits_ = 0;
    uint16_t ssav_color_ = 0;
    uint32_t ssav_lastFrameMs_ = 0;
    int16_t ssav_textCy_ = -1;

    uint32_t lastBannerGeneration_ = 0;
    bool bannerNeedsPaint_ = false;

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
};
