#include "AppRuntime.h"

#include <esp_system.h>
#include "hal/Clock.h"
#include "hal/Watchdog.h"
#include "ui/LauncherLayout.h"

namespace {
constexpr Rect HOME_BUTTON{0, 0, 44, TOP_BAR_HEIGHT};
}

Ui::Renderer& KidsPlatformApp::display() {
    return renderer_;
}

Board& KidsPlatformApp::board() {
    return board_;
}

bool KidsPlatformApp::hasCapability(uint32_t capability) const {
    return activeApp_ != nullptr && activeApp_->hasCapability(capability);
}

bool KidsPlatformApp::requireCapability(uint32_t capability, const char* action) {
    if (hasCapability(capability)) {
        return true;
    }
    Serial.printf("[auth] denied '%s' for app '%s' (need 0x%08lx, has 0x%08lx)\n",
                  action != nullptr ? action : "?",
                  activeApp_ != nullptr ? activeApp_->id() : "none",
                  static_cast<unsigned long>(capability),
                  static_cast<unsigned long>(activeApp_ != nullptr ? activeApp_->capabilities : 0));
    board_.beepError();
    return false;
}

ContentLoader& KidsPlatformApp::content() {
    return content_;
}

uint32_t KidsPlatformApp::getScore(const char* key, uint32_t fallback) {
    return board_.getScore(key, fallback);
}

void KidsPlatformApp::setScore(const char* key, uint32_t value) {
    board_.setScore(key, value);
}

bool KidsPlatformApp::saveBestScore(const char* key, uint32_t value, bool lowerIsBetter) {
    return board_.saveBestScore(key, value, lowerIsBetter);
}

void KidsPlatformApp::loadBlob(const char* key, void* dst, size_t len) {
    board_.loadBlob(key, dst, len);
}

void KidsPlatformApp::saveBlob(const char* key, const void* src, size_t len) {
    board_.saveBlob(key, src, len);
}

void KidsPlatformApp::beepOk() {
    board_.beepOk();
}

void KidsPlatformApp::beepError() {
    board_.beepError();
}

void KidsPlatformApp::pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
    board_.pulseRgb(r, g, b, ms);
}

void KidsPlatformApp::drawTopBar(const char* title) {
    Ui::drawTopBar(board_, title);
}

void KidsPlatformApp::begin() {
    board_.begin();
    Watchdog::begin();
    if (!board_.hasTouchCalibration()) {
        board_.runTouchCalibration();
    }
    randomSeed(static_cast<uint32_t>(esp_random()));
    content_.begin(board_);
    Clock::begin();
    Serial.printf("[boot] rot=%d layout=%s (CYD_SCREEN_ROTATION=%d)\n",
                  (int)board_.displayRotation(),
                  board_.layoutMode() == Board::LayoutMode::Vertical ? "Vertical" : "Horizontal",
                  (int)CYD_SCREEN_ROTATION);
    Serial.printf("[boot] ntp=%d creds=%d ssid='%s' tzmin=%d\n",
                  (int)board_.ntpEnabled(), (int)board_.hasWifiCredentials(),
                  board_.wifiSsid().c_str(), (int)board_.tzOffsetMinutes());
    board_.beginTimeSync();
    lastClockMinute_ = Clock::minuteKey();
    lastActivityMs_ = millis();
    Ui::setTheme(board_.themeMode() == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);
    openProfiles();
}

void KidsPlatformApp::loop() {
    Watchdog::feed();
    const TouchPoint rawTouch = board_.pollTouch();
    const uint32_t nowMs = millis();

    TouchPoint touch = rawTouch;
    if (swallowTouch_) {
        if (!rawTouch.down) swallowTouch_ = false;
        touch = TouchPoint{};
    }

    board_.tickTimeSync();
    board_.tickRgb();

    if (rawTouch.down || rawTouch.justPressed || rawTouch.justReleased) {
        lastActivityMs_ = nowMs;
    }

    const Board::IdleAction idlePolicy = board_.idleAction();
    if (view_ != View::ScreenSaver && view_ != View::Asleep) {
        const uint32_t idleMs = nowMs - lastActivityMs_;
        const uint32_t timeoutMs = static_cast<uint32_t>(board_.screenSaverSeconds()) * 1000UL;
        if (timeoutMs > 0 && idleMs > timeoutMs) {
            if (idlePolicy == Board::IdleAction::SleepOnly) {
                enterSleep();
            } else {
                enterScreenSaver();
            }
        }
    } else if (view_ == View::ScreenSaver &&
               idlePolicy == Board::IdleAction::SaverThenSleep) {
        const uint32_t saverMs = nowMs - screenSaverStartMs_;
        if (saverMs > static_cast<uint32_t>(board_.sleepSeconds()) * 1000UL) {
            enterSleep();
        }
    }

    const uint32_t minuteNow = Clock::minuteKey();
    if (minuteNow != lastClockMinute_) {
        lastClockMinute_ = minuteNow;
        if (view_ == View::Game && activeGame_ != nullptr) {
            activeGame_->requestRender();
        }
    }

    if (view_ == View::Asleep) {
        if (rawTouch.justPressed || rawTouch.down) {
            wakeFromSleep();
        }
    } else if (view_ == View::ScreenSaver) {
        if (touch.justPressed) {
            exitScreenSaver();
        } else if (nowMs - ssav_lastFrameMs_ >= 40) {
            ssav_lastFrameMs_ = nowMs;
            renderScreenSaver();
        }
    } else if (activeGame_ != nullptr) {
        const Rect settingsButton = LauncherLayout::topBarSettingsRect(display().width());
        if (activeGame_ != &launcher_ &&
            touch.justPressed && settingsButton.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            openSettings();
        } else if (activeGame_ != &launcher_ &&
                   touch.justPressed && HOME_BUTTON.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            goHome();
        } else {
            activeGame_->update(*this, touch);
        }
        if (activeGame_ != nullptr && activeGame_->needsRender()) {
            activeGame_->render(*this);
            activeGame_->clearDirty();
        }
    }

    const uint32_t workMs = millis() - nowMs;
    Watchdog::recordFrameWork(workMs);
    const uint32_t budgetMs = (view_ == View::Asleep) ? SLEEP_POLL_MS
                                                      : FRAME_BUDGET_MS;
    if (workMs < budgetMs) {
        delay(budgetMs - workMs);
    } else {
        delay(1);
    }
}

void KidsPlatformApp::applyRotation(uint8_t rotation) {
    if (board_.displayRotation() != rotation) {
        board_.setDisplayRotation(rotation);
    }
    swallowTouch_ = true;
}

uint8_t KidsPlatformApp::effectiveRotation(bool landscape) {
    return landscape ? CYD_SCREEN_ROTATION : CYD_PORTRAIT_ROTATION;
}

void KidsPlatformApp::leaveActiveGame() {
    if (activeGame_ == nullptr) {
        return;
    }
    const char* leaving = Watchdog::context();
    activeGame_->end(*this);
    activeGame_ = nullptr;
    activeApp_ = nullptr;
    Watchdog::noteScreenLeft(leaving, heapAtLaunch_);
}

void KidsPlatformApp::goHome() {
    leaveActiveGame();
    Watchdog::setContext("Launcher");
    activeGame_ = &launcher_;
    activeApp_ = nullptr;
    view_ = View::Game;
    applyRotation(effectiveRotation(board_.layoutMode() != Board::LayoutMode::Vertical));
    heapAtLaunch_ = ESP.getFreeHeap();
    activeGame_->begin(*this);
    activeGame_->render(*this);
    activeGame_->clearDirty();
}

void KidsPlatformApp::relaunchActiveGame() {
    if (activeGame_ != nullptr) {
        activeGame_->begin(*this);
    }
}

void KidsPlatformApp::openSettings() {
    launch(appById("settings"));
}

void KidsPlatformApp::openWifi() {
    launch(appById("wifi"));
}

void KidsPlatformApp::openProfiles() {
    launch(appById("profiles"));
}

const char* KidsPlatformApp::activeAppTitle() const {
    if (activeGame_ == &launcher_) {
        return "Launcher";
    }
    return activeApp_ != nullptr ? activeApp_->title() : "Game";
}

const AppDefinition& KidsPlatformApp::appById(const char* id) const {
    for (uint8_t i = 0; i < APP_REGISTRY_COUNT; ++i) {
        if (strcmp(APP_REGISTRY[i].id(), id) == 0) {
            return APP_REGISTRY[i];
        }
    }
    return APP_REGISTRY[APP_REGISTRY_COUNT - 1];
}
