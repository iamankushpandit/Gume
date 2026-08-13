#include "SettingsGame.h"


const char* SettingsGame::title() const { return "Settings"; }

void SettingsGame::begin(GameHost& host) {
    confirmReset_ = false;
    Ui::setTheme(host.board().themeMode() == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);
    markDirty();
}

Rect SettingsGame::themeRect()  const { return Rect{8,   68, 144, 36}; }
Rect SettingsGame::layoutRect() const { return Rect{164, 68, 144, 36}; }
Rect SettingsGame::saverRect()  const { return Rect{8,  110, 144, 36}; }
Rect SettingsGame::ntpRect()    const { return Rect{164, 110, 144, 36}; }
Rect SettingsGame::brightRect() const { return Rect{8,  200, 304, 32}; }
Rect SettingsGame::wifiRect()   const { return Rect{8,  152, 148, 36}; }
Rect SettingsGame::resetRect()  const { return Rect{164, 152, 144, 36}; }

void SettingsGame::cycleScreenSaver(Board& board) {
    const uint16_t current = board.screenSaverSeconds();
    const uint16_t next = current < 60 ? 60 : (current < 120 ? 120 : (current < 300 ? 300 : 30));
    board.setScreenSaverSeconds(next);
}

void SettingsGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;

    Board& board = host.board();

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
    if (saverRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        cycleScreenSaver(board); markDirty(); return;
    }
    if (ntpRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        board.setRgbEnabled(!board.rgbEnabled()); markDirty(); return;
    }
    if (resetRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (confirmReset_) {
            board.factoryReset();
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
    TFT_eSPI& tft = board.display();

    Ui::drawButton(tft, themeRect(),
        String("Theme: ") + (board.themeMode() == Board::ThemeMode::Dark ? "Dark" : "Light"),
        Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, layoutRect(),
        String("Menu: ") + (board.layoutMode() == Board::LayoutMode::Horizontal
                            ? "Horizontal" : "Vertical"),
        Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, saverRect(),
        String("Saver: ") + board.screenSaverSeconds() + "s",
        Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, ntpRect(),
        String("Light: ") + (board.rgbEnabled() ? "On" : "Off"),
        Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, wifiRect(), "Network", Ui::rgb(36, 132, 204), Ui::outline(), TFT_WHITE, false, 2);
    Ui::drawButton(tft, resetRect(),
                   confirmReset_ ? "Tap to ERASE" : "Reset device",
                   confirmReset_ ? Ui::rgb(220, 40, 40) : Ui::rgb(120, 58, 58),
                   Ui::outline(), TFT_WHITE, false, 2);

    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(confirmReset_ ? "Erases scores, names, Wi-Fi and settings"
                             : "Brightness", 8, 190, 1);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String(board.brightness()) + "%", SCREEN_WIDTH - 8, 190, 1);
    tft.setTextDatum(TL_DATUM);
    Ui::drawSlider(tft, brightRect(), board.brightness(), Board::BRIGHTNESS_MIN);
}

void SettingsGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(tft, title());
    renderDeviceTab(host);
    tft.setTextDatum(TL_DATUM);
}
