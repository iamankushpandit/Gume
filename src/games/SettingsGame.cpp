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
            /* Muting greys the slider, both test buttons and the footnote,
             * so this genuinely is the whole body -- but the top bar and the
             * tab strip above it did not change, and markFullDirty() was
             * repainting those too, battery read included. */
            markDirty();
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
            /* The slider AND its readout, which sits 12px above it (see
             * renderSoundTab). Dragging is the fastest-repainting thing in
             * this app -- a drag is a touch every frame -- so it is the one
             * that most needed to stop clearing the whole tab. */
            markControl(Rect{volumeRect().x,
                             static_cast<int16_t>(volumeRect().y - 14),
                             volumeRect().w,
                             static_cast<int16_t>(volumeRect().h + 14)});
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
            /* This one really does change the tab: it decides whether the
              * sleep row is live, and the footnote restates both numbers. */
             cycleIdleAction(board); markDirty(); return;
        }
        if (idleAfterRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            cycleScreenSaver(board); markDirty(); return;   // footnote restates it
        }
        if (sleepAfterRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            // Inert unless the saver actually hands over to sleep.
            if (sleepRowActive(board)) { cycleSleepSeconds(board); markDirty(); }
            return;
        }
        if (wakeLockRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.setWakeLockEnabled(!board.wakeLockEnabled());
            markControl(wakeLockRect()); return;
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
        markControl(layoutRect()); return;
    }
    if (lightRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setRgbEnabled(!board.rgbEnabled()); markControl(lightRect()); return;
    }
    if (bleRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setBleBeaconEnabled(!board.bleBeaconEnabled());
        /* Nearby play rides on this radio. Turning the beacon off stands it
         * down (NearbyPlay::tick watches for exactly that); turning the beacon
         * back on re-arms whatever the owner had chosen here. */
        markControl(bleRect());
        markControl(nearbyRect());   // the beacon takes Nearby with it
        return;
    }
    if (nearbyRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (!board.bleBeaconEnabled()) {
            /* Refusing out loud beats a switch that flips and does nothing. */
            board.beepError();
            markControl(nearbyRect()); return;
        }
        NearbyPlay::setEnabled(board, !NearbyPlay::enabled());
        markControl(nearbyRect()); return;
    }
    if (resetRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (confirmReset_) {
            if (host.requireCapability(APP_CAP_FACTORY_RESET, "factory reset")) {
                board.factoryReset();
            }
        }
        confirmReset_ = true;
        markControl(resetRect()); return;
    }
    /* A tap anywhere else disarms the reset, and that CHANGES the reset row:
     * it goes from "Tap to ERASE" back to "Reset device". The whole-body
     * repaint used to cover this by accident; a clipped one will not. */
    if (confirmReset_) { confirmReset_ = false; markControl(resetRect()); }
    if (wifiRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.openWifi(); return;
    }
    if (ntpSyncRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        cycleNtpResyncHours(board); markControl(ntpSyncRect()); return;
    }
    if (brightRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setBrightness(Ui::sliderValueAt(brightRect(), touch.x, Board::BRIGHTNESS_MIN));
        /* The slider and the readout above it -- same shape as the volume
         * one, and the same reason: a drag is a touch every frame. */
        markControl(Rect{brightRect().x,
                         static_cast<int16_t>(brightRect().y - 14),
                         brightRect().w,
                         static_cast<int16_t>(brightRect().h + 14)});
        return;
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

void SettingsGame::markControl(const Rect& r) {
    if (r.w <= 0 || r.h <= 0) { markDirty(); return; }
    if (dirtyRect_.w <= 0) {
        dirtyRect_ = r;
    } else {
        /* Two controls changed before either was painted -- a cycle that
         * turns another row inert, say. Cover both rather than dropping one;
         * the union of two rows is still a fraction of the body. */
        const int16_t x1 = static_cast<int16_t>(max(dirtyRect_.x + dirtyRect_.w, r.x + r.w));
        const int16_t y1 = static_cast<int16_t>(max(dirtyRect_.y + dirtyRect_.h, r.y + r.h));
        dirtyRect_.x = static_cast<int16_t>(min(dirtyRect_.x, r.x));
        dirtyRect_.y = static_cast<int16_t>(min(dirtyRect_.y, r.y));
        dirtyRect_.w = static_cast<int16_t>(x1 - dirtyRect_.x);
        dirtyRect_.h = static_cast<int16_t>(y1 - dirtyRect_.y);
    }
    markDirty();
}

void SettingsGame::renderDynamic(GameHost& host) {
    syncPanel(host);
    Ui::Renderer& tft = host.display();

    /* A keypress changes four dots. It does not need the body cleared, and
     * clearing it is precisely what the flicker was -- so this returns before
     * the fill below rather than after it. */
    if (pinTask_ != PinTask::None) {
        renderPinDots(host);
        dirtyRect_ = Rect{0, 0, 0, 0};
        return;
    }

    /* The body is everything below the tab strip. A full repaint of it is
     * ~187 rows over SPI; one control is a tenth of that. */
    const int16_t bodyY = 53;
    const int16_t bodyH = static_cast<int16_t>(panelH_ - bodyY);
    Rect box{0, bodyY, panelW_, bodyH};

    /* A full render is entering the screen, changing tab or rotating, and all
     * three genuinely change the whole picture -- the rect is ignored there. */
    const bool whole = needsFullRender() || dirtyRect_.w <= 0;
    if (!whole) {
        box = dirtyRect_;
        /* Clamp into the body. A control's own Rect cannot escape it, but a
         * union of two can round outward, and a viewport wider than the panel
         * is how the tab strip got painted over once already. */
        if (box.y < bodyY) {
            box.h = static_cast<int16_t>(box.h - (bodyY - box.y));
            box.y = bodyY;
        }
        if (box.x < 0) { box.w = static_cast<int16_t>(box.w + box.x); box.x = 0; }
        if (box.x + box.w > panelW_) box.w = static_cast<int16_t>(panelW_ - box.x);
        if (box.y + box.h > panelH_) box.h = static_cast<int16_t>(panelH_ - box.y);
        if (box.w <= 0 || box.h <= 0) { dirtyRect_ = Rect{0, 0, 0, 0}; return; }
        /* A couple of pixels of daylight: a button paints a shadow and a bevel
         * outside the rect it was measured from, and clipping those off leaves
         * a hairline of the old fill along the edge. */
        box.x = static_cast<int16_t>(box.x > 2 ? box.x - 2 : 0);
        box.y = static_cast<int16_t>(box.y > bodyY + 2 ? box.y - 2 : bodyY);
        box.w = static_cast<int16_t>(box.x + box.w + 4 <= panelW_ ? box.w + 4 : panelW_ - box.x);
        box.h = static_cast<int16_t>(box.y + box.h + 4 <= panelH_ ? box.h + 4 : panelH_ - box.y);
        /* vpDatum=false keeps drawing coordinates absolute, so the renderers
         * below need to know nothing about any of this. */
        tft.setViewport(box.x, box.y, box.w, box.h, false);
    }

    /* Clear the ground first: the tab renderers paint controls onto whatever
     * is already there rather than erasing behind themselves -- a value going
     * from "100%" to "25%" would otherwise leave its tail. Inside a viewport
     * this clears only the box. */
    tft.fillRect(box.x, box.y, box.w, box.h, Ui::bg());

    switch (tab_) {
        case Tab::Device: renderDeviceTab(host); break;
        case Tab::Power:  renderPowerTab(host);  break;
        case Tab::Sound:  renderSoundTab(host);  break;
        case Tab::Admin:  renderAdminTab(host);  break;
    }
    tft.setTextDatum(TL_DATUM);
    if (!whole) tft.resetViewport();
    dirtyRect_ = Rect{0, 0, 0, 0};
}
