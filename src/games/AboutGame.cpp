#include "AboutGame.h"

namespace {
constexpr uint8_t PAGE_COUNT = 5;
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
    Ui::drawTopBar(tft, title());

    tft.fillRoundRect(10, 38, 300, 158, 6, Ui::surface());
    tft.drawRoundRect(10, 38, 300, 158, 6, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(TL_DATUM);

    switch (page_) {
        case 0:
            drawLine(tft, 50, "(C) GoodTime Micro Company™", 2);
            drawLine(tft, 80, "Kids educational games for CYD.");
            drawLine(tft, 106, "Copyright 2026.");
            drawLine(tft, 132, "Trademark owned by the");
            drawLine(tft, 154, "company.");
            break;
        case 1:
            drawLine(tft, 50, "Tic-Tac-Toe: two players.");
            drawLine(tft, 72, "Memory: match card pairs.");
            drawLine(tft, 94, "Math: add and subtract.");
            drawLine(tft, 116, "Multiply: times tables 1-12.");
            drawLine(tft, 138, "Time: read analog clocks.");
            drawLine(tft, 160, "Simon: repeat color sequences.");
            break;
        case 2:
            drawLine(tft, 50, "Sudoku: 2x2, 4x4, then 6x6.");
            drawLine(tft, 72, "Shapes: match colors and shapes.");
            drawLine(tft, 94, "Counting: count dots and tap.");
            drawLine(tft, 116, "Money: count and compare coins.");
            drawLine(tft, 138, "Fractions: compare pie charts.");
            drawLine(tft, 160, "Maze: 30 solvable mazes.");
            break;
        case 3:
            drawLine(tft, 50, "Sorting: order number tiles.");
            drawLine(tft, 72, "Color Mix: pick mixed colors.");
            drawLine(tft, 94, "Slide: solvable number puzzle.");
            drawLine(tft, 116, "Odd One: find the different item.");
            drawLine(tft, 138, "Whack A Mole: tap smiley faces.");
            drawLine(tft, 170, "Scores save on the device.");
            break;
        default:
            drawLine(tft, 50, "Fraction Circles starts simple:");
            drawLine(tft, 72, "halves and quarters first.");
            drawLine(tft, 98, "Then thirds, eighths, and");
            drawLine(tft, 120, "unlike-fraction comparisons.");
            drawLine(tft, 152, "Built for 320x240 touch screens.");
            break;
    }

    Ui::drawButton(tft, prevRect(), "Prev", page_ > 0 ? Ui::panel() : Ui::surface(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, nextRect(), "Next", page_ + 1 < PAGE_COUNT ? Ui::panel() : Ui::surface(), Ui::outline(), Ui::text(), false, 2);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(page_ + 1) + "/" + PAGE_COUNT, SCREEN_WIDTH / 2, 220, 2);
    tft.setTextDatum(TL_DATUM);
}
