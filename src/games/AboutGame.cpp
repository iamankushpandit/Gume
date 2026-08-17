#include "AboutGame.h"
#include "AppVersion.h"
#include "engine/AppRegistry.h"
#include "hal/Board.h"
#include "hal/BleBeacon.h"
#include "ui/GameLayout.h"

namespace {
/* The game list is paged straight out of the playable app registry. An earlier version kept
 * its own hand-written list and had quietly fallen six games behind, so nothing
 * here names a game directly.
 *
 * The radio page follows the same principle for the same reason: it reports
 * what the radios are actually doing rather than repeating a claim someone
 * typed once. A privacy statement that has drifted from the hardware is worse
 * than no statement at all, because it is believed. */
constexpr uint8_t PAGE_INTRO = 0;
constexpr uint8_t PAGE_FIRST_GAME = 1;

constexpr int16_t PANEL_TOP = 38;
constexpr int16_t FOOTER_H = 44;
constexpr int16_t PANEL_PAD = 10;   // inside the panel, top and bottom
constexpr int16_t GAME_ROW_H = 21;
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
Rect AboutGame::panelRect(const Ui::Frame& f) const {
    return Rect{10, PANEL_TOP, static_cast<int16_t>(f.w - 20),
                static_cast<int16_t>(f.h - PANEL_TOP - FOOTER_H)};
}

Rect AboutGame::prevRect(const Ui::Frame& f) const {
    return Rect{12, static_cast<int16_t>(f.h - 34), 92, 28};
}

Rect AboutGame::nextRect(const Ui::Frame& f) const {
    return Rect{static_cast<int16_t>(f.w - 104), static_cast<int16_t>(f.h - 34), 92, 28};
}

/* A font-2 line is half again as tall as a font-1 one, so the panel is shared
 * out in half-units and each line takes the share its font needs. The unit is
 * capped so a tall panel spaces the text out rather than sprawling, and
 * floored so a short one stays legible rather than shrinking to nothing. */
AboutGame::Flow::Flow(const Rect& panel, uint8_t smallLines, uint8_t largeLines) {
    const int16_t room = static_cast<int16_t>(panel.h - 2 * PANEL_PAD);
    const int16_t halves = static_cast<int16_t>(2 * smallLines + 3 * largeLines);
    int16_t unit = halves > 0 ? static_cast<int16_t>((2 * room) / halves) : 14;
    if (unit > 16) unit = 16;
    if (unit < 11) unit = 11;
    unit_ = unit;
    y_ = static_cast<int16_t>(panel.y + PANEL_PAD);
}

int16_t AboutGame::Flow::next(uint8_t font) {
    const int16_t at = y_;
    y_ = static_cast<int16_t>(y_ + (font >= 2 ? (unit_ * 3) / 2 : unit_));
    return at;
}

/* Both the rows on a game page and the number of game pages follow the panel,
 * so the 4-inch board lists ten games where the 2.8-inch one lists six. */
uint8_t AboutGame::gamesPerPage(const Rect& panel) const {
    const int16_t rows = static_cast<int16_t>((panel.h - 2 * PANEL_PAD) / GAME_ROW_H);
    if (rows < 1) return 1;
    if (rows > playableAppCount()) return static_cast<uint8_t>(playableAppCount());
    return static_cast<uint8_t>(rows);
}

uint8_t AboutGame::gamePageCount(const Rect& panel) const {
    const uint8_t per = gamesPerPage(panel);
    return static_cast<uint8_t>((playableAppCount() + per - 1) / per);
}

/* intro + game pages + radios + credits */
uint8_t AboutGame::pageCount(const Rect& panel) const {
    return static_cast<uint8_t>(PAGE_FIRST_GAME + gamePageCount(panel) + 2);
}

void AboutGame::drawLine(Ui::Renderer& tft, int16_t y, const String& text, uint8_t font) const {
    tft.drawString(Ui::fitted(tft, text, static_cast<int16_t>(tft.width() - 28), font),
                   14, y, font);
}

void AboutGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    const Ui::Frame f = Ui::frame(host.display());
    if (prevRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ > 0) {
        --page_;
        markFullDirty();
        return;
    }
    if (nextRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP) &&
        page_ + 1 < pageCount(panelRect(f))) {
        ++page_;
        markFullDirty();
    }
}

/* The product name and the copyright are two separate facts here, and the
 * intro page is the one screen an owner reads to learn either. Braino! is the
 * console; GoodTime Micro Company still owns it. Both come from AppVersion.h --
 * this page must not be where either gets re-typed. */
void AboutGame::renderIntro(Ui::Renderer& tft, const Rect& panel) {
    Flow flow(panel, 6, 3);
    drawLine(tft, flow.next(2), BRAINO_PRODUCT_NAME, 2);
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, flow.next(1), BRAINO_COPYRIGHT, 1);
    drawLine(tft, flow.next(1), String("Educational games for the ") + BOARD_NAME + ".", 1);
    drawLine(tft, flow.next(1), "Copyright 2026.", 1);
    tft.setTextColor(Ui::text(), Ui::surface());
    drawLine(tft, flow.next(2), String("Version ") + BRAINO_VERSION, 2);
    drawLine(tft, flow.next(2), String(playableAppCount()) + " games built in", 2);
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, flow.next(1), "195 flags and 50 US states,", 1);
    drawLine(tft, flow.next(1), "all stored on the device.", 1);
    drawLine(tft, flow.next(1), "Up to 5 children, plus a Guest.", 1);
}

void AboutGame::renderGames(Ui::Renderer& tft, const Rect& panel) {
    const uint8_t perPage = gamesPerPage(panel);
    const uint8_t start = static_cast<uint8_t>((page_ - PAGE_FIRST_GAME) * perPage);
    const int16_t w = static_cast<int16_t>(panel.x + panel.w + 10);
    /* Blurbs start at a fraction of the width, not a fixed 118px, so the column
     * does not run off the edge of a 240px portrait screen. */
    const int16_t blurbX = max<int16_t>(100, static_cast<int16_t>((w * 37) / 100));
    const int16_t blurbMaxW = static_cast<int16_t>(w - blurbX - 16);

    for (uint8_t i = 0; i < perPage; ++i) {
        const uint8_t idx = static_cast<uint8_t>(start + i);
        if (idx >= playableAppCount()) break;
        const int16_t y = static_cast<int16_t>(panel.y + PANEL_PAD + i * GAME_ROW_H);
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
void AboutGame::renderRadios(Ui::Renderer& tft, const Rect& panel, Board& board) {
    const BleBeacon::Advertisement& cfg = BleBeacon::configured();
    /* One line is conditional, so the flow is told the count it will actually
     * be asked for -- otherwise the page would space itself for a line that
     * never gets drawn. */
    Flow flow(panel, static_cast<uint8_t>(cfg.sharesActivity ? 9 : 8), 2);
    drawLine(tft, flow.next(2), "What the radios do", 2);

    const bool wifiCreds = board.hasWifiCredentials();
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, flow.next(1), "Wi-Fi: the clock only (NTP), plus", 1);
    drawLine(tft, flow.next(1), "a one-off time zone lookup.", 1);
    tft.setTextColor(wifiCreds ? Ui::text() : Ui::muted(), Ui::surface());
    drawLine(tft, flow.next(1), wifiCreds ? (board.isWifiConnected() ? "Now: connected"
                                                            : "Now: set up, not connected")
                                 : "Now: no network set up", 1);

    const bool onAir = BleBeacon::active();
    tft.setTextColor(Ui::text(), Ui::surface());
    drawLine(tft, flow.next(2), "Bluetooth beacon", 2);
    tft.setTextColor(onAir ? Ui::warning() : Ui::success(), Ui::surface());
    drawLine(tft, flow.next(1), onAir ? String("Now: broadcasting ") + cfg.deviceName
                             : String("Now: off, broadcasting nothing"), 1);

    /* Nearby play adds two fields to that beacon, so this page reads the
     * beacon's own flag rather than restating a list that was written before
     * the feature existed. A privacy claim that has drifted from the hardware
     * is worse than no claim, because it is believed. */
    tft.setTextColor(cfg.sharesActivity ? Ui::warning() : Ui::muted(), Ui::surface());
    drawLine(tft, flow.next(1), cfg.sharesActivity
                 ? "Nearby on: also the game open and"
                 : "Nearby off: no game, no score.", 1);
    if (cfg.sharesActivity) {
        drawLine(tft, flow.next(1), "its best score. No name, no profile.", 1);
    }
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, flow.next(1), cfg.sharesActivity
                 ? "Never sent: names, progress, location."
                 : "Never sent: names, scores, progress.", 1);
    tft.setTextColor(Ui::text(), Ui::surface());
    drawLine(tft, flow.next(1), "System Info > BLE shows the exact", 1);
    drawLine(tft, flow.next(1), "bytes being transmitted.", 1);
}

void AboutGame::renderCredits(Ui::Renderer& tft, const Rect& panel) {
    Flow flow(panel, 8, 1);
    drawLine(tft, flow.next(2), "Artwork credits", 2);
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, flow.next(1), "Flags: lipis/flag-icons (MIT).", 1);
    drawLine(tft, flow.next(1), "State flags: fonttools/region-flags.", 1);
    drawLine(tft, flow.next(1), "State outlines: Natural Earth.", 1);
    drawLine(tft, flow.next(1), "Capitals: mledoze/countries (ODbL).", 1);
    tft.setTextColor(Ui::text(), Ui::surface());
    drawLine(tft, flow.next(1), "No accounts. No tracking.", 1);
    drawLine(tft, flow.next(1), "No data ever leaves the device.", 1);
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, flow.next(1), "Scores and progress live in this", 1);
    drawLine(tft, flow.next(1), "device's own memory.", 1);
}

void AboutGame::render(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    const Rect panel = panelRect(f);
    const uint8_t pages = pageCount(panel);
    const uint8_t firstRadios = static_cast<uint8_t>(PAGE_FIRST_GAME + gamePageCount(panel));
    /* Games per page follow the panel, so a page that existed on a taller
     * screen can vanish on a shorter one. Clamp rather than draw a blank. */
    if (page_ >= pages) {
        page_ = static_cast<uint8_t>(pages - 1);
    }

    Ui::clear(tft);
    Ui::drawTopBar(board, title());

    tft.fillRoundRect(panel.x, panel.y, panel.w, panel.h, 6, Ui::surface());
    tft.drawRoundRect(panel.x, panel.y, panel.w, panel.h, 6, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(TL_DATUM);

    if (page_ == PAGE_INTRO) {
        renderIntro(tft, panel);
    } else if (page_ < firstRadios) {
        renderGames(tft, panel);
    } else if (page_ == firstRadios) {
        renderRadios(tft, panel, board);
    } else {
        renderCredits(tft, panel);
    }

    Ui::drawPagerButton(tft, prevRect(f), "Prev", page_ > 0);
    Ui::drawPagerButton(tft, nextRect(f), "Next", page_ + 1 < pages);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(page_ + 1) + "/" + pages,
                   static_cast<int16_t>(f.w / 2), static_cast<int16_t>(f.h - 20), 2);
    tft.setTextDatum(TL_DATUM);
}
