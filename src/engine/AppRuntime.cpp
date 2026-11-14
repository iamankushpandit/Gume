// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "AppRuntime.h"

#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <math.h>
#include "engine/NearbyPlay.h"
#include "hal/BleBeacon.h"
#include "hal/Clock.h"
#include "hal/PowerMonitor.h"
#include "hal/Watchdog.h"
#include "ui/LauncherLayout.h"
#include "ui/Stops.h"

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

/* Which renderer a screen draws through, and it is the same question as which
 * orientation it runs in.
 *
 * A playable game that does NOT follow layout is the original contract: it is
 * authored against a fixed 320x240 landscape canvas, forced to landscape by
 * rotationForActiveScreen(), and scaled up on a bigger panel. All 31 games in
 * the catalog work that way and are unaffected by this.
 *
 * A playable game that DOES follow layout has opted out of both halves at
 * once, and it has to be both or neither. ScaledRenderer::width()/height()
 * report the fixed canvas whatever the panel is doing, so a game that honoured
 * the user's portrait setting while still measuring 320 wide would lay out a
 * third of itself off the right-hand edge of a 240px panel -- and the serial
 * log would look perfectly healthy, which is the same shape as the Settings
 * and Wi-Fi portrait bug CLAUDE.md complains about.
 *
 * So: follows layout => raw renderer, real dimensions, read tft.width() and
 * tft.height() at render time and lay out against them. Never SCREEN_WIDTH,
 * SCREEN_HEIGHT or GAME_CANVAS_*. The cost is that such a game gives up free
 * upscaling on the 4-inch and has to be responsive itself, which is the point
 * rather than a regression. */
Ui::Renderer& BrainoApp::display() {
    if (activeAppIsPlayable() && !activeApp_->followsLayout) {
        return scaledRenderer_;
    }
    return renderer_;
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
    PowerMonitor::begin(board_);
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
    logBootBanner();
    board_.beginTimeSync();
    /* After Board::begin(), which is what brings the beacon up: Nearby play
     * rides on that radio and must not try to arm itself before it exists. */
    NearbyPlay::begin(board_);
    lastBannerGeneration_ = NearbyPlay::bannerGeneration();
    lastClockMinute_ = Clock::minuteKey();
    lastActivityMs_ = millis();
    lastBatteryPercent_ = board_.getBatteryPercent();
    /* The two enums are kept numerically identical (see the static_asserts in
     * Ui.cpp), so this is a cast rather than a mapping. It used to be
     * `== Light ? Light : Dark`, written out in three places -- a form that
     * silently collapses every theme that is not Light into Dark the moment a
     * third one exists. */
    Ui::setTheme(static_cast<Ui::Theme>(board_.themeMode()));

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
    const FrameKeys keys = pollKeys();
    /* For the idle, saver and sleep paths below a key is a key: any of them
     * does exactly what BOOT does there. Which key it was matters only to the
     * active screen. */
    const bool keyPress = boot.justPressed || keys.any();
    const uint32_t nowMs = millis();

    TouchPoint touch = rawTouch;
    if (swallowTouch_) {
        if (!rawTouch.down) swallowTouch_ = false;
        touch = TouchPoint{};
    }

    board_.tickTimeSync();
    board_.tickUpdateCheck();
    board_.tickRgb();
    /* Releases the BLE controller once Wi-Fi is down, when switching the
     * beacon off had to leave it up to avoid a coexistence-teardown panic. */
    BleBeacon::tickRadio();
    board_.tickAudio();
    NearbyPlay::tick(board_);
    tickBatteryWarning(nowMs);
    tickUpdateNotice(nowMs);
    tickBenchNotice(nowMs);
    tickLightTest(nowMs);
    tickPowerMonitor(nowMs);
    tickSerialQuery();

    /* A Nearby notice appearing, changing its words or expiring. The strip is
     * all that changes and settleNotice(), below, is all that answers it: the
     * notice lies along the bottom of the panel, over none of the header, so
     * nothing of the screen's has to repaint for one to arrive. This used to
     * ask for the chrome as well, when the strip was painted over the top
     * bar, and before that for the whole panel. */
    if (NearbyPlay::bannerGeneration() != lastBannerGeneration_) {
        lastBannerGeneration_ = NearbyPlay::bannerGeneration();
        bannerNeedsPaint_ = true;
    }

    /* A console being looked for lights its panel, and any press stops it.
     *
     * The wake goes through the ordinary path, so an owner who keeps the
     * hold-to-unlock guard on still gets it -- and that is the right screen to
     * arrive at: the lock screen carries the wordmark and the battery badge,
     * which is exactly what somebody who has just found a device wants, and it
     * returns itself to sleep afterwards.
     *
     * The press is NOT consumed. It stops the noise and then does whatever it
     * was going to do; the lock screen already swallows the press that woke
     * it. Dismissing here rather than inside any one view means every way of
     * ending an alert -- a touch, the BOOT key, or its own timeout in
     * NearbyPlay::tick() -- runs the same restore of the mute switch. */
    if (NearbyPlay::alertActive() &&
        (rawTouch.justPressed || rawTouch.down || keyPress)) {
        NearbyPlay::dismissAlert(board_);
    }
    if (NearbyPlay::alertActive() && view_ == View::Asleep) {
        wakeFromSleep();
    }

    /* The BOOT key counts as activity exactly as a touch does. Leaving it out
     * would mean pressing Home and watching the saver arrive a moment later,
     * because as far as the idle timer was concerned nobody had touched the
     * device since before the press. */
    if (rawTouch.down || rawTouch.justPressed || rawTouch.justReleased ||
        boot.down || boot.justPressed || boot.justReleased || keyPress) {
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
        /* The digits alone, where the screen can: a few characters drawn over
         * the old ones, not the header wiped and redrawn once a minute. Not
         * outside a live screen -- requestChromeRender() is what knows that,
         * and it is the fallback. A notice used to own those pixels while it
         * was up; it is along the bottom now and the clock does not wait for
         * it. */
        const bool inPlace = view_ == View::Game && activeGame_ != nullptr &&
                             !chromeNeedsPaint_ &&
                             (beginScreenPaint(), activeGame_->renderClock(*this));
        if (!inPlace) {
            requestChromeRender();
        }
    }

    /* Keep the battery badge live rather than only correct at a screen change.
     *
     * Header-only, like the clock above it. This was the worst of the three
     * full repaints: the percentage crosses a boundary far more often than the
     * cell actually discharges -- see Board::getBatteryPercent() -- so the
     * panel was being wiped every couple of seconds, on every screen, for a
     * badge 22 pixels wide. */
    const int8_t percentNow = board_.getBatteryPercent();
    if (percentNow != lastBatteryPercent_) {
        lastBatteryPercent_ = percentNow;
        requestChromeRender();
    }

    if (view_ == View::Asleep) {
        if (rawTouch.justPressed || rawTouch.down || keyPress) {
            wakeFromSleep();
        } else {
            tickDeepSleep(nowMs);
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
        if (touch.justPressed || keyPress) {
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
        const Rect speakerButton = activeSpeakerRect();
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
        /* On a board with keys, the d-pad's selector runs here, above the
         * routing, and does everything through it: a press is a touch at the
         * selected control (it replaces the frame's `touch`), and Home, Lock
         * and Mute come back as bits for the same branches BOOT, the power
         * key and the speaker's tap already take (AppRuntimeSelector.cpp).
         *
         * A power key's short press is the Lock button, consumed up here for
         * the reason Lock's tap is, and on the launcher too, which has a Lock
         * badge of its own. Like BOOT it is a shortcut and nothing more: it
         * wakes a sleeping panel and dismisses the saver exactly as any key
         * does (above), and it does nothing on the lock screen, which takes no
         * key at all. */
#if GUME_HAS_NAV_KEYS
        const uint8_t selectorJobs = selectorFrame(touch, keys);
#else
        const uint8_t selectorJobs = 0;
#endif
        const bool powerLock = keys.power() || (selectorJobs & SELECTOR_LOCK) != 0;
        const bool homeKey = !powerLock &&
                             (boot.justPressed || (selectorJobs & SELECTOR_HOME) != 0);
        /* With a notice up, Home's key puts the notice away and does not go
         * Home. A console with no touch panel has nothing to press the
         * notice with, and what it covers -- the bottom row of answers, the
         * launcher's Prev and Next -- is out of reach until it goes. The key
         * that means "back" is the one a hand goes to, and one more press is
         * Home as ever. On the launcher too, where the key otherwise does
         * nothing at all. */
        const bool noticeAway = homeKey && noticeOnPanel_;
        const bool bootHome = homeKey && !noticeAway && activeGame_ != &launcher_;
        const bool keyMute = !powerLock && !homeKey && (selectorJobs & SELECTOR_MUTE) != 0;
        offerKeys(keys, bootHome || powerLock || keyMute || noticeAway);
        if (powerLock) {
            lockAndSleepNow();
        } else if (noticeAway) {
            dismissNotice(nowMs);
        } else if (bootHome) {
            goHome();
        } else if (keyMute) {
            toggleMute();
        } else if (touch.justPressed && noticeOnPanel_ &&
                   Ui::notificationRect(renderer_.width(), renderer_.height())
                       .contains(touch.x, touch.y)) {
            /* A press on a notice puts it away, and does nothing else. It is
             * taken up here, above the screen, for the reason Home and Lock
             * are: the notice lies over the bottom of the screen, and a press
             * that also reached the screen would press whatever is under it,
             * which the player cannot see. The rest of the press is swallowed
             * as well, so the button under the strip is not handed the
             * release of a press it never saw begin -- and that goes for a
             * press the d-pad's selector was making, which is let go here.
             *
             * No slop. A strip that took presses from just above itself would
             * be taking them from the controls it lies beside. */
            dismissNotice(nowMs);
            swallowTouch_ = true;
            selectorLetGo();
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
        /* An empty rect is a screen showing no padlock (Game::headerControls),
         * and with the slop it would still catch the corner. */
        } else if (touch.justPressed && lockButton.w > 0 &&
                   lockButton.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            lockAndSleepNow();
        /* Mute is consumed up here for the same reason Home, the gear and
         * Lock are: a tap that also reached the screen would press whatever
         * sat under the speaker, and the player would find it done. */
        } else if (touch.justPressed && Board::hasSound() && speakerButton.w > 0 &&
                   speakerButton.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            toggleMute();
        } else if (activeAppIsPlayable() && !activeApp_->followsLayout) {
            /* Below the chrome, a fixed-canvas game hit-tests against its own
             * 320x240 space, so the physical press has to be mapped back into
             * that space or every target lands where the content used to be
             * rather than where it is drawn. The inverse of ScaledRenderer's
             * transform, and a no-op when the panel is canvas-sized.
             *
             * A game that follows layout draws through the raw renderer at the
             * panel's real size, so it must NOT be mapped -- the transform
             * that keeps a fixed-canvas game honest is exactly what would
             * break an adaptive one. The two have to be decided by the same
             * flag as display(), or a game gets real pixels to draw on and
             * canvas coordinates to hit-test with, which is the bug this
             * comment describes, arrived at from the opposite direction. */
            TouchPoint gameTouch = touch;
            gameTouch.x = static_cast<int16_t>(lroundf(
                touch.x * static_cast<float>(GAME_CANVAS_WIDTH) / SCREEN_WIDTH));
            gameTouch.y = static_cast<int16_t>(lroundf(
                touch.y * static_cast<float>(GAME_CANVAS_HEIGHT) / SCREEN_HEIGHT));
            if (gameTouch.justPressed) {
                pressedInGame_ = true;      // there is now something to keep
            }
            activeGame_->update(*this, gameTouch);
        } else {
            if (touch.justPressed) {
                pressedInGame_ = true;
            }
            activeGame_->update(*this, touch);
        }
        withdrawKeys();
        /* The runtime asks Carry on / New game on its own screen, drawn as
         * the game it stands in front of; this is where it reads the answer.
         * A no-op on every other screen, and after a press the chrome took. */
        settleCarryOn();
        /* Whether the screen, or its chrome, painted this frame: the
         * selector's ring was painted over if so, and goes back on after. */
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
                repainted = true;
            } else {
                /* The screen cannot do it in isolation. Fall back rather than
                 * leave a stale badge: the full repaint lands next frame and
                 * clears the flag on its way through the branch above. */
                activeGame_->requestRender();
            }
        }
        /* After the screen, never before: the notice is painted over what the
         * screen has just drawn, and from then on the screen cannot draw
         * there. Neither branch above has to tell it anything -- a repaint of
         * the screen or of its chrome no longer lands on the notice. It says
         * when the strip changed hands, which the selector has to hear: the
         * control it was on may be under a notice now, or have come back. */
        if (settleNotice()) {
            repainted = true;
        }
#if GUME_HAS_NAV_KEYS
        paintSelector(repainted);
#endif
    }

#if GUME_HAS_NAV_KEYS
    /* In every view, so a key that woke the panel or dismissed the saver is
     * not taken as a fresh press on the frame after. */
    sel_.prev = keys.held;
#endif
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

/* What the battery diagnostic cannot find out for itself. Once a second, from
 * RAM: the task that books the time reads the last one published, so nothing
 * here is on its schedule and nothing there touches the runtime. */
void BrainoApp::tickPowerMonitor(uint32_t nowMs) {
    if (nowMs - lastPowerPublishMs_ < 1000) return;
    lastPowerPublishMs_ = nowMs;
    PowerMonitor::Activity a;
    a.activity = view_ == View::Asleep        ? PowerLedger::Activity::Asleep
                 : view_ == View::ScreenSaver ? PowerLedger::Activity::Saver
                 : view_ == View::Locked      ? PowerLedger::Activity::Locked
                                              : PowerLedger::Activity::Awake;
    a.lampPercent = board_.lampPercent();
    a.wifi = WiFi.status() == WL_CONNECTED;
    a.bluetooth = BleBeacon::active() || NearbyPlay::active();
    PowerMonitor::publish(a);
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
        /* The bottom of the panel is about to be somewhere else, and the
         * driver drops its viewport when it turns. A notice that is up is
         * painted again where the bottom now is, after the screen. */
        noticeOffPanel();
        board_.setDisplayRotation(rotation);
    }
    swallowTouch_ = true;
}

uint8_t BrainoApp::effectiveRotation(bool landscape) {
    /* Which quarter-turn is "landscape" depends on where the board puts its
     * USB socket, so both come from the board profile. */
    const uint8_t base = landscape ? BOARD.panel.landscapeRotation
                                   : BOARD.panel.portraitRotation;
    /* Which way up the board sits depends on the case it was put in, so that
     * half comes from the owner. Half a turn keeps landscape landscape and
     * portrait portrait, which is why it can be added here, underneath every
     * caller, without any of them learning a third answer. */
    return board_.screenFlipped() ? static_cast<uint8_t>((base + 2) & 3) : base;
}

/* The orientation settings changed while a screen is up. Settings and the
 * serial console both land here, so a flip is seen the moment it is chosen
 * rather than on the way out of the screen it was chosen on.
 *
 * Nothing happens when the screen that is up does not turn: Settings is
 * always landscape, so choosing Portrait from it changes the launcher it
 * returns to and nothing in front of the owner. Repainting -- and swallowing
 * a touch -- for that would be a flash that answers no question. The saver,
 * sleep and the lock screen are left alone too; resumeUnderlyingScreen()
 * applies the rotation when they hand back. */
void BrainoApp::applyOrientation() {
    if (view_ != View::Game || activeGame_ == nullptr) {
        return;
    }
    const uint8_t rotation = rotationForActiveScreen();
    if (board_.displayRotation() == rotation) {
        return;
    }
    applyRotation(rotation);
    activeGame_->requestRender();
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
    noteGameLeft();     // before end(): the question is about the game as it stood
    activeGame_->end(*this);
    activeGame_ = nullptr;
    activeApp_ = nullptr;
    /* What the screen said about its board goes with the screen. */
    Ui::clearBoardStops();
    Ui::clearOverlayStops();
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
        pressedInGame_ = false;     // begun again: nothing in it yet
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
