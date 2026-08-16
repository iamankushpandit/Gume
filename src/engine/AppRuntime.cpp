#include "AppRuntime.h"

#include <esp_system.h>
#include "engine/NearbyPlay.h"
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
    const bool improved = board_.saveBestScore(key, value, lowerIsBetter);
    if (improved) {
        /* Peers should see the number that is true now, not the one that was
         * current when the screen opened. */
        NearbyPlay::refreshScore(board_);
    }
    return improved;
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
    /* Deliberately not seeded. randomSeed() reads like it improves things and
     * does the reverse on this core: arduino-esp32 defaults random() to the
     * hardware RNG, and randomSeed() switches it to newlib rand() for the rest
     * of the boot. Seeding it from esp_random() -- which is what used to happen
     * here -- bought one good number and then spent the next thirty games on a
     * software PRNG. Leaving it alone keeps every draw hardware-backed. See
     * engine/Entropy.h. */
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
    /* After Board::begin(), which is what brings the beacon up: Nearby play
     * rides on that radio and must not try to arm itself before it exists. */
    NearbyPlay::begin(board_);
    lastBannerGeneration_ = NearbyPlay::bannerGeneration();
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
    NearbyPlay::tick(board_);

    /* A notification appearing or expiring changes the header, and the header
     * belongs to the screen underneath -- so the screen has to repaint before
     * the strip is painted over it. Doing this here, ahead of the render
     * below, keeps both in the same frame; events are a handful a minute, so
     * the full repaint they cost is not worth optimising away. */
    if (NearbyPlay::bannerGeneration() != lastBannerGeneration_) {
        lastBannerGeneration_ = NearbyPlay::bannerGeneration();
        bannerNeedsPaint_ = true;
        if (view_ == View::Game && activeGame_ != nullptr) {
            activeGame_->requestRender();
        }
    }

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
        bool repainted = false;
        if (activeGame_ != nullptr && activeGame_->needsRender()) {
            activeGame_->render(*this);
            activeGame_->clearDirty();
            repainted = true;
        }
        drawNearbyBanner(repainted);
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

/* Painted when it first appears and again whenever the screen underneath has
 * just redrawn over it -- not on every frame. Repainting a 320x30 strip at
 * 50Hz for five seconds would spend milliseconds a frame redrawing text that
 * has not changed, and the frame budget is 20ms for everything. */
void KidsPlatformApp::drawNearbyBanner(bool screenRepainted) {
    const char* text = NearbyPlay::banner();
    if (text == nullptr) {
        bannerNeedsPaint_ = false;
        return;
    }
    if (!bannerNeedsPaint_ && !screenRepainted) {
        return;
    }
    bannerNeedsPaint_ = false;
    Ui::drawNotification(renderer_, text);
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

/* One place that decides orientation, because it used to be three and one of
 * them was wrong.
 *
 * Playable games are authored against a fixed 320x240 landscape canvas and are
 * always landscape. The launcher (activeApp_ == nullptr) and the system apps
 * that opted into followsLayout honour the user's layout setting.
 *
 * launch() had this right. wakeFromSleep() and exitScreenSaver() both tested
 * `activeApp_ != nullptr` instead -- true for *every* launched app, including
 * Profiles and System Info, which both follow layout -- so returning from the
 * screen saver or from sleep forced those two screens to landscape and left
 * them there. Boot into Profiles in Vertical layout was correct; idle until the
 * saver came on, then touch, and it came back landscape. */
uint8_t KidsPlatformApp::rotationForActiveScreen() {
    const bool followsLayout = (activeApp_ == nullptr) || activeApp_->followsLayout;
    return effectiveRotation(!followsLayout ||
                             board_.layoutMode() != Board::LayoutMode::Vertical);
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
    NearbyPlay::setActiveApp(board_, nullptr);
    Watchdog::setContext("Launcher");
    activeGame_ = &launcher_;
    activeApp_ = nullptr;
    view_ = View::Game;
    applyRotation(rotationForActiveScreen());
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
