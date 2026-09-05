#include "SettingsGame.h"

#include "engine/NearbyPlay.h"
#include "hal/Board.h"

/* The screen itself: lifecycle, the tab strip, and the one touch handler that
 * routes everything else.
 *
 * Settings is split across three files by concern, because it grew past the
 * ~600-line mark CLAUDE.md's modularity rule draws and the Sound tab would
 * have taken it well past 700:
 *
 *   SettingsGame.cpp    this file -- lifecycle, tabs, update(), render()
 *   SettingsPanels.cpp  the four tab bodies and the geometry of their rows
 *   SettingsPin.cpp     the PIN pad and the Admin tab it belongs to
 *
 * The split is by concern rather than by size: the pad is a self-contained
 * modal that owns the whole screen while it runs, and the panels are rows of
 * controls that never touch it. Every member is still declared in one header,
 * so a rect and the hit test that reads it cannot drift apart. */

const char* SettingsGame::title() const { return "Settings"; }

void SettingsGame::begin(GameHost& host) {
    (void)host.requireCapability(APP_CAP_DEVICE_SETTINGS, "open settings");
    confirmReset_ = false;
    tab_ = Tab::Device;
    enteredPin_ = 0;
    enteredPinDigits_ = 0;
    pinTask_ = PinTask::None;
    pendingPin_ = 0;
    Ui::setTheme(host.board().themeMode() == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);
    markFullDirty();
}

/* Tab strip sits directly under the top bar; the rows start below its
 * baseline. The width divides evenly between the tabs, and the last one takes
 * whatever the division left over so the strip always reaches the right edge
 * -- 320 / 4 is exact, 320 / 3 was not, and the fudge belongs in one place
 * rather than in each tab's own accessor. */
Rect SettingsGame::tabRect(uint8_t index) const {
    const int16_t w = static_cast<int16_t>(panelW_ / TAB_COUNT);
    const int16_t x = static_cast<int16_t>(index * w);
    const int16_t last = index + 1 >= TAB_COUNT;
    return Rect{x, 30, last ? static_cast<int16_t>(panelW_ - x) : w, 22};
}

Rect SettingsGame::tabRectFor(Tab tab) const {
    return tabRect(static_cast<uint8_t>(tab));
}

bool SettingsGame::isAdmin(Board& board) const {
    return board.isAdminProfile(board.activeProfile());
}

/* Read the panel once per entry point. Every rect helper on this screen is
 * shared between hit testing and drawing, so they have to agree; caching it
 * here is what lets them, and picks up a rotation without any of them
 * knowing about it. */
void SettingsGame::syncPanel(GameHost& host) {
    panelW_ = static_cast<int16_t>(host.display().width());
    panelH_ = static_cast<int16_t>(host.display().height());
}

void SettingsGame::update(GameHost& host, const TouchPoint& touch) {
    syncPanel(host);
    if (!touch.justPressed) return;

    Board& board = host.board();

    /* Mid-change: the pad takes the screen until both entries are in. */
    if (pinTask_ != PinTask::None) {
        if (pinCancelRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            pinTask_ = PinTask::None;
            enteredPin_ = 0;
            enteredPinDigits_ = 0;
            pendingPin_ = 0;
            markFullDirty();
            return;
        }
        handlePinPadTouch(host, touch);
        return;
    }

    for (uint8_t i = 0; i < TAB_COUNT; ++i) {
        if (!tabRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        const Tab picked = static_cast<Tab>(i);
        if (tab_ != picked) {
            tab_ = picked;
            /* A half-armed factory reset must not survive leaving the tab it
             * lives on: coming back to a Device tab already saying "Tap to
             * ERASE" is one stray press from wiping the console. */
            confirmReset_ = false;
            markFullDirty();
        }
        return;
    }

    if (!host.requireCapability(APP_CAP_DEVICE_SETTINGS, "change settings")) {
        return;
    }

    /* Anyone may look; only the admin may change anything. Tab switching is
     * above this line so a player can still read every page.
     *
     * This gate is what actually enforces it. The two render paths grey the
     * controls out for a non-admin, but greying is a drawing decision -- until
     * this early return existed every one of those greyed rows was still live
     * and a player could toggle the lot. */
    if (!isAdmin(board)) {
        host.beepError();
        return;
    }

    if (tab_ == Tab::Admin) {
        if (changePinRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            pinTask_ = PinTask::SetNew;
            enteredPin_ = 0;
            enteredPinDigits_ = 0;
            pendingPin_ = 0;
            markFullDirty();
            return;
        }
        /* Blocking on purpose: the wizard owns the panel until it has three
         * points or times out. It is safe to call from here -- it pauses the
         * watchdog itself, saves and restores the rotation, reads the panel
         * raw so the stored calibration cannot affect it, and replaces that
         * calibration only if the new fit succeeds. A mis-tap therefore costs
         * the owner some seconds, never the calibration they already had,
         * which is why this needs no confirm step where factory reset does. */
        if (BOARD.touch.needsCalibration() &&
            recalibrateRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.runTouchCalibration();
            markFullDirty();
        }
        return;
    }

    if (tab_ == Tab::Sound) {
        /* Nothing on this tab does anything on a board with no codec, and the
         * renderer says so rather than offering the controls. */
        if (!Board::hasSound()) return;

        if (muteRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            const bool on = !board.soundEnabled();
            board.setSoundEnabled(on);
            /* Unmuting plays something immediately. A silence switch whose
             * only feedback is the word on the button leaves you pressing it
             * twice to find out which way round it is. Muting stays silent,
             * obviously. */
            if (on) board.playSound(Sound::Select);
            markFullDirty();
            return;
        }
        if (volumeRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (!board.soundEnabled()) {
                /* Dragging a slider you cannot hear is not setting a volume,
                 * it is guessing at one. Refuse out loud and leave the stored
                 * level alone. */
                board.beepError();
                return;
            }
            board.setVolume(Ui::sliderValueAt(volumeRect(), touch.x, 0,
                                              Board::AUDIO_VOLUME_MAX));
            /* The new level, at the new level. This is the only way to set a
             * volume by ear rather than by number. */
            board.playSound(Sound::Coin);
            markDirty();
            return;
        }
        if (testCueRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.playSound(board.soundEnabled() ? Sound::Victory : Sound::Correct);
            if (!board.soundEnabled()) board.beepError();
            return;
        }
        if (testVoiceRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (!board.soundEnabled()) { board.beepError(); return; }
            board.playSound(Sound::Boot);
            return;
        }
        return;
    }

    if (tab_ == Tab::Power) {
        if (idleActionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            cycleIdleAction(board); markFullDirty(); return;
        }
        if (idleAfterRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            cycleScreenSaver(board); markFullDirty(); return;
        }
        if (sleepAfterRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            // Inert unless the saver actually hands over to sleep.
            if (sleepRowActive(board)) { cycleSleepSeconds(board); markFullDirty(); }
            return;
        }
        if (wakeLockRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.setWakeLockEnabled(!board.wakeLockEnabled());
            markFullDirty(); return;
        }
        return;
    }

    if (themeRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        const Board::ThemeMode next = board.themeMode() == Board::ThemeMode::Dark
            ? Board::ThemeMode::Light : Board::ThemeMode::Dark;
        board.setThemeMode(next);
        Ui::setTheme(next == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);
        markDirty(); return;
    }
    if (layoutRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setLayoutMode(board.layoutMode() == Board::LayoutMode::Horizontal
            ? Board::LayoutMode::Vertical : Board::LayoutMode::Horizontal);
        markDirty(); return;
    }
    if (lightRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setRgbEnabled(!board.rgbEnabled()); markDirty(); return;
    }
    if (bleRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setBleBeaconEnabled(!board.bleBeaconEnabled());
        /* Nearby play rides on this radio. Turning the beacon off stands it
         * down (NearbyPlay::tick watches for exactly that); turning the beacon
         * back on re-arms whatever the owner had chosen here. */
        markDirty(); return;
    }
    if (nearbyRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (!board.bleBeaconEnabled()) {
            /* Refusing out loud beats a switch that flips and does nothing. */
            board.beepError();
            markDirty(); return;
        }
        NearbyPlay::setEnabled(board, !NearbyPlay::enabled());
        markDirty(); return;
    }
    if (resetRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (confirmReset_) {
            if (host.requireCapability(APP_CAP_FACTORY_RESET, "factory reset")) {
                board.factoryReset();
            }
        }
        confirmReset_ = true;
        markDirty(); return;
    }
    confirmReset_ = false;
    if (wifiRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.openWifi(); return;
    }
    if (ntpSyncRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        cycleNtpResyncHours(board); markDirty(); return;
    }
    if (brightRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setBrightness(Ui::sliderValueAt(brightRect(), touch.x, Board::BRIGHTNESS_MIN));
        markDirty(); return;
    }
}

void SettingsGame::renderStatic(GameHost& host) {
    syncPanel(host);
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    /* The change-PIN flow owns the whole screen while it runs, so it gets no
     * tab strip. Entering and leaving it are both full repaints, so the strip
     * cannot be left behind. */
    if (pinTask_ != PinTask::None) {
        return;
    }

    /* Four tabs across 320px is 80px each, which at font 1 holds every one of
     * these labels whole. Check a new one against the narrowest tab before
     * adding it -- TFT_eSPI drops characters off the right edge without a mark
     * rather than truncating visibly. */
    static const char* const TAB_LABELS[TAB_COUNT] = {"Device", "Power", "Sound", "Admin"};
    for (uint8_t i = 0; i < TAB_COUNT; ++i) {
        Ui::drawTab(tft, tabRect(i), TAB_LABELS[i], static_cast<Tab>(i) == tab_);
    }
    Ui::drawTabBaseline(tft, 52, 0, panelW_, tabRectFor(tab_));
}

void SettingsGame::renderDynamic(GameHost& host) {
    syncPanel(host);
    Ui::Renderer& tft = host.display();

    /* Clear the body, not the screen. The tab renderers below paint controls
     * onto whatever is already there rather than erasing behind themselves --
     * a value going from "100%" to "25%" would otherwise leave its tail -- so
     * the ground they need still has to be laid. What this saves over the old
     * full clear is the top bar and the tab strip above it, and the top bar
     * costs a battery read and five glyphs every time it is drawn. */
    const int16_t bodyTop = (pinTask_ == PinTask::None)
                                ? static_cast<int16_t>(53) : TOP_BAR_HEIGHT;
    tft.fillRect(0, bodyTop, panelW_, static_cast<int16_t>(panelH_ - bodyTop), Ui::bg());

    if (pinTask_ == PinTask::SetNew) {
        renderPinPad(host, "Enter new PIN");
        return;
    }
    if (pinTask_ == PinTask::ConfirmNew) {
        renderPinPad(host, "Re-enter new PIN");
        return;
    }

    switch (tab_) {
        case Tab::Device: renderDeviceTab(host); break;
        case Tab::Power:  renderPowerTab(host);  break;
        case Tab::Sound:  renderSoundTab(host);  break;
        case Tab::Admin:  renderAdminTab(host);  break;
    }
    tft.setTextDatum(TL_DATUM);
}
