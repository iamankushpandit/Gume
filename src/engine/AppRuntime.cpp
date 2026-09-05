#include "AppRuntime.h"

#include <esp_system.h>
#include <math.h>
#include "engine/NearbyPlay.h"
#include "AppVersion.h"
#include "BuildStamp.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"
#include "ui/LauncherLayout.h"

// Home, Lock and the gear all come from LauncherLayout, so a hit target and
// the glyph Ui paints into it cannot drift apart.


/* Hand every screen the same starting state for text.
 *
 * TftRenderer remembers the glyph size it was last given, which is what lets
 * the launcher ask for larger labels. The cost is that the size outlives the
 * screen that set it: a playable game drawing through ScaledRenderer sets a
 * size on every string, and a system app -- which draws through the same
 * renderer and never asks for a size at all -- would then inherit whatever the
 * last game left behind. That is not hypothetical; it is why text came out
 * enlarged in screens that do no scaling of their own.
 *
 * Resetting here rather than trusting each screen to tidy up after itself
 * makes the whole class of fault impossible: any screen wanting something
 * other than 1x sets it, and no screen can be affected by one that did. */
void BrainoApp::beginScreenPaint() {
    renderer_.setTextSize(1);
}

Ui::Renderer& BrainoApp::display() {
    if (activeAppIsPlayable()) {
        return scaledRenderer_;
    }
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

void BrainoApp::playSound(Sound cue) {
    board_.playSound(cue);
}

void BrainoApp::pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
    board_.pulseRgb(r, g, b, ms);
}

void BrainoApp::drawTopBar(const char* title) {
    Ui::drawTopBar(board_, title);
}

void BrainoApp::begin() {
    board_.begin();
    /* Games always render landscape, so the panel's landscape extent is the
     * physical size their fixed canvas has to fill. SCREEN_WIDTH/HEIGHT are
     * exactly that -- BoardConfig.h derives them from the profile's panel size
     * and landscape rotation -- so on a board that is already canvas-sized
     * this configures a scale of 1.0 and the wrapper becomes a passthrough. */
    scaledRenderer_.configure(SCREEN_WIDTH, SCREEN_HEIGHT);
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

    /* The console introduces itself. Synthesised from a phoneme table in
     * BoardAudio.cpp rather than played from a clip -- there is no audio file
     * anywhere in this firmware and there is not going to be one.
     *
     * It goes here, after openProfiles(), for two reasons. It must be the
     * last thing begin() does, because everything above it can block: touch
     * calibration is a wizard, and a phrase armed before it would be a second
     * and a half of speech that finished while the panel was still showing
     * crosshairs. And playSound() only arms a script -- the samples are
     * generated by tickAudio() from the loop -- so the profile picker paints
     * and takes touches all the way through it. */
    board_.playSound(Sound::Boot);
}

void BrainoApp::loop() {
    Watchdog::feed();
    const TouchPoint rawTouch = board_.pollTouch();
    const Board::ButtonEvent boot = board_.pollBootButton();
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
     * belongs to the screen underneath -- so the chrome has to repaint before
     * the strip is painted over it. Doing this here, ahead of the render
     * below, keeps both in the same frame.
     *
     * The strip is all that changes, so only the strip is repainted. This used
     * to call requestRender() and wipe the whole panel to put a banner in the
     * top 30 pixels. */
    if (NearbyPlay::bannerGeneration() != lastBannerGeneration_) {
        lastBannerGeneration_ = NearbyPlay::bannerGeneration();
        bannerNeedsPaint_ = true;
        requestChromeRender();
    }

    /* The BOOT key counts as activity exactly as a touch does. Leaving it out
     * would mean pressing Home and watching the saver arrive a moment later,
     * because as far as the idle timer was concerned nobody had touched the
     * device since before the press. */
    if (rawTouch.down || rawTouch.justPressed || rawTouch.justReleased ||
        boot.down || boot.justPressed || boot.justReleased) {
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
        requestChromeRender();
    }

    /* Keep the battery badge live rather than only correct at a screen change.
     * Charging state and percentage move independently, so both are watched.
     *
     * Header-only, like the clock above it. This was the worst of the three
     * full repaints: the percentage crosses a boundary far more often than the
     * cell actually discharges -- see Board::getBatteryPercent() -- so the
     * panel was being wiped every couple of seconds, on every screen, for a
     * badge 22 pixels wide. */
    const Board::ChargingState chargingNow = board_.getChargingState();
    const int8_t percentNow = board_.getBatteryPercent();
    if (chargingNow != lastChargingState_ || percentNow != lastBatteryPercent_) {
        lastChargingState_ = chargingNow;
        lastBatteryPercent_ = percentNow;
        requestChromeRender();
    }

    if (view_ == View::Asleep) {
        if (rawTouch.justPressed || rawTouch.down || boot.justPressed) {
            wakeFromSleep();
        }
    } else if (view_ == View::Locked) {
        /* The swallowed touch, not the raw one: the press that lit the panel
         * has to be released before a hold can start.
         *
         * The BOOT key is deliberately not offered here. This screen is the
         * accidental-touch guard, and a button pressed through the side of a
         * bag is exactly the accident it exists to catch -- a key that
         * dismissed it would be a hole in the one thing it does. Unlocking
         * stays the deliberate hold on the panel. */
        updateLock(touch, nowMs);
    } else if (view_ == View::ScreenSaver) {
        if (touch.justPressed || boot.justPressed) {
            exitScreenSaver();
        } else if (nowMs - ssav_lastFrameMs_ >= 40) {
            ssav_lastFrameMs_ = nowMs;
            beginScreenPaint();
            renderScreenSaver();
        }
    } else if (activeGame_ != nullptr) {
        /* The top bar -- home, gear, lock, clock, badges -- is drawn through
         * the raw renderer, because it is shared chrome sized against the real
         * panel rather than part of any game's canvas. Its hit rects and the
         * touch coordinates tested against them must therefore stay in that
         * same physical space, not display()'s, which is scaled while a
         * playable game is active. */
        const Rect settingsButton = LauncherLayout::topBarSettingsRect(renderer_.width());
        const Rect homeButton = LauncherLayout::topBarHomeRect();
        const Rect lockButton = activeLockRect();
        /* BOOT is Home. It is consumed here, above the active screen's
         * update(), for the same reason the Home glyph is: a screen that also
         * saw the press would act on it, and the player would find it done on
         * their way back.
         *
         * On the launcher it is not consumed at all -- you are already home,
         * and taking the frame would drop a simultaneous touch for no gain.
         *
         * This fires on the press rather than the release because a Home key
         * that waits to see how long you held it feels broken. The cost is
         * that a future hold gesture cannot be layered on top without moving
         * this to the release edge first; hal/BoardButton.cpp says the same
         * thing from the other end. */
        const bool bootHome = boot.justPressed && activeGame_ != &launcher_;
        if (bootHome) {
            goHome();
        } else if (activeGame_ != &launcher_ &&
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
        } else if (activeAppIsPlayable()) {
            /* Below the chrome, a playable game hit-tests against its own
             * fixed canvas, so the physical press has to be mapped back into
             * that space or every target lands where the content used to be
             * rather than where it is drawn. The inverse of ScaledRenderer's
             * transform, and a no-op when the panel is canvas-sized. */
            TouchPoint gameTouch = touch;
            gameTouch.x = static_cast<int16_t>(lroundf(
                touch.x * static_cast<float>(GAME_CANVAS_WIDTH) / SCREEN_WIDTH));
            gameTouch.y = static_cast<int16_t>(lroundf(
                touch.y * static_cast<float>(GAME_CANVAS_HEIGHT) / SCREEN_HEIGHT));
            activeGame_->update(*this, gameTouch);
        } else {
            activeGame_->update(*this, touch);
        }
        bool repainted = false;
        if (activeGame_ != nullptr && activeGame_->needsRender()) {
            beginScreenPaint();
            activeGame_->render(*this);
            activeGame_->clearDirty();
            repainted = true;
            /* A full render redraws the header on its way past, so a chrome
             * request raised earlier in this same frame is already served. */
            chromeNeedsPaint_ = false;
        } else if (chromeNeedsPaint_ && activeGame_ != nullptr) {
            beginScreenPaint();
            if (activeGame_->renderChrome(*this)) {
                chromeNeedsPaint_ = false;
                /* The banner paints over the same strip, so a chrome repaint
                 * erases it exactly as a full one does. Both have to tell
                 * drawHeaderBanner() to put it back. */
                repainted = true;
            } else {
                /* The screen cannot do it in isolation. Fall back rather than
                 * leave a stale badge: the full repaint lands next frame and
                 * clears the flag on its way through the branch above. */
                activeGame_->requestRender();
            }
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

void BrainoApp::requestChromeRender() {
    /* Only a live screen has chrome to repaint. The saver, sleep and the lock
     * screen each own the whole panel and paint their own status on their own
     * cadence -- the lock screen tracks its battery badge in lockPaintedPct_ --
     * so a badge change must not reach in and draw a top bar over them. */
    if (view_ != View::Game || activeGame_ == nullptr) {
        return;
    }
    chromeNeedsPaint_ = true;
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
    /* A SCREEN TRANSITION IS ALWAYS A WHOLE-SCREEN REPAINT.
     *
     * Every screen is a static instance reused for the life of the device, so
     * it arrives here carrying the dirty flags the LAST visit left behind --
     * and the last thing that visit did was clearDirty(). Without this, a
     * screen whose begin() happens to call only markDirty() renders without
     * its renderStatic(), which is where both Ui::clear() and the top bar
     * live: no chrome, and the previous screen still showing underneath it.
     *
     * It belongs here rather than in each begin(). Fourteen screens would each
     * have to remember, and the one that forgot would be found by eye on a
     * panel rather than by any build. Partial repaint is an optimisation
     * WITHIN a screen's lifetime; entering one is not the place to be greedy. */
    activeGame_->requestRender();
    beginScreenPaint();
    activeGame_->render(*this);
    activeGame_->clearDirty();
}

void BrainoApp::relaunchActiveGame() {
    if (activeGame_ != nullptr) {
        activeGame_->begin(*this);
        /* Same reasoning as launch() and goHome(): starting a screen over is a
         * whole-screen repaint, and this one does not even render here -- it
         * leaves that to the next loop iteration, which would otherwise use
         * whatever flags the previous frame left. */
        activeGame_->requestRender();
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
