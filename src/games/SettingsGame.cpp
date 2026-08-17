#include "SettingsGame.h"

#include "engine/NearbyPlay.h"
#include "hal/Board.h"
#include "ui/GameLayout.h"

namespace {
constexpr int16_t ROWS_TOP  = 58;   // first control row, below the tab baseline
constexpr int16_t ROW_PITCH = 34;
constexpr int16_t ROW_H     = 30;
constexpr int16_t COL_GAP   = 12;   // between the two halves of a paired row
}

const char* SettingsGame::title() const { return "Settings"; }

void SettingsGame::begin(GameHost& host) {
    (void)host.requireCapability(APP_CAP_DEVICE_SETTINGS, "open settings");
    confirmReset_ = false;
    tab_ = Tab::Device;
    Ui::setTheme(host.board().themeMode() == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);
    markFullDirty();
}

/* Tab strip sits directly under the top bar; the rows start below its
 * baseline. Both tabs split the width in half, whatever the width is. */
Rect SettingsGame::deviceTabRect(const Ui::Frame& f) const {
    return Rect{0, 30, static_cast<int16_t>(f.w / 2), 22};
}
Rect SettingsGame::powerTabRect(const Ui::Frame& f) const {
    return Rect{static_cast<int16_t>(f.w / 2), 30,
                static_cast<int16_t>(f.w - f.w / 2), 22};
}

Rect SettingsGame::rowBand(const Ui::Frame& f, uint8_t row) const {
    return Rect{8, static_cast<int16_t>(ROWS_TOP + row * ROW_PITCH),
                static_cast<int16_t>(f.w - 16), ROW_H};
}

/* Device tab: four rows of 30px, then the brightness slider.
 *
 * Network takes a whole row because Nearby needed one of the half-width slots
 * and the row it went into is the one Reset was in. Reset keeps a slot of its
 * own on the last row rather than sitting beside a harmless toggle -- it is
 * the one destructive control here.
 *
 * Nearby is deliberately next to Beacon's row rather than beside it: it does
 * nothing unless Beacon is on, and reading downwards is the order you have to
 * turn them on in.
 *
 * The paired rows split whatever width the panel has rather than the 320 they
 * were authored against; on a 320px panel gridCell reproduces the authored
 * 8/164 columns to within two pixels. */
Rect SettingsGame::themeRect(const Ui::Frame& f)  const { return Ui::gridCell(rowBand(f, 0), 2, 1, 0, COL_GAP); }
Rect SettingsGame::layoutRect(const Ui::Frame& f) const { return Ui::gridCell(rowBand(f, 0), 2, 1, 1, COL_GAP); }
Rect SettingsGame::ntpRect(const Ui::Frame& f)    const { return Ui::gridCell(rowBand(f, 1), 2, 1, 0, COL_GAP); }
Rect SettingsGame::bleRect(const Ui::Frame& f)    const { return Ui::gridCell(rowBand(f, 1), 2, 1, 1, COL_GAP); }
Rect SettingsGame::wifiRect(const Ui::Frame& f)   const { return rowBand(f, 2); }
Rect SettingsGame::nearbyRect(const Ui::Frame& f) const { return Ui::gridCell(rowBand(f, 3), 2, 1, 0, COL_GAP); }
Rect SettingsGame::resetRect(const Ui::Frame& f)  const { return Ui::gridCell(rowBand(f, 3), 2, 1, 1, COL_GAP); }

/* The slider hangs off the bottom edge rather than the last row, so the
 * controls stay at the top and the extra height of a taller panel falls in
 * between instead of stranding the slider mid-screen. */
Rect SettingsGame::brightRect(const Ui::Frame& f) const {
    return Rect{8, static_cast<int16_t>(f.h - 36), static_cast<int16_t>(f.w - 16), 32};
}

/* Power tab: three full-width rows, so the labels have room to say what the
 * setting actually does rather than abbreviating to fit half a screen. */
Rect SettingsGame::idleActionRect(const Ui::Frame& f) const { return rowBand(f, 0); }
Rect SettingsGame::idleAfterRect(const Ui::Frame& f)  const { return rowBand(f, 1); }
Rect SettingsGame::sleepAfterRect(const Ui::Frame& f) const { return rowBand(f, 2); }

bool SettingsGame::sleepRowActive(Board& board) const {
    return board.idleAction() == Board::IdleAction::SaverThenSleep;
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

void SettingsGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;

    Board& board = host.board();
    const Ui::Frame f = Ui::frame(host.display());

    if (deviceTabRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (tab_ != Tab::Device) { tab_ = Tab::Device; confirmReset_ = false; markFullDirty(); }
        return;
    }
    if (powerTabRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (tab_ != Tab::Power) { tab_ = Tab::Power; confirmReset_ = false; markFullDirty(); }
        return;
    }

    if (!host.requireCapability(APP_CAP_DEVICE_SETTINGS, "change settings")) {
        return;
    }

    if (tab_ == Tab::Power) {
        if (idleActionRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            cycleIdleAction(board); markFullDirty(); return;
        }
        if (idleAfterRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            cycleScreenSaver(board); markFullDirty(); return;
        }
        if (sleepAfterRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            // Inert unless the saver actually hands over to sleep.
            if (sleepRowActive(board)) { cycleSleepSeconds(board); markFullDirty(); }
            return;
        }
        return;
    }

    if (themeRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        const Board::ThemeMode next = board.themeMode() == Board::ThemeMode::Dark
            ? Board::ThemeMode::Light : Board::ThemeMode::Dark;
        board.setThemeMode(next);
        Ui::setTheme(next == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);
        markDirty(); return;
    }
    if (layoutRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setLayoutMode(board.layoutMode() == Board::LayoutMode::Horizontal
            ? Board::LayoutMode::Vertical : Board::LayoutMode::Horizontal);
        markDirty(); return;
    }
    if (ntpRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setRgbEnabled(!board.rgbEnabled()); markDirty(); return;
    }
    if (bleRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setBleBeaconEnabled(!board.bleBeaconEnabled());
        /* Nearby play rides on this radio. Turning the beacon off stands it
         * down (NearbyPlay::tick watches for exactly that); turning the beacon
         * back on re-arms whatever the owner had chosen here. */
        markDirty(); return;
    }
    if (nearbyRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (!board.bleBeaconEnabled()) {
            /* Refusing out loud beats a switch that flips and does nothing. */
            board.beepError();
            markDirty(); return;
        }
        NearbyPlay::setEnabled(board, !NearbyPlay::enabled());
        markDirty(); return;
    }
    if (resetRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (confirmReset_) {
            if (host.requireCapability(APP_CAP_FACTORY_RESET, "factory reset")) {
                board.factoryReset();
            }
        }
        confirmReset_ = true;
        markDirty(); return;
    }
    confirmReset_ = false;
    if (wifiRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.openWifi(); return;
    }
    if (brightRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setBrightness(Ui::sliderValueAt(brightRect(f), touch.x, Board::BRIGHTNESS_MIN));
        markDirty(); return;
    }
}

void SettingsGame::renderDeviceTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);

    char label[28];
    snprintf(label, sizeof(label), "Theme: %s",
             board.themeMode() == Board::ThemeMode::Dark ? "Dark" : "Light");
    Ui::drawButton(tft, themeRect(f), label,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    snprintf(label, sizeof(label), "Menu: %s",
             board.layoutMode() == Board::LayoutMode::Horizontal ? "Horizontal" : "Vertical");
    Ui::drawButton(tft, layoutRect(f), label,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    snprintf(label, sizeof(label), "Light: %s", board.rgbEnabled() ? "On" : "Off");
    Ui::drawButton(tft, ntpRect(f), label,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    snprintf(label, sizeof(label), "Beacon: %s", board.bleBeaconEnabled() ? "On" : "Off");
    Ui::drawButton(tft, bleRect(f), label,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, wifiRect(f), "Network", Ui::rgb(36, 132, 204), Ui::outline(), TFT_WHITE, false, 2);
    /* Greyed, not hidden, when the beacon is off: the control is what tells
     * you the feature exists and what it depends on. */
    const bool beaconOn = board.bleBeaconEnabled();
    snprintf(label, sizeof(label), "Nearby: %s",
             !beaconOn ? "needs Beacon" : (NearbyPlay::enabled() ? "On" : "Off"));
    Ui::drawButton(tft, nearbyRect(f), label,
                   Ui::panel(), Ui::outline(), beaconOn ? Ui::text() : Ui::muted(), false, 2);
    Ui::drawButton(tft, resetRect(f),
                   confirmReset_ ? "Tap to ERASE" : "Reset device",
                   confirmReset_ ? Ui::rgb(220, 40, 40) : Ui::rgb(120, 58, 58),
                   Ui::outline(), TFT_WHITE, false, 2);

    const Rect bright = brightRect(f);
    const int16_t captionY = static_cast<int16_t>(bright.y - 10);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(confirmReset_ ? "Erases scores, names, Wi-Fi and settings"
                             : "Brightness", 8, captionY, 1);
    tft.setTextDatum(TR_DATUM);
    snprintf(label, sizeof(label), "%u%%", board.brightness());
    tft.drawString(label, static_cast<int16_t>(f.w - 8), captionY, 1);
    tft.setTextDatum(TL_DATUM);
    Ui::drawSlider(tft, bright, board.brightness(), Board::BRIGHTNESS_MIN);
}

void SettingsGame::renderPowerTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);

    const Board::IdleAction action = board.idleAction();
    const uint16_t idleSecs  = board.screenSaverSeconds();
    const uint16_t sleepSecs = board.sleepSeconds();

    const char* actionName =
        action == Board::IdleAction::SaverThenSleep ? "Saver then sleep" :
        action == Board::IdleAction::SleepOnly      ? "Sleep only"       : "Saver only";

    char buf[52];
    snprintf(buf, sizeof(buf), "When idle: %s", actionName);
    Ui::drawButton(tft, idleActionRect(f), buf,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);

    if (idleSecs >= 60 && idleSecs % 60 == 0) {
        snprintf(buf, sizeof(buf), "Idle after: %um", idleSecs / 60);
    } else {
        snprintf(buf, sizeof(buf), "Idle after: %us", idleSecs);
    }
    Ui::drawButton(tft, idleAfterRect(f), buf,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);

    /* The sleep delay only means anything when the saver hands over to sleep.
     * The other two policies say so on the button rather than leaving a live
     * control that quietly does nothing. */
    const bool sleepLive = sleepRowActive(board);
    if (!sleepLive) {
        snprintf(buf, sizeof(buf), "Sleep after: %s",
                 action == Board::IdleAction::SleepOnly ? "(at idle)" : "--");
    } else if (sleepSecs >= 60 && sleepSecs % 60 == 0) {
        snprintf(buf, sizeof(buf), "Sleep after: %um", sleepSecs / 60);
    } else {
        snprintf(buf, sizeof(buf), "Sleep after: %us", sleepSecs);
    }
    Ui::drawButton(tft, sleepAfterRect(f), buf,
                   sleepLive ? Ui::panel() : Ui::surface(), Ui::outline(),
                   sleepLive ? Ui::text() : Ui::muted(), false, 2);

    /* Say the resulting behaviour in plain words, composed from the live
     * values -- three settings that interact are hard to hold in your head. */
    char explain[64];
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
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    const int16_t explainY = static_cast<int16_t>(sleepAfterRect(f).y + ROW_H + 12);
    tft.drawString(explain, 8, explainY, 1);
    tft.drawString("Sleep blanks the screen. A touch wakes it.", 8,
                   static_cast<int16_t>(explainY + 16), 1);
}

void SettingsGame::render(GameHost& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    const Rect devTab = deviceTabRect(f);
    const Rect powTab = powerTabRect(f);
    Ui::drawTab(tft, devTab, "Device", tab_ == Tab::Device);
    Ui::drawTab(tft, powTab, "Power", tab_ == Tab::Power);
    Ui::drawTabBaseline(tft, 52, 0, f.w,
                        tab_ == Tab::Device ? devTab : powTab);

    if (tab_ == Tab::Device) {
        renderDeviceTab(host);
    } else {
        renderPowerTab(host);
    }
    tft.setTextDatum(TL_DATUM);
}
