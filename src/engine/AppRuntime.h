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

    uint32_t lastClockMinute_ = 0;
    uint32_t lastActivityMs_ = 0;
    uint32_t screenSaverStartMs_ = 0;
};
