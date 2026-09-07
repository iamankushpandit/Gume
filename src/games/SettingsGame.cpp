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
    Ui::setTheme(static_cast<Ui::Theme>(host.board().themeMode()));
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
        /* One button, two directions: the left half steps back, the right
         * half steps forward.
         *
         * Two themes were a toggle and one button was plenty. Nine are a list,
         * and a forward-only cycle means nine taps to undo one -- on the
         * setting most likely to be tried out of curiosity and then put back.
         * The Device tab has no room for a second control: the grid is exactly
         * four rows by construction and the brightness bar owns everything
         * below it. Splitting the target costs no pixels, and the label says
         * so with chevrons on both sides. */
        const uint8_t count = static_cast<uint8_t>(Ui::Theme::Count);
        const Rect box = themeRect();
        const bool back = touch.x < box.x + box.w / 2;
        const uint8_t current = static_cast<uint8_t>(board.themeMode());
        const uint8_t next = static_cast<uint8_t>(
            back ? (current + count - 1) % count : (current + 1) % count);
        board.setThemeMode(static_cast<Board::ThemeMode>(next));
        Ui::setTheme(static_cast<Ui::Theme>(next));
        /* markFullDirty(), NOT markDirty().
         *
         * A theme changes the ground and the chrome, not the content on top of
         * it. markDirty() means "repaint what moved", so the tab strip and the
         * panel background -- which are painted only under needsFullRender()
         * -- kept the palette they were drawn with, and switching from Light
         * to Dark left light-coloured tabs sitting above a dark screen. That
         * was the behaviour before this change, with only two themes to notice
         * it in. */
        markFullDirty(); return;
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
     * cannot be left behind.
     *
     * The pad's chrome belongs here rather than in renderDynamic(): the keys,
     * the heading and Back do not change while a PIN is being typed, and
     * repainting them on every digit is what made this screen flash. */
    if (pinTask_ != PinTask::None) {
        renderPinPadChrome(host, pinTask_ == PinTask::SetNew ? "Enter new PIN"
                                                             : "Re-enter new PIN");
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

    /* A keypress changes four dots. It does not need the body cleared, and
     * clearing it is precisely what the flicker was -- so this returns before
     * the fill below rather than after it. */
    if (pinTask_ != PinTask::None) {
        renderPinDots(host);
        return;
    }

    /* Clear the body, not the screen. The tab renderers below paint controls
     * onto whatever is already there rather than erasing behind themselves --
     * a value going from "100%" to "25%" would otherwise leave its tail -- so
     * the ground they need still has to be laid. What this saves over the old
     * full clear is the top bar and the tab strip above it, and the top bar
     * costs a battery read and five glyphs every time it is drawn. */
    tft.fillRect(0, 53, panelW_, static_cast<int16_t>(panelH_ - 53), Ui::bg());

    switch (tab_) {
        case Tab::Device: renderDeviceTab(host); break;
        case Tab::Power:  renderPowerTab(host);  break;
        case Tab::Sound:  renderSoundTab(host);  break;
        case Tab::Admin:  renderAdminTab(host);  break;
    }
    tft.setTextDatum(TL_DATUM);
}
