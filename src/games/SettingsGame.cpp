#include "SettingsGame.h"

#include "engine/NearbyPlay.h"
#include "hal/Board.h"

const char* SettingsGame::title() const { return "Settings"; }

void SettingsGame::begin(GameHost& host) {
    (void)host.requireCapability(APP_CAP_DEVICE_SETTINGS, "open settings");
    confirmReset_ = false;
    tab_ = Tab::Device;
    Ui::setTheme(host.board().themeMode() == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);
    markFullDirty();
}

/* Tab strip sits directly under the top bar; the rows start below its
 * baseline. Both tabs split the width in half. */
Rect SettingsGame::deviceTabRect(int16_t W) const { return Rect{0,   30, static_cast<int16_t>(W/2), 22}; }
Rect SettingsGame::powerTabRect(int16_t W)  const { return Rect{static_cast<int16_t>(W/2), 30, static_cast<int16_t>(W - W/2), 22}; }

/* Device tab rows: two half-width columns, then full-width rows, then slider.
 * All x/w values are derived from W so portrait and landscape both fit. */
Rect SettingsGame::themeRect(int16_t W)  const { const int16_t hw=static_cast<int16_t>(W/2-12); return Rect{8,    58, hw, 30}; }
Rect SettingsGame::layoutRect(int16_t W) const { const int16_t hw=static_cast<int16_t>(W/2-12); return Rect{static_cast<int16_t>(W/2+4), 58, hw, 30}; }
Rect SettingsGame::ntpRect(int16_t W)    const { const int16_t hw=static_cast<int16_t>(W/2-12); return Rect{8,    92, hw, 30}; }
Rect SettingsGame::bleRect(int16_t W)    const { const int16_t hw=static_cast<int16_t>(W/2-12); return Rect{static_cast<int16_t>(W/2+4), 92, hw, 30}; }
Rect SettingsGame::wifiRect(int16_t W)   const { return Rect{8,   126, static_cast<int16_t>(W-16), 30}; }
Rect SettingsGame::nearbyRect(int16_t W) const { const int16_t hw=static_cast<int16_t>(W/2-12); return Rect{8,   160, hw, 30}; }
Rect SettingsGame::resetRect(int16_t W)  const { const int16_t hw=static_cast<int16_t>(W/2-12); return Rect{static_cast<int16_t>(W/2+4), 160, hw, 30}; }
Rect SettingsGame::brightRect(int16_t W, int16_t H) const { return Rect{8, static_cast<int16_t>(H-36), static_cast<int16_t>(W-16), 32}; }

/* Power tab: three full-width rows. */
Rect SettingsGame::idleActionRect(int16_t W) const { return Rect{8,  58, static_cast<int16_t>(W-16), 30}; }
Rect SettingsGame::idleAfterRect(int16_t W)  const { return Rect{8,  92, static_cast<int16_t>(W-16), 30}; }
Rect SettingsGame::sleepAfterRect(int16_t W) const { return Rect{8, 126, static_cast<int16_t>(W-16), 30}; }

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
    const int16_t W = static_cast<int16_t>(host.display().width());
    const int16_t H = static_cast<int16_t>(host.display().height());

    if (deviceTabRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (tab_ != Tab::Device) { tab_ = Tab::Device; confirmReset_ = false; markFullDirty(); }
        return;
    }
    if (powerTabRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (tab_ != Tab::Power) { tab_ = Tab::Power; confirmReset_ = false; markFullDirty(); }
        return;
    }

    if (!host.requireCapability(APP_CAP_DEVICE_SETTINGS, "change settings")) {
        return;
    }

    if (tab_ == Tab::Power) {
        if (idleActionRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            cycleIdleAction(board); markFullDirty(); return;
        }
        if (idleAfterRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            cycleScreenSaver(board); markFullDirty(); return;
        }
        if (sleepAfterRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (sleepRowActive(board)) { cycleSleepSeconds(board); markFullDirty(); }
            return;
        }
        return;
    }

    if (themeRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        const Board::ThemeMode next = board.themeMode() == Board::ThemeMode::Dark
            ? Board::ThemeMode::Light : Board::ThemeMode::Dark;
        board.setThemeMode(next);
        Ui::setTheme(next == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);
        markDirty(); return;
    }
    if (layoutRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setLayoutMode(board.layoutMode() == Board::LayoutMode::Horizontal
            ? Board::LayoutMode::Vertical : Board::LayoutMode::Horizontal);
        markDirty(); return;
    }
    if (ntpRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setRgbEnabled(!board.rgbEnabled()); markDirty(); return;
    }
    if (bleRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setBleBeaconEnabled(!board.bleBeaconEnabled());
        markDirty(); return;
    }
    if (nearbyRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (!board.bleBeaconEnabled()) {
            board.beepError();
            markDirty(); return;
        }
        NearbyPlay::setEnabled(board, !NearbyPlay::enabled());
        markDirty(); return;
    }
    if (resetRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (confirmReset_) {
            if (host.requireCapability(APP_CAP_FACTORY_RESET, "factory reset")) {
                board.factoryReset();
            }
        }
        confirmReset_ = true;
        markDirty(); return;
    }
    confirmReset_ = false;
    if (wifiRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.openWifi(); return;
    }
    if (brightRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setBrightness(Ui::sliderValueAt(brightRect(W, H), touch.x, Board::BRIGHTNESS_MIN));
        markDirty(); return;
    }
}

void SettingsGame::renderDeviceTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());

    char label[28];
    snprintf(label, sizeof(label), "Theme: %s",
             board.themeMode() == Board::ThemeMode::Dark ? "Dark" : "Light");
    Ui::drawButton(tft, themeRect(W), label,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    snprintf(label, sizeof(label), "Menu: %s",
             board.layoutMode() == Board::LayoutMode::Horizontal ? "Horizontal" : "Vertical");
    Ui::drawButton(tft, layoutRect(W), label,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    snprintf(label, sizeof(label), "Light: %s", board.rgbEnabled() ? "On" : "Off");
    Ui::drawButton(tft, ntpRect(W), label,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    snprintf(label, sizeof(label), "Beacon: %s", board.bleBeaconEnabled() ? "On" : "Off");
    Ui::drawButton(tft, bleRect(W), label,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, wifiRect(W), "Network", Ui::rgb(36, 132, 204), Ui::outline(), TFT_WHITE, false, 2);
    const bool beaconOn = board.bleBeaconEnabled();
    snprintf(label, sizeof(label), "Nearby: %s",
             !beaconOn ? "needs Beacon" : (NearbyPlay::enabled() ? "On" : "Off"));
    Ui::drawButton(tft, nearbyRect(W), label,
                   Ui::panel(), Ui::outline(), beaconOn ? Ui::text() : Ui::muted(), false, 2);
    Ui::drawButton(tft, resetRect(W),
                   confirmReset_ ? "Tap to ERASE" : "Reset device",
                   confirmReset_ ? Ui::rgb(220, 40, 40) : Ui::rgb(120, 58, 58),
                   Ui::outline(), TFT_WHITE, false, 2);

    const Rect br = brightRect(W, H);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(confirmReset_ ? "Erases scores, names, Wi-Fi and settings"
                             : "Brightness", 8, static_cast<int16_t>(br.y - 6), 1);
    tft.setTextDatum(TR_DATUM);
    snprintf(label, sizeof(label), "%u%%", board.brightness());
    tft.drawString(label, static_cast<int16_t>(W - 8), static_cast<int16_t>(br.y - 6), 1);
    tft.setTextDatum(TL_DATUM);
    Ui::drawSlider(tft, br, board.brightness(), Board::BRIGHTNESS_MIN);
}

void SettingsGame::renderPowerTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());

    const Board::IdleAction action = board.idleAction();
    const uint16_t idleSecs  = board.screenSaverSeconds();
    const uint16_t sleepSecs = board.sleepSeconds();

    const char* actionName =
        action == Board::IdleAction::SaverThenSleep ? "Saver then sleep" :
        action == Board::IdleAction::SleepOnly      ? "Sleep only"       : "Saver only";

    char buf[52];
    snprintf(buf, sizeof(buf), "When idle: %s", actionName);
    Ui::drawButton(tft, idleActionRect(W), buf,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);

    if (idleSecs >= 60 && idleSecs % 60 == 0) {
        snprintf(buf, sizeof(buf), "Idle after: %um", idleSecs / 60);
    } else {
        snprintf(buf, sizeof(buf), "Idle after: %us", idleSecs);
    }
    Ui::drawButton(tft, idleAfterRect(W), buf,
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);

    const bool sleepLive = sleepRowActive(board);
    if (!sleepLive) {
        snprintf(buf, sizeof(buf), "Sleep after: %s",
                 action == Board::IdleAction::SleepOnly ? "(at idle)" : "--");
    } else if (sleepSecs >= 60 && sleepSecs % 60 == 0) {
        snprintf(buf, sizeof(buf), "Sleep after: %um", sleepSecs / 60);
    } else {
        snprintf(buf, sizeof(buf), "Sleep after: %us", sleepSecs);
    }
    Ui::drawButton(tft, sleepAfterRect(W), buf,
                   sleepLive ? Ui::panel() : Ui::surface(), Ui::outline(),
                   sleepLive ? Ui::text() : Ui::muted(), false, 2);

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
    tft.drawString(explain, 8, 168, 1);
    tft.drawString("Sleep blanks the screen. A touch wakes it.", 8, 184, 1);
}

void SettingsGame::render(GameHost& host) {
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    const Rect devTab = deviceTabRect(W);
    const Rect powTab = powerTabRect(W);
    Ui::drawTab(tft, devTab, "Device", tab_ == Tab::Device);
    Ui::drawTab(tft, powTab, "Power", tab_ == Tab::Power);
    Ui::drawTabBaseline(tft, 52, 0, W,
                        tab_ == Tab::Device ? devTab : powTab);

    if (tab_ == Tab::Device) {
        renderDeviceTab(host);
    } else {
        renderPowerTab(host);
    }
    tft.setTextDatum(TL_DATUM);
}
