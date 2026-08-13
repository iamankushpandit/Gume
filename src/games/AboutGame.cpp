#include "AboutGame.h"
#include "AppVersion.h"
#include "engine/GameCatalog.h"

namespace {
/* The game list is paged straight out of GAME_CATALOG. The previous version
 * kept its own hand-written list and had quietly fallen six games behind, so
 * nothing here names a game directly. */
constexpr uint8_t GAMES_PER_PAGE = 7;
constexpr uint8_t GAME_PAGES =
    (GAME_CATALOG_COUNT + GAMES_PER_PAGE - 1) / GAMES_PER_PAGE;
constexpr uint8_t PAGE_INTRO = 0;
constexpr uint8_t PAGE_FIRST_GAME = 1;
constexpr uint8_t PAGE_CREDITS = PAGE_FIRST_GAME + GAME_PAGES;
constexpr uint8_t PAGE_COUNT = PAGE_CREDITS + 1;
}

const char* AboutGame::title() const {
    return "About";
}

void AboutGame::begin(GameHost&) {
    page_ = 0;
    markDirty();
}

Rect AboutGame::prevRect() const {
    return Rect{12, 206, 92, 28};
}

Rect AboutGame::nextRect() const {
    return Rect{216, 206, 92, 28};
}

void AboutGame::drawLine(TFT_eSPI& tft, int16_t y, const String& text, uint8_t font) const {
    String fitted = text;
    while (fitted.length() > 2 && tft.textWidth(fitted, font) > SCREEN_WIDTH - 28) {
        fitted.remove(fitted.length() - 1);
    }
    tft.drawString(fitted, 14, y, font);
}

void AboutGame::update(GameHost&, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    if (prevRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ > 0) {
        --page_;
        markDirty();
        return;
    }
    if (nextRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ + 1 < PAGE_COUNT) {
        ++page_;
        markDirty();
    }
}

void AboutGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    tft.fillRoundRect(10, 38, 300, 158, 6, Ui::surface());
    tft.drawRoundRect(10, 38, 300, 158, 6, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(TL_DATUM);

    if (page_ == PAGE_INTRO) {
        drawLine(tft, 48, "(C) GoodTime Micro Company", 2);
        tft.setTextColor(Ui::muted(), Ui::surface());
        drawLine(tft, 72, "Educational games for the Cheap", 1);
        drawLine(tft, 86, "Yellow Display. Copyright 2026.", 1);
        tft.setTextColor(Ui::text(), Ui::surface());
        drawLine(tft, 108, String("Version ") + GOODTIME_KIDS_VERSION, 2);
        drawLine(tft, 134, String(GAME_CATALOG_COUNT) + " games built in", 2);
        tft.setTextColor(Ui::muted(), Ui::surface());
        drawLine(tft, 158, "195 flags and 50 US states,", 1);
        drawLine(tft, 172, "all stored on the device.", 1);

    } else if (page_ < PAGE_CREDITS) {
        const uint8_t start = static_cast<uint8_t>((page_ - PAGE_FIRST_GAME) * GAMES_PER_PAGE);
        for (uint8_t i = 0; i < GAMES_PER_PAGE; ++i) {
            const uint8_t idx = static_cast<uint8_t>(start + i);
            if (idx >= GAME_CATALOG_COUNT) break;
            const int16_t y = static_cast<int16_t>(48 + i * 21);

            tft.setTextColor(Ui::text(), Ui::surface());
            tft.drawString(GAME_CATALOG[idx].title, 14, y, 2);
            tft.setTextColor(Ui::muted(), Ui::surface());
            String blurb = GAME_CATALOG[idx].blurb;
            while (blurb.length() > 2 && tft.textWidth(blurb, 1) > 190) {
                blurb.remove(blurb.length() - 1);
            }
            tft.drawString(blurb, 118, static_cast<int16_t>(y + 4), 1);
        }

    } else {
        drawLine(tft, 48, "Artwork credits", 2);
        tft.setTextColor(Ui::muted(), Ui::surface());
        drawLine(tft, 74, "Flags: lipis/flag-icons (MIT).", 1);
        drawLine(tft, 90, "Capitals: mledoze/countries.", 1);
        drawLine(tft, 104, "US state data is public domain.", 1);
        tft.setTextColor(Ui::text(), Ui::surface());
        drawLine(tft, 162, "Wi-Fi is used only for the clock.", 1);
        drawLine(tft, 176, "No accounts, no tracking.", 1);
    }

    Ui::drawPagerButton(tft, prevRect(), "Prev", page_ > 0);
    Ui::drawPagerButton(tft, nextRect(), "Next", page_ + 1 < PAGE_COUNT);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(page_ + 1) + "/" + PAGE_COUNT, SCREEN_WIDTH / 2, 220, 2);
    tft.setTextDatum(TL_DATUM);
}
