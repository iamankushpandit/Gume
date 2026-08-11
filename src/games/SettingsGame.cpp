#include "SettingsGame.h"


const char* SettingsGame::title() const { return "Settings"; }

void SettingsGame::begin(GameHost& host) {
    tab_ = Tab::Device;
    gameScroll_ = 0;
    Ui::setTheme(host.board().themeMode() == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);
    markDirty();
}

Rect SettingsGame::tabRect(Tab t) const {
    return t == Tab::Device ? Rect{4, 32, 150, 28} : Rect{162, 32, 150, 28};
}
Rect SettingsGame::themeRect()  const { return Rect{8,  70, 144, 40}; }
Rect SettingsGame::layoutRect() const { return Rect{164, 70, 144, 40}; }
Rect SettingsGame::saverRect()  const { return Rect{8, 120, 144, 40}; }
Rect SettingsGame::ntpRect()    const { return Rect{164, 120, 144, 40}; }
Rect SettingsGame::wifiRect()   const { return Rect{8, 170, 304, 40}; }
Rect SettingsGame::gameCheckRect(uint8_t row) const {
    // 29px pitch keeps all five rows clear of the Prev/Next buttons at y=210.
    return Rect{8, static_cast<int16_t>(62 + row * 29), 304, 27};
}

void SettingsGame::cycleScreenSaver(Board& board) {
    const uint16_t current = board.screenSaverSeconds();
    const uint16_t next = current < 60 ? 60 : (current < 120 ? 120 : (current < 300 ? 300 : 30));
    board.setScreenSaverSeconds(next);
}

void SettingsGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;

    Board& board = host.board();

    // Tab switching
    if (tabRect(Tab::Device).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        tab_ = Tab::Device; gameScroll_ = 0; markDirty(); return;
    }
    if (tabRect(Tab::Games).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        tab_ = Tab::Games; gameScroll_ = 0; markDirty(); return;
    }

    if (tab_ == Tab::Device) {
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
        if (wifiRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.openWifi(); return;
        }
    } else {
        constexpr uint8_t VISIBLE = 5;
        if (gamesPrevRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) && gameScroll_ > 0) {
            gameScroll_ = static_cast<uint8_t>(gameScroll_ >= VISIBLE ? gameScroll_ - VISIBLE : 0);
            markDirty(); return;
        }
        if (gamesNextRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) && gameScroll_ + VISIBLE < GAME_COUNT) {
            gameScroll_ = static_cast<uint8_t>(gameScroll_ + VISIBLE);
            markDirty(); return;
        }
        for (uint8_t row = 0; row < VISIBLE; ++row) {
            const uint8_t gi = gameScroll_ + row;
            if (gi >= GAME_COUNT) break;
            if (gameCheckRect(row).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                board.setGameVisible(GAME_CATALOG[gi].id, !board.gameVisible(GAME_CATALOG[gi].id));
                markDirty(); return;
            }
        }
    }
}

void SettingsGame::renderDeviceTab(GameHost& host) {
    Board& board = host.board();
    TFT_eSPI& tft = board.display();

    Ui::drawButton(tft, themeRect(),
        String("Theme: ") + (board.themeMode() == Board::ThemeMode::Dark ? "Dark" : "Light"),
        Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    // Games are all authored for the 320x240 landscape canvas, so this only
    // affects the launcher. Labelled explicitly so it does not look like a bug.
    Ui::drawButton(tft, layoutRect(),
        String("Menu: ") + (board.layoutMode() == Board::LayoutMode::Horizontal ? "Wide" : "Tall"),
        Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, saverRect(),
        String("Saver: ") + board.screenSaverSeconds() + "s",
        Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    // NTP moved to the Network & Time screen so all connectivity lives together;
    // this slot now toggles the case LED.
    Ui::drawButton(tft, ntpRect(),
        String("Light: ") + (board.rgbEnabled() ? "On" : "Off"),
        Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, wifiRect(), "Network & Time", Ui::rgb(36, 132, 204), Ui::outline(), TFT_WHITE, false, 2);

    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Menu layout applies to the home screen only", 8, 222, 1);
}

void SettingsGame::renderGamesTab(GameHost& host) {
    Board& board = host.board();
    TFT_eSPI& tft = board.display();
    constexpr uint8_t VISIBLE = 5;

    for (uint8_t row = 0; row < VISIBLE; ++row) {
        const uint8_t gi = gameScroll_ + row;
        if (gi >= GAME_COUNT) break;
        const Rect r = gameCheckRect(row);
        const bool on = board.gameVisible(GAME_CATALOG[gi].id);
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, Ui::surface());
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
        tft.fillRoundRect(r.x + 4, r.y + 6, 16, 16, 3, on ? Ui::success() : Ui::panel());
        tft.drawRoundRect(r.x + 4, r.y + 6, 16, 16, 3, Ui::outline());
        if (on) {
            tft.setTextColor(TFT_WHITE, Ui::success());
            tft.setTextDatum(MC_DATUM);
            tft.drawString("v", r.x + 12, r.y + 14, 1);
        }
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(ML_DATUM);
        tft.drawString(GAME_CATALOG[gi].label, r.x + 28, r.y + r.h / 2, 2);
    }
    // Real buttons rather than the old font-1 text links, which were nearly
    // impossible to hit and easy to miss entirely.
    const bool canPrev = gameScroll_ > 0;
    const bool canNext = gameScroll_ + VISIBLE < GAME_COUNT;
    Ui::drawButton(tft, gamesPrevRect(), "Prev", canPrev ? Ui::panel() : Ui::surface(),
                   Ui::outline(), canPrev ? Ui::text() : Ui::muted(), false, 2);
    Ui::drawButton(tft, gamesNextRect(), "Next", canNext ? Ui::panel() : Ui::surface(),
                   Ui::outline(), canNext ? Ui::text() : Ui::muted(), false, 2);

    const uint8_t page  = static_cast<uint8_t>(gameScroll_ / VISIBLE + 1);
    const uint8_t pages = static_cast<uint8_t>((GAME_COUNT + VISIBLE - 1) / VISIBLE);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(page) + "/" + pages, SCREEN_WIDTH / 2, 222, 2);
    tft.setTextDatum(TL_DATUM);
}

Rect SettingsGame::gamesPrevRect() const { return Rect{8, 210, 92, 25}; }
Rect SettingsGame::gamesNextRect() const { return Rect{212, 210, 92, 25}; }

void SettingsGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(tft, title());

    const uint16_t devFill  = tab_ == Tab::Device ? Ui::rgb(36, 132, 204) : Ui::panel();
    const uint16_t gameFill = tab_ == Tab::Games  ? Ui::rgb(36, 132, 204) : Ui::panel();
    const uint16_t devTc    = tab_ == Tab::Device ? TFT_WHITE : Ui::text();
    const uint16_t gameTc   = tab_ == Tab::Games  ? TFT_WHITE : Ui::text();
    Ui::drawButton(tft, tabRect(Tab::Device), "Device / Wi-Fi", devFill,  Ui::outline(), devTc,  false, 2);
    Ui::drawButton(tft, tabRect(Tab::Games),  "Games",          gameFill, Ui::outline(), gameTc, false, 2);

    if (tab_ == Tab::Device) {
        renderDeviceTab(host);
    } else {
        renderGamesTab(host);
    }
    tft.setTextDatum(TL_DATUM);
}