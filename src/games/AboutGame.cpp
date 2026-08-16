#include "AboutGame.h"
#include "AppVersion.h"
#include "engine/AppRegistry.h"
#include "hal/Board.h"
#include "hal/BleBeacon.h"

namespace {
/* The game list is paged straight out of the playable app registry. An earlier version kept
 * its own hand-written list and had quietly fallen six games behind, so nothing
 * here names a game directly.
 *
 * The radio page follows the same principle for the same reason: it reports
 * what the radios are actually doing rather than repeating a claim someone
 * typed once. A privacy statement that has drifted from the hardware is worse
 * than no statement at all, because it is believed. */
constexpr uint8_t GAMES_PER_PAGE = 7;
constexpr uint8_t GAME_PAGES =
    (playableAppCount() + GAMES_PER_PAGE - 1) / GAMES_PER_PAGE;
constexpr uint8_t PAGE_INTRO = 0;
constexpr uint8_t PAGE_FIRST_GAME = 1;
constexpr uint8_t PAGE_RADIOS = PAGE_FIRST_GAME + GAME_PAGES;
constexpr uint8_t PAGE_CREDITS = PAGE_RADIOS + 1;
constexpr uint8_t PAGE_COUNT = PAGE_CREDITS + 1;

constexpr int16_t PANEL_TOP = 38;
constexpr int16_t FOOTER_H = 44;
}

const char* AboutGame::title() const {
    return "About";
}

void AboutGame::begin(GameHost& host) {
    (void)host.requireCapability(APP_CAP_DEVICE_STATUS, "open about");
    page_ = 0;
    markFullDirty();
}

/* About is a system app, so it lays out against the live screen size rather
 * than SCREEN_WIDTH/SCREEN_HEIGHT and works in either orientation. */
Rect AboutGame::panelRect(int16_t w, int16_t h) const {
    return Rect{10, PANEL_TOP, static_cast<int16_t>(w - 20),
                static_cast<int16_t>(h - PANEL_TOP - FOOTER_H)};
}

Rect AboutGame::prevRect(int16_t w, int16_t h) const {
    (void)w;
    return Rect{12, static_cast<int16_t>(h - 34), 92, 28};
}

Rect AboutGame::nextRect(int16_t w, int16_t h) const {
    return Rect{static_cast<int16_t>(w - 104), static_cast<int16_t>(h - 34), 92, 28};
}

void AboutGame::drawLine(Ui::Renderer& tft, int16_t y, const String& text, uint8_t font) const {
    tft.drawString(Ui::fitted(tft, text, static_cast<int16_t>(tft.width() - 28), font),
                   14, y, font);
}

void AboutGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    const int16_t w = host.display().width();
    const int16_t h = host.display().height();
    if (prevRect(w, h).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ > 0) {
        --page_;
        markFullDirty();
        return;
    }
    if (nextRect(w, h).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ + 1 < PAGE_COUNT) {
        ++page_;
        markFullDirty();
    }
}

void AboutGame::renderIntro(Ui::Renderer& tft) {
    drawLine(tft, 48, "(C) GoodTime Micro Company", 2);
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, 72, String("Educational games for the ") + BOARD_NAME + ".", 1);
    drawLine(tft, 86, "Copyright 2026.", 1);
    tft.setTextColor(Ui::text(), Ui::surface());
    drawLine(tft, 108, String("Version ") + GOODTIME_KIDS_VERSION, 2);
    drawLine(tft, 134, String(playableAppCount()) + " games built in", 2);
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, 158, "195 flags and 50 US states,", 1);
    drawLine(tft, 172, "all stored on the device.", 1);
    drawLine(tft, 190, "Up to 5 children, plus a Guest.", 1);
}

void AboutGame::renderGames(Ui::Renderer& tft, int16_t w) {
    const uint8_t start = static_cast<uint8_t>((page_ - PAGE_FIRST_GAME) * GAMES_PER_PAGE);
    /* Blurbs start at a fraction of the width, not a fixed 118px, so the column
     * does not run off the edge of a 240px portrait screen. */
    const int16_t blurbX = max<int16_t>(100, static_cast<int16_t>((w * 37) / 100));
    const int16_t blurbMaxW = static_cast<int16_t>(w - blurbX - 16);

    for (uint8_t i = 0; i < GAMES_PER_PAGE; ++i) {
        const uint8_t idx = static_cast<uint8_t>(start + i);
        if (idx >= playableAppCount()) break;
        const int16_t y = static_cast<int16_t>(48 + i * 21);
        const AppDefinition& app = playableAppAt(idx);

        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(Ui::fitted(tft, app.title(),
                                  static_cast<int16_t>(blurbX - 20), 2), 14, y, 2);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(Ui::fitted(tft, app.blurb(), blurbMaxW, 1),
                       blurbX, static_cast<int16_t>(y + 4), 1);
    }
}

/* Everything on this page is read from the running system. Nothing here is a
 * claim that can quietly stop being true. */
void AboutGame::renderRadios(Ui::Renderer& tft, Board& board) {
    drawLine(tft, 48, "What the radios do", 2);

    const bool wifiCreds = board.hasWifiCredentials();
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, 74, "Wi-Fi: the clock only (NTP), plus", 1);
    drawLine(tft, 88, "a one-off time zone lookup.", 1);
    tft.setTextColor(wifiCreds ? Ui::text() : Ui::muted(), Ui::surface());
    drawLine(tft, 104, wifiCreds ? (board.isWifiConnected() ? "Now: connected"
                                                            : "Now: set up, not connected")
                                 : "Now: no network set up", 1);

    const BleBeacon::Advertisement& cfg = BleBeacon::configured();
    const bool onAir = BleBeacon::active();
    tft.setTextColor(Ui::text(), Ui::surface());
    drawLine(tft, 126, "Bluetooth beacon", 2);
    tft.setTextColor(onAir ? Ui::warning() : Ui::success(), Ui::surface());
    drawLine(tft, 150, onAir ? String("Now: broadcasting ") + cfg.deviceName
                             : String("Now: off, broadcasting nothing"), 1);

    /* Nearby play adds two fields to that beacon, so this page reads the
     * beacon's own flag rather than restating a list that was written before
     * the feature existed. A privacy claim that has drifted from the hardware
     * is worse than no claim, because it is believed. */
    tft.setTextColor(cfg.sharesActivity ? Ui::warning() : Ui::muted(), Ui::surface());
    drawLine(tft, 166, cfg.sharesActivity
                 ? "Nearby on: also the game open and"
                 : "Nearby off: no game, no score.", 1);
    if (cfg.sharesActivity) {
        drawLine(tft, 180, "its best score. No name, no profile.", 1);
    }
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, 194, cfg.sharesActivity
                 ? "Never sent: names, progress, location."
                 : "Never sent: names, scores, progress.", 1);
    tft.setTextColor(Ui::text(), Ui::surface());
    drawLine(tft, 212, "System Info > BLE shows the exact", 1);
    drawLine(tft, 226, "bytes being transmitted.", 1);
}

void AboutGame::renderCredits(Ui::Renderer& tft) {
    drawLine(tft, 48, "Artwork credits", 2);
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, 74, "Flags: lipis/flag-icons (MIT).", 1);
    drawLine(tft, 88, "State flags: fonttools/region-flags.", 1);
    drawLine(tft, 102, "State outlines: Natural Earth.", 1);
    drawLine(tft, 116, "Capitals: mledoze/countries (ODbL).", 1);
    tft.setTextColor(Ui::text(), Ui::surface());
    drawLine(tft, 142, "No accounts. No tracking.", 1);
    drawLine(tft, 156, "No data ever leaves the device.", 1);
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, 176, "Scores and progress live in this", 1);
    drawLine(tft, 190, "device's own memory.", 1);
}

void AboutGame::render(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t w = static_cast<int16_t>(tft.width());
    const int16_t h = static_cast<int16_t>(tft.height());
    const Rect panel = panelRect(w, h);

    Ui::clear(tft);
    Ui::drawTopBar(board, title());

    tft.fillRoundRect(panel.x, panel.y, panel.w, panel.h, 6, Ui::surface());
    tft.drawRoundRect(panel.x, panel.y, panel.w, panel.h, 6, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(TL_DATUM);

    if (page_ == PAGE_INTRO) {
        renderIntro(tft);
    } else if (page_ < PAGE_RADIOS) {
        renderGames(tft, w);
    } else if (page_ == PAGE_RADIOS) {
        renderRadios(tft, board);
    } else {
        renderCredits(tft);
    }

    Ui::drawPagerButton(tft, prevRect(w, h), "Prev", page_ > 0);
    Ui::drawPagerButton(tft, nextRect(w, h), "Next", page_ + 1 < PAGE_COUNT);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(page_ + 1) + "/" + PAGE_COUNT,
                   static_cast<int16_t>(w / 2), static_cast<int16_t>(h - 20), 2);
    tft.setTextDatum(TL_DATUM);
}
