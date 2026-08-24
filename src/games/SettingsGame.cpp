#include "SettingsGame.h"

#include "engine/NearbyPlay.h"
#include "hal/Board.h"

namespace {
/* PIN pad geometry, matching ProfileGame's: three columns, four rows, the
 * last row being DEL / 0 / OK. */
constexpr uint8_t PIN_PAD_COLS = 3;
constexpr uint8_t PIN_PAD_ROWS = 4;
constexpr int16_t PIN_PAD_TOP  = 92;
constexpr int16_t PIN_DOT_Y    = 74;
constexpr int16_t PIN_DOT_R    = 7;
constexpr uint8_t PIN_LENGTH   = 4;
}

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
 * baseline. Both tabs split the width in half. */
Rect SettingsGame::deviceTabRect() const { return Rect{0, 30, SCREEN_WIDTH / 3, 22}; }
Rect SettingsGame::powerTabRect()  const {
    return Rect{static_cast<int16_t>(SCREEN_WIDTH / 3), 30, SCREEN_WIDTH / 3, 22};
}
Rect SettingsGame::adminTabRect()  const {
    const int16_t x = static_cast<int16_t>((SCREEN_WIDTH / 3) * 2);
    return Rect{x, 30, static_cast<int16_t>(SCREEN_WIDTH - x), 22};
}

/* Admin tab: one action for now. Sized like a Device-tab row. */
Rect SettingsGame::changePinRect() const { return Rect{8, 58, 304, 30}; }

/* Abandons a half-finished PIN change. Without it a mis-tap on Change PIN
 * traps the admin on the pad with no way back. */
Rect SettingsGame::pinCancelRect() const { return Rect{6, 6, 52, 22}; }

/* Device tab: four rows of 30px, then the brightness slider.
 *
 * Network takes a whole row because Nearby needed one of the half-width slots
 * and the row it went into is the one Reset was in. Reset keeps a slot of its
 * own on the last row rather than sitting beside a harmless toggle -- it is
 * the one destructive control here.
 *
 * Nearby is deliberately next to Beacon's row rather than beside it: it does
 * nothing unless Beacon is on, and reading downwards is the order you have to
 * turn them on in. */
Rect SettingsGame::themeRect()  const { return Rect{8,    58, 144, 30}; }
Rect SettingsGame::layoutRect() const { return Rect{164,  58, 144, 30}; }
Rect SettingsGame::ntpRect()    const { return Rect{8,    92, 144, 30}; }
Rect SettingsGame::bleRect()    const { return Rect{164,  92, 144, 30}; }
Rect SettingsGame::wifiRect()   const { return Rect{8,   126, 304, 30}; }
Rect SettingsGame::nearbyRect() const { return Rect{8,   160, 144, 30}; }
Rect SettingsGame::resetRect()  const { return Rect{164, 160, 144, 30}; }
Rect SettingsGame::brightRect() const { return Rect{8,   204, 304, 32}; }

/* Power tab: three full-width rows, so the labels have room to say what the
 * setting actually does rather than abbreviating to fit half a screen. */
Rect SettingsGame::idleActionRect() const { return Rect{8,  58, 304, 30}; }
Rect SettingsGame::idleAfterRect()  const { return Rect{8,  92, 304, 30}; }
Rect SettingsGame::sleepAfterRect() const { return Rect{8, 126, 304, 30}; }

/* Standard PIN pad, laid out against the live panel size so it works in both
 * orientations and, more importantly, so every row lands above the bottom
 * edge. The previous version hard-coded rows at y=220 and buttons at y=270 on
 * a 240px-tall panel: the bottom row and both actions were drawn off-screen,
 * leaving nothing to press. */
Rect SettingsGame::pinKeyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH) const {
    const int16_t top    = PIN_PAD_TOP;
    const int16_t bottom = static_cast<int16_t>(screenH - 6);
    const int16_t pitchY = static_cast<int16_t>((bottom - top) / PIN_PAD_ROWS);
    const int16_t keyH   = static_cast<int16_t>(pitchY - 5);

    const int16_t keyW   = static_cast<int16_t>(min<int16_t>(70, (screenW - 40) / PIN_PAD_COLS - 8));
    const int16_t pitchX = static_cast<int16_t>(keyW + 8);
    const int16_t left   = static_cast<int16_t>((screenW - (pitchX * (PIN_PAD_COLS - 1) + keyW)) / 2);

    return Rect{static_cast<int16_t>(left + col * pitchX),
                static_cast<int16_t>(top + row * pitchY), keyW, keyH};
}

/* Bottom row, outer cells -- accessors so the renderer and the touch handler
 * cannot disagree about where they are. */
Rect SettingsGame::pinDeleteRect(int16_t screenW, int16_t screenH) const {
    return pinKeyRect(3, 0, screenW, screenH);
}
Rect SettingsGame::pinConfirmRect(int16_t screenW, int16_t screenH) const {
    return pinKeyRect(3, 2, screenW, screenH);
}

bool SettingsGame::sleepRowActive(Board& board) const {
    return board.idleAction() == Board::IdleAction::SaverThenSleep;
}

bool SettingsGame::isAdmin(Board& board) const {
    return board.isAdminProfile(board.activeProfile());
}

void SettingsGame::appendPinDigit(uint8_t digit) {
    if (enteredPinDigits_ < PIN_LENGTH) {
        enteredPin_ = static_cast<uint16_t>(enteredPin_ * 10 + digit);
        enteredPinDigits_++;
        markDirty();
    }
}

void SettingsGame::deletePinDigit() {
    if (enteredPinDigits_ > 0) {
        enteredPin_ /= 10;
        enteredPinDigits_--;
        markDirty();
    }
}

void SettingsGame::cycleScreenSaver(Board& board) {
    const uint16_t current = board.screenSaverSeconds();
    const uint16_t next = current < 60 ? 60 : (current < 120 ? 120 : (current < 300 ? 300 : 30));
    board.setScreenSaverSeconds(next);
}

void SettingsGame::cycleSleepSeconds(Board& board) {
    const uint16_t current = board.sleepSeconds();
    const uint16_t next = current < 30 ? 30 : (current < 60 ? 60 : (current < 120 ? 120 :
                          (current < 300 ? 300 : 15)));
    board.setSleepSeconds(next);
}

void SettingsGame::cycleIdleAction(Board& board) {
    switch (board.idleAction()) {
        case Board::IdleAction::SaverThenSleep:
            board.setIdleAction(Board::IdleAction::SleepOnly); break;
        case Board::IdleAction::SleepOnly:
            board.setIdleAction(Board::IdleAction::SaverOnly); break;
        default:
            board.setIdleAction(Board::IdleAction::SaverThenSleep); break;
    }
}

/* One pad, three jobs: unlock, enter a new PIN, confirm it. Keeping the hit
 * testing in a single place is what stops the renderer and the handler
 * disagreeing about which cell is which. */
bool SettingsGame::handlePinPadTouch(GameHost& host, const TouchPoint& touch) {
    Board& board = host.board();
    const int16_t W = static_cast<int16_t>(host.display().width());
    const int16_t H = static_cast<int16_t>(host.display().height());

    if (pinConfirmRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (enteredPinDigits_ != PIN_LENGTH) {
            host.beepError();          // partial entry: nothing to judge yet
            return true;
        }
        const uint16_t value = enteredPin_;
        enteredPin_ = 0;
        enteredPinDigits_ = 0;

        switch (pinTask_) {
            case PinTask::None:
                break;      // pad is not in use
            case PinTask::SetNew:
                pendingPin_ = value;
                pinTask_ = PinTask::ConfirmNew;
                host.beepOk();
                break;
            case PinTask::ConfirmNew:
                /* Only commit when both entries agree -- a mistyped new PIN
                 * that is stored anyway locks the owner out of their own
                 * device, and there is no recovery path short of a factory
                 * reset. */
                if (value == pendingPin_) {
                    board.setAdminPin(value);
                    host.beepOk();
                } else {
                    host.beepError();
                }
                pendingPin_ = 0;
                pinTask_ = PinTask::None;
                break;
        }
        markFullDirty();
        return true;
    }

    if (pinDeleteRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        deletePinDigit();
        return true;
    }
    /* Rows 0-2 carry 1-9; row 3 middle cell is 0. DEL and OK are the outer
     * cells of row 3 and were handled above. */
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t col = 0; col < PIN_PAD_COLS; ++col) {
            if (pinKeyRect(row, col, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                appendPinDigit(static_cast<uint8_t>(row * PIN_PAD_COLS + col + 1));
                return true;
            }
        }
    }
    if (pinKeyRect(3, 1, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        appendPinDigit(0);
        return true;
    }
    return false;
}

void SettingsGame::update(GameHost& host, const TouchPoint& touch) {
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

    if (deviceTabRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (tab_ != Tab::Device) { tab_ = Tab::Device; confirmReset_ = false; markFullDirty(); }
        return;
    }
    if (powerTabRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (tab_ != Tab::Power) { tab_ = Tab::Power; confirmReset_ = false; markFullDirty(); }
        return;
    }
    if (adminTabRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (tab_ != Tab::Admin) { tab_ = Tab::Admin; confirmReset_ = false; markFullDirty(); }
        return;
    }

    if (!host.requireCapability(APP_CAP_DEVICE_SETTINGS, "change settings")) {
        return;
    }

    /* Anyone may look; only the admin may change anything. Tab switching is
     * above this line so a child can still read every page.
     *
     * This gate is what actually enforces it. The two render paths grey the
     * controls out for a non-admin, but greying is a drawing decision -- until
     * this early return existed every one of those greyed rows was still live
     * and a child could toggle the lot. */
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
    if (ntpRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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
    if (brightRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setBrightness(Ui::sliderValueAt(brightRect(), touch.x, Board::BRIGHTNESS_MIN));
        markDirty(); return;
    }
}

void SettingsGame::renderDeviceTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const bool admin = isAdmin(board);

    char label[28];
    snprintf(label, sizeof(label), "Theme: %s",
             board.themeMode() == Board::ThemeMode::Dark ? "Dark" : "Light");
    Ui::drawButton(tft, themeRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);
    snprintf(label, sizeof(label), "Menu: %s",
             board.layoutMode() == Board::LayoutMode::Horizontal ? "Horizontal" : "Vertical");
    Ui::drawButton(tft, layoutRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);
    snprintf(label, sizeof(label), "Light: %s", board.rgbEnabled() ? "On" : "Off");
    Ui::drawButton(tft, ntpRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);
    snprintf(label, sizeof(label), "Beacon: %s", board.bleBeaconEnabled() ? "On" : "Off");
    Ui::drawButton(tft, bleRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);
    Ui::drawButton(tft, wifiRect(), "Network", admin ? Ui::rgb(36, 132, 204) : Ui::rgb(80, 80, 80),
                   Ui::outline(), admin ? TFT_WHITE : Ui::muted(), false, 2);
    /* Greyed, not hidden, when the beacon is off: the control is what tells
     * you the feature exists and what it depends on. */
    const bool beaconOn = board.bleBeaconEnabled();
    snprintf(label, sizeof(label), "Nearby: %s",
             !beaconOn ? "needs Beacon" : (NearbyPlay::enabled() ? "On" : "Off"));
    const bool nearbyTextEnabled = admin && beaconOn;
    Ui::drawButton(tft, nearbyRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), nearbyTextEnabled ? Ui::text() : Ui::muted(), false, 2);
    Ui::drawButton(tft, resetRect(),
                   confirmReset_ ? "Tap to ERASE" : "Reset device",
                   admin ? (confirmReset_ ? Ui::rgb(220, 40, 40) : Ui::rgb(120, 58, 58)) : Ui::rgb(80, 80, 80),
                   Ui::outline(), admin ? TFT_WHITE : Ui::muted(), false, 2);

    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    if (!admin) {
        tft.drawString("Settings locked. Only Admin can change.", 8, 194, 1);
    } else {
        tft.drawString(confirmReset_ ? "Erases scores, names, Wi-Fi and settings"
                                 : "Brightness", 8, 194, 1);
        tft.setTextDatum(TR_DATUM);
        snprintf(label, sizeof(label), "%u%%", board.brightness());
        tft.drawString(label, SCREEN_WIDTH - 8, 194, 1);
        tft.setTextDatum(TL_DATUM);
        Ui::drawSlider(tft, brightRect(), board.brightness(), Board::BRIGHTNESS_MIN);
    }
}

void SettingsGame::renderPowerTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const bool admin = isAdmin(board);

    const Board::IdleAction action = board.idleAction();
    const uint16_t idleSecs  = board.screenSaverSeconds();
    const uint16_t sleepSecs = board.sleepSeconds();

    const char* actionName =
        action == Board::IdleAction::SaverThenSleep ? "Saver then sleep" :
        action == Board::IdleAction::SleepOnly      ? "Sleep only"       : "Saver only";

    char buf[52];
    snprintf(buf, sizeof(buf), "When idle: %s", actionName);
    Ui::drawButton(tft, idleActionRect(), buf,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);

    if (idleSecs >= 60 && idleSecs % 60 == 0) {
        snprintf(buf, sizeof(buf), "Idle after: %um", idleSecs / 60);
    } else {
        snprintf(buf, sizeof(buf), "Idle after: %us", idleSecs);
    }
    Ui::drawButton(tft, idleAfterRect(), buf,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);

    /* The sleep delay only means anything when the saver hands over to sleep.
     * The other two policies say so on the button rather than leaving a live
     * control that quietly does nothing. */
    const bool sleepLive = sleepRowActive(board) && admin;
    if (!sleepLive) {
        snprintf(buf, sizeof(buf), "Sleep after: %s",
                 action == Board::IdleAction::SleepOnly ? "(at idle)" : "--");
    } else if (sleepSecs >= 60 && sleepSecs % 60 == 0) {
        snprintf(buf, sizeof(buf), "Sleep after: %um", sleepSecs / 60);
    } else {
        snprintf(buf, sizeof(buf), "Sleep after: %us", sleepSecs);
    }
    Ui::drawButton(tft, sleepAfterRect(), buf,
                   sleepLive ? Ui::panel() : Ui::surface(), Ui::outline(),
                   sleepLive ? Ui::text() : Ui::muted(), false, 2);

    /* Say the resulting behaviour in plain words, composed from the live
     * values -- three settings that interact are hard to hold in your head. */
    char explain[64];
    if (!admin) {
        snprintf(explain, sizeof(explain), "Settings locked. Only Admin can change.");
    } else {
        switch (action) {
            case Board::IdleAction::SleepOnly:
                snprintf(explain, sizeof(explain), "Screen off at %us. No screen saver.", idleSecs);
                break;
            case Board::IdleAction::SaverOnly:
                snprintf(explain, sizeof(explain), "Saver at %us. Screen never turns off.", idleSecs);
                break;
            default:
                snprintf(explain, sizeof(explain), "Saver at %us, screen off at %us.",
                         idleSecs, static_cast<uint16_t>(idleSecs + sleepSecs));
                break;
        }
    }
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(explain, 8, 168, 1);
    if (admin) {
        tft.drawString("Sleep blanks the screen. A touch wakes it.", 8, 184, 1);
    }
}

void SettingsGame::renderPinPad(GameHost& host, const char* heading) {
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());

    Ui::drawButton(tft, pinCancelRect(), "Back", Ui::panel(), Ui::outline(),
                   Ui::text(), false, 1);

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);
    tft.drawString(heading, W / 2, 38, 2);

    /* Dots, not the digits: the PIN is masked. The previous version printed
     * it in plain text in a box, which defeats the point of having one. */
    const int16_t dotPitch = 26;
    const int16_t dotsX0   = static_cast<int16_t>(W / 2 - (dotPitch * (PIN_LENGTH - 1)) / 2);
    for (uint8_t i = 0; i < PIN_LENGTH; ++i) {
        const int16_t x = static_cast<int16_t>(dotsX0 + i * dotPitch);
        const uint16_t fill = i < enteredPinDigits_ ? Ui::success() : Ui::panel();
        tft.fillCircle(x, PIN_DOT_Y, PIN_DOT_R, fill);
        tft.drawCircle(x, PIN_DOT_Y, PIN_DOT_R, Ui::outline());
    }

    /* Rows 0-2 are 1-9; row 3 is DEL / 0 / OK. */
    for (uint8_t row = 0; row < PIN_PAD_ROWS; ++row) {
        for (uint8_t col = 0; col < PIN_PAD_COLS; ++col) {
            const Rect r = pinKeyRect(row, col, W, H);

            const char* label;
            char digitLabel[2] = {0, 0};
            uint16_t fill = Ui::panel();

            if (row < 3) {
                digitLabel[0] = static_cast<char>('1' + row * PIN_PAD_COLS + col);
                label = digitLabel;
            } else if (col == 0) {
                label = "DEL";
                fill  = Ui::rgb(150, 60, 60);
            } else if (col == 1) {
                label = "0";
            } else {
                label = "OK";
                fill  = Ui::rgb(45, 154, 96);
            }

            Ui::drawButton(tft, r, label, fill, Ui::outline(), Ui::text(), false, 2);
        }
    }
    tft.setTextDatum(TL_DATUM);
}

void SettingsGame::render(GameHost& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    /* The change-PIN flow owns the whole screen while it runs, so there is no
     * half-changed state visible behind it. */
    if (pinTask_ == PinTask::SetNew) {
        renderPinPad(host, "Enter new PIN");
        return;
    }
    if (pinTask_ == PinTask::ConfirmNew) {
        renderPinPad(host, "Re-enter new PIN");
        return;
    }

    const Rect devTab = deviceTabRect();
    const Rect powTab = powerTabRect();
    const Rect admTab = adminTabRect();
    Ui::drawTab(tft, devTab, "Device", tab_ == Tab::Device);
    Ui::drawTab(tft, powTab, "Power", tab_ == Tab::Power);
    Ui::drawTab(tft, admTab, "Admin", tab_ == Tab::Admin);
    Ui::drawTabBaseline(tft, 52, 0, SCREEN_WIDTH,
                        tab_ == Tab::Device ? devTab
                                            : (tab_ == Tab::Power ? powTab : admTab));

    if (tab_ == Tab::Device) {
        renderDeviceTab(host);
    } else if (tab_ == Tab::Power) {
        renderPowerTab(host);
    } else {
        renderAdminTab(host);
    }
    tft.setTextDatum(TL_DATUM);
}

void SettingsGame::renderAdminTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const bool admin = isAdmin(board);

    Ui::drawButton(tft, changePinRect(), "Change admin PIN",
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(),
                   admin ? Ui::text() : Ui::muted(), false, 2);

    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString("The PIN guards the admin profile. It", 8, 100, 1);
    tft.drawString("ships as 0000 -- change it.", 8, 116, 1);

    /* Name the admin rather than restating that one exists: if the profile is
     * renamed this follows it. */
    char who[40];
    const uint8_t adminIdx = board.adminProfileIndex();
    if (adminIdx == Board::GUEST_INDEX) {
        snprintf(who, sizeof(who), "No admin profile is set");
    } else {
        snprintf(who, sizeof(who), "Admin profile: %s", board.profileName(adminIdx).c_str());
    }
    tft.drawString(who, 8, 140, 1);

    tft.setTextDatum(TC_DATUM);
    tft.drawString(admin ? "Entered twice; only saved if both match."
                         : "Only the admin can change settings.",
                   W / 2, static_cast<int16_t>(tft.height() - 20), 1);
    tft.setTextDatum(TL_DATUM);
}
