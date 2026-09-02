#include "AppRuntime.h"

#include <esp_system.h>
#include "engine/NearbyPlay.h"
#include "AppVersion.h"
#include "BuildStamp.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"
#include "ui/LauncherLayout.h"

// Home, Lock and the gear all come from LauncherLayout, so a hit target and
// the glyph Ui paints into it cannot drift apart.


Ui::Renderer& BrainoApp::display() {
    return renderer_;
}

Board& BrainoApp::board() {
    return board_;
}

bool BrainoApp::hasCapability(uint32_t capability) const {
    return activeApp_ != nullptr && activeApp_->hasCapability(capability);
}

bool BrainoApp::requireCapability(uint32_t capability, const char* action) {
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

ContentLoader& BrainoApp::content() {
    return content_;
}

uint32_t BrainoApp::getScore(const char* key, uint32_t fallback) {
    return board_.getScore(key, fallback);
}

void BrainoApp::setScore(const char* key, uint32_t value) {
    board_.setScore(key, value);
}

bool BrainoApp::saveBestScore(const char* key, uint32_t value, bool lowerIsBetter) {
    const bool improved = board_.saveBestScore(key, value, lowerIsBetter);
    if (improved) {
        /* Peers should see the number that is true now, not the one that was
         * current when the screen opened. */
        NearbyPlay::refreshScore(board_);
    }
    return improved;
}

void BrainoApp::loadBlob(const char* key, void* dst, size_t len) {
    board_.loadBlob(key, dst, len);
}

void BrainoApp::saveBlob(const char* key, const void* src, size_t len) {
    board_.saveBlob(key, src, len);
}

void BrainoApp::beepOk() {
    board_.beepOk();
}

void BrainoApp::beepError() {
    board_.beepError();
}

void BrainoApp::pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
    board_.pulseRgb(r, g, b, ms);
}

void BrainoApp::drawTopBar(const char* title) {
    Ui::drawTopBar(board_, title);
}

void BrainoApp::begin() {
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
    Serial.printf("[boot] board=%s rot=%d layout=%s (landscape=%d portrait=%d)\n",
                  BOARD.name,
                  (int)board_.displayRotation(),
                  board_.layoutMode() == Board::LayoutMode::Vertical ? "Vertical" : "Horizontal",
                  (int)BOARD.panel.landscapeRotation,
                  (int)BOARD.panel.portraitRotation);
    /* Beside the board line, so a serial log pasted into an issue identifies
     * the firmware that produced it. "Which build was that?" has cost more
     * than one round trip on a bug report. */
    Serial.printf("[boot] build=%s built=%s version=%s\n",
                  BuildStamp::describe(), BuildStamp::builtAt(), BRAINO_VERSION);
    /* The saved SSID is deliberately absent from this line, and from every
     * other Serial write in the tree. A serial log is the one artifact of
     * this device that routinely leaves the house -- pasted into a bug
     * report, captured by whoever plugs a cable in -- and the network's name
     * belongs to the household, not to the firmware. `creds` answers the only
     * question a log needs answered: whether a network is configured at all.
     * The name itself is still on the device, in System Info and the network
     * activity list, where the person reading it is the person holding it. */
    Serial.printf("[boot] ntp=%d creds=%d tzmin=%d\n",
                  (int)board_.ntpEnabled(), (int)board_.hasWifiCredentials(),
                  (int)board_.tzOffsetMinutes());
    board_.beginTimeSync();
    /* After Board::begin(), which is what brings the beacon up: Nearby play
     * rides on that radio and must not try to arm itself before it exists. */
    NearbyPlay::begin(board_);
    lastBannerGeneration_ = NearbyPlay::bannerGeneration();
    lastClockMinute_ = Clock::minuteKey();
    lastActivityMs_ = millis();
    lastChargingState_ = board_.getChargingState();
    lastBatteryPercent_ = board_.getBatteryPercent();
    Ui::setTheme(board_.themeMode() == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);

    /* First-boot: automatically create default Admin profile with PIN 0000. */
    if (board_.playerCount() == 0 && board_.adminProfileIndex() == Board::GUEST_INDEX) {
        uint8_t adminIdx = board_.addPlayer("Admin");
        board_.setAdminProfileIndex(adminIdx);
        board_.setAdminPin(0);
        Serial.println("[boot] Created default Admin profile with PIN 0000");
    }

    /* Never boot straight into the admin profile. The picker's Done button
     * goes home with whatever profile is already active, so leaving admin
     * selected across a reboot would hand out admin without a PIN -- the one
     * path the PIN is there to close. Guest writes nothing, which is the right
     * thing to be holding until somebody chooses. */
    if (board_.isAdminProfile(board_.activeProfile())) {
        board_.setActiveProfile(Board::GUEST_INDEX);
    }
    openProfiles();
}

void BrainoApp::loop() {
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
    board_.tickAudio();
    NearbyPlay::tick(board_);
    tickBatteryWarning(nowMs);

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
    /* View::Locked is excluded along with the two idle views. It runs its own
     * timeout in updateLock(); leaving it in here would re-arm the saver timer
     * against a lastActivityMs_ that is already older than the timeout, and
     * the two would fight over the same frame. */
    if (view_ != View::ScreenSaver && view_ != View::Asleep &&
        view_ != View::Locked) {
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

    /* Invalidate screens when battery state changes, so the badge updates
     * in real-time instead of only on screen switches. Check both charging
     * state and percentage since either can change independently. */
    const Board::ChargingState chargingNow = board_.getChargingState();
    const int8_t percentNow = board_.getBatteryPercent();
    if (chargingNow != lastChargingState_ || percentNow != lastBatteryPercent_) {
        lastChargingState_ = chargingNow;
        lastBatteryPercent_ = percentNow;
        if (view_ == View::Game && activeGame_ != nullptr) {
            activeGame_->requestRender();
        }
    }

    if (view_ == View::Asleep) {
        if (rawTouch.justPressed || rawTouch.down) {
            wakeFromSleep();
        }
    } else if (view_ == View::Locked) {
        /* The swallowed touch, not the raw one: the press that lit the panel
         * has to be released before a hold can start. */
        updateLock(touch, nowMs);
    } else if (view_ == View::ScreenSaver) {
        if (touch.justPressed) {
            exitScreenSaver();
        } else if (nowMs - ssav_lastFrameMs_ >= 40) {
            ssav_lastFrameMs_ = nowMs;
            renderScreenSaver();
        }
    } else if (activeGame_ != nullptr) {
        const Rect settingsButton = LauncherLayout::topBarSettingsRect(display().width());
        const Rect homeButton = LauncherLayout::topBarHomeRect();
        const Rect lockButton = activeLockRect();
        if (activeGame_ != &launcher_ &&
            touch.justPressed && settingsButton.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            openSettings();
        } else if (activeGame_ != &launcher_ &&
                   touch.justPressed && homeButton.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            goHome();
        /* Above the screen's own update(), so the tap that locks is consumed
         * here and cannot also press whatever it landed on -- the user would
         * find it done when they unlocked. Home is tested first: their touch
         * slop overlaps even though the glyphs do not, and the older target
         * keeps the ambiguous pixels. */
        } else if (touch.justPressed &&
                   lockButton.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            lockAndSleepNow();
        } else {
            activeGame_->update(*this, touch);
        }
        bool repainted = false;
        if (activeGame_ != nullptr && activeGame_->needsRender()) {
            activeGame_->render(*this);
            activeGame_->clearDirty();
            repainted = true;
        }
        drawHeaderBanner(repainted);
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
/* The charger is the only way out of this state, so the warning is driven off
 * Board's charge verdict rather than the percentage alone: plugging in clears
 * it within a couple of seconds, long before the reading climbs. */
void BrainoApp::tickBatteryWarning(uint32_t nowMs) {
    if (batteryCheckMs_ != 0 && nowMs - batteryCheckMs_ < BATTERY_CHECK_MS) {
        return;
    }
    batteryCheckMs_ = nowMs;

    const uint8_t level = board_.isBatteryCritical() ? 2
                        : board_.isBatteryLow()      ? 1
                                                     : 0;

    if (level != batteryWarnLevel_) {
        batteryWarnLevel_ = level;
        /* Crossing a threshold -- in either direction -- restarts the cycle,
         * so going from low to critical says so at once instead of waiting out
         * the repeat interval left over from the milder warning. */
        batteryEverHidden_ = false;
        if (batteryBanner_ != nullptr) {
            batteryBanner_ = nullptr;
            requestBannerRepaint();
        }
    }

    if (level == 0) {
        if (batteryBanner_ != nullptr) {
            batteryBanner_ = nullptr;
            requestBannerRepaint();
        }
        return;
    }

    if (batteryBanner_ != nullptr) {
        if (nowMs - batteryShownMs_ >= BATTERY_BANNER_MS) {
            batteryBanner_ = nullptr;
            batteryHiddenMs_ = nowMs;
            batteryEverHidden_ = true;
            requestBannerRepaint();
        }
        return;
    }

    if (batteryEverHidden_ && nowMs - batteryHiddenMs_ < BATTERY_REPEAT_MS) {
        return;
    }
    batteryBanner_ = (level == 2) ? "Battery empty - plug in the charger"
                                  : "Battery low - time to charge";
    batteryShownMs_ = nowMs;
    requestBannerRepaint();
}

/* The strip is painted over the screen's own header, so the screen underneath
 * has to redraw before it can genuinely go away again. */
void BrainoApp::requestBannerRepaint() {
    bannerNeedsPaint_ = true;
    if (view_ == View::Game && activeGame_ != nullptr) {
        activeGame_->requestRender();
    }
}

void BrainoApp::drawHeaderBanner(bool screenRepainted) {
    const char* text = (batteryBanner_ != nullptr) ? batteryBanner_
                                                   : NearbyPlay::banner();
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

void BrainoApp::applyRotation(uint8_t rotation) {
    if (board_.displayRotation() != rotation) {
        board_.setDisplayRotation(rotation);
    }
    swallowTouch_ = true;
}

uint8_t BrainoApp::effectiveRotation(bool landscape) {
    /* Which quarter-turn is "landscape" depends on where the board puts its
     * USB socket, so both come from the board profile. */
    return landscape ? BOARD.panel.landscapeRotation : BOARD.panel.portraitRotation;
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
uint8_t BrainoApp::rotationForActiveScreen() {
    const bool followsLayout = (activeApp_ == nullptr) || activeApp_->followsLayout;
    return effectiveRotation(!followsLayout ||
                             board_.layoutMode() != Board::LayoutMode::Vertical);
}

void BrainoApp::leaveActiveGame() {
    if (activeGame_ == nullptr) {
        return;
    }
    const char* leaving = Watchdog::context();
    activeGame_->end(*this);
    activeGame_ = nullptr;
    activeApp_ = nullptr;
    Watchdog::noteScreenLeft(leaving, heapAtLaunch_);
}

void BrainoApp::goHome() {
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

void BrainoApp::relaunchActiveGame() {
    if (activeGame_ != nullptr) {
        activeGame_->begin(*this);
    }
}

void BrainoApp::openSettings() {
    launch(appById("settings"));
}

void BrainoApp::openWifi() {
    launch(appById("wifi"));
}

void BrainoApp::openProfiles() {
    launch(appById("profiles"));
}

const char* BrainoApp::activeAppTitle() const {
    if (activeGame_ == &launcher_) {
        return "Launcher";
    }
    return activeApp_ != nullptr ? activeApp_->title() : "Game";
}

const AppDefinition& BrainoApp::appById(const char* id) const {
    for (uint8_t i = 0; i < APP_REGISTRY_COUNT; ++i) {
        if (strcmp(APP_REGISTRY[i].id(), id) == 0) {
            return APP_REGISTRY[i];
        }
    }
    return APP_REGISTRY[APP_REGISTRY_COUNT - 1];
}
