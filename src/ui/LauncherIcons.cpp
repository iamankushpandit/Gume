#include "ui/LauncherIcons.h"

namespace {

uint16_t paper() { return Ui::rgb(248, 252, 255); }
uint16_t ink() { return Ui::rgb(34, 69, 116); }
uint16_t inkSoft() { return Ui::rgb(88, 123, 166); }
uint16_t sky() { return Ui::rgb(115, 204, 255); }
uint16_t mint() { return Ui::rgb(106, 224, 159); }
uint16_t sun() { return Ui::rgb(255, 213, 84); }
uint16_t coral() { return Ui::rgb(247, 112, 110); }
uint16_t coinGold() { return Ui::rgb(246, 201, 64); }
uint16_t skin() { return Ui::rgb(255, 222, 184); }

void drawPlate(Ui::Renderer& tft, int16_t cx, int16_t cy, uint16_t fill) {
    tft.fillCircle(cx + 1, cy + 2, 21, Ui::shade(fill, 70));
    tft.fillCircle(cx, cy, 20, paper());
    tft.drawCircle(cx, cy, 20, Ui::rgb(214, 232, 248));
    tft.fillCircle(cx - 7, cy - 10, 4, TFT_WHITE);
}

void drawText(Ui::Renderer& tft, const char* text, int16_t x, int16_t y,
              uint8_t font, uint16_t fg, uint16_t bg) {
    tft.setTextColor(fg, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, x, y, font);
}

void thickLine(Ui::Renderer& tft, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
               uint16_t color) {
    tft.drawLine(x0, y0, x1, y1, color);
    if (abs(x1 - x0) >= abs(y1 - y0)) {
        tft.drawLine(x0, y0 + 1, x1, y1 + 1, color);
    } else {
        tft.drawLine(x0 + 1, y0, x1 + 1, y1, color);
    }
}

void sparkle(Ui::Renderer& tft, int16_t x, int16_t y, uint16_t color) {
    tft.drawFastVLine(x, y - 3, 7, color);
    tft.drawFastHLine(x - 3, y, 7, color);
    tft.drawPixel(x - 2, y - 2, color);
    tft.drawPixel(x + 2, y - 2, color);
    tft.drawPixel(x - 2, y + 2, color);
    tft.drawPixel(x + 2, y + 2, color);
}

void smallCard(Ui::Renderer& tft, int16_t x, int16_t y, int16_t w, int16_t h,
               uint16_t fill, uint16_t outline) {
    tft.fillRoundRect(x + 1, y + 2, w, h, 3, Ui::rgb(176, 190, 210));
    tft.fillRoundRect(x, y, w, h, 3, fill);
    tft.drawRoundRect(x, y, w, h, 3, outline);
}

void drawDieFace(Ui::Renderer& tft, int16_t x, int16_t y, int16_t size,
                 uint16_t body, uint16_t outline, uint16_t mask) {
    tft.fillRoundRect(x + 1, y + 2, size, size, 4, Ui::rgb(176, 190, 210));
    tft.fillRoundRect(x, y, size, size, 4, body);
    tft.drawRoundRect(x, y, size, size, 4, outline);
    const int16_t step = static_cast<int16_t>(size / 4);
    const int16_t pipR = size > 19 ? 2 : 1;
    for (uint8_t cell = 0; cell < 9; ++cell) {
        if ((mask & (1u << (8 - cell))) == 0) {
            continue;
        }
        tft.fillCircle(static_cast<int16_t>(x + step + (cell % 3) * step),
                       static_cast<int16_t>(y + step + (cell / 3) * step),
                       pipR, ink());
    }
}

void drawFlagShape(Ui::Renderer& tft, int16_t cx, int16_t cy,
                   uint16_t mainColor, uint16_t accentColor) {
    thickLine(tft, cx - 13, cy - 14, cx - 13, cy + 15, ink());
    tft.fillTriangle(cx - 12, cy - 13, cx + 13, cy - 9, cx - 12, cy - 4, mainColor);
    tft.fillTriangle(cx - 12, cy - 4, cx + 13, cy - 9, cx + 12, cy, accentColor);
    tft.drawTriangle(cx - 12, cy - 13, cx + 13, cy - 9, cx - 12, cy - 4, ink());
    tft.drawTriangle(cx - 12, cy - 4, cx + 13, cy - 9, cx + 12, cy, ink());
}

void drawMapSilhouette(Ui::Renderer& tft, int16_t cx, int16_t cy, uint16_t color) {
    tft.fillTriangle(cx - 13, cy - 7, cx - 2, cy - 13, cx + 12, cy - 6, color);
    tft.fillTriangle(cx - 13, cy - 7, cx + 12, cy - 6, cx + 6, cy + 10, color);
    tft.fillTriangle(cx - 13, cy - 7, cx + 6, cy + 10, cx - 8, cy + 8, color);
    tft.drawLine(cx - 13, cy - 7, cx - 2, cy - 13, ink());
    tft.drawLine(cx - 2, cy - 13, cx + 12, cy - 6, ink());
    tft.drawLine(cx + 12, cy - 6, cx + 6, cy + 10, ink());
    tft.drawLine(cx + 6, cy + 10, cx - 8, cy + 8, ink());
    tft.drawLine(cx - 8, cy + 8, cx - 13, cy - 7, ink());
}

void drawPercentIcon(Ui::Renderer& tft, int16_t cx, int16_t cy, uint16_t fill) {
    tft.fillCircle(cx + 1, cy + 2, 21, Ui::shade(fill, 70));
    tft.fillCircle(cx, cy, 20, paper());
    tft.fillTriangle(cx, cy, cx, cy - 20, cx + 20, cy - 20, sun());
    tft.fillTriangle(cx, cy, cx + 20, cy - 20, cx + 20, cy, sun());
    tft.fillTriangle(cx, cy, cx + 20, cy, cx + 11, cy + 17, mint());
    tft.fillCircle(cx, cy, 9, paper());
    tft.drawCircle(cx, cy, 9, inkSoft());
    tft.drawCircle(cx, cy, 20, ink());
    tft.fillCircle(cx - 6, cy - 6, 3, ink());
    tft.fillCircle(cx + 7, cy + 7, 3, ink());
    thickLine(tft, cx - 9, cy + 10, cx + 10, cy - 9, ink());
}

void drawFingerIcon(Ui::Renderer& tft, int16_t cx, int16_t cy, uint16_t fill) {
    drawPlate(tft, cx, cy, fill);
    const uint16_t shadeSkin = Ui::rgb(245, 190, 146);
    for (uint8_t i = 0; i < 4; ++i) {
        const int16_t x = static_cast<int16_t>(cx - 12 + i * 6);
        const int16_t h = static_cast<int16_t>(17 + (i == 1 ? 3 : 0));
        tft.fillRoundRect(x, cy - 15, 5, h, 3, skin());
        tft.drawFastVLine(static_cast<int16_t>(x + 4), cy - 11,
                          static_cast<int16_t>(h - 5), shadeSkin);
    }
    tft.fillRoundRect(cx - 13, cy - 2, 25, 16, 5, skin());
    tft.fillRoundRect(cx + 7, cy + 1, 12, 7, 4, skin());
    tft.drawLine(cx + 9, cy + 7, cx + 17, cy + 4, shadeSkin);
    tft.fillRoundRect(cx - 10, cy + 10, 24, 6, 3, Ui::rgb(93, 151, 223));
    tft.drawFastHLine(cx - 8, cy + 3, 13, shadeSkin);
    tft.drawFastHLine(cx - 7, cy + 8, 11, shadeSkin);
}

}  // namespace

void drawLauncherIcon(Ui::Renderer& tft, LauncherIcon icon, const Rect& r,
                      uint16_t fill, int16_t cx, int16_t cy) {
    (void)r;
    switch (icon) {
        case LauncherIcon::TicTacToe:
            drawPlate(tft, cx, cy, fill);
            tft.drawFastVLine(cx - 5, cy - 13, 26, inkSoft());
            tft.drawFastVLine(cx + 5, cy - 13, 26, inkSoft());
            tft.drawFastHLine(cx - 13, cy - 4, 26, inkSoft());
            tft.drawFastHLine(cx - 13, cy + 5, 26, inkSoft());
            thickLine(tft, cx - 12, cy - 11, cx - 7, cy - 6, coral());
            thickLine(tft, cx - 7, cy - 11, cx - 12, cy - 6, coral());
            tft.drawCircle(cx + 9, cy + 9, 4, mint());
            tft.drawCircle(cx + 9, cy + 9, 5, mint());
            break;
        case LauncherIcon::Memory:
            smallCard(tft, cx - 15, cy - 12, 20, 25, Ui::rgb(232, 238, 250), inkSoft());
            smallCard(tft, cx - 4, cy - 15, 20, 25, paper(), ink());
            sparkle(tft, cx + 6, cy - 5, sun());
            tft.fillCircle(cx + 6, cy + 7, 3, sky());
            break;
        case LauncherIcon::Math:
            drawPlate(tft, cx, cy, fill);
            thickLine(tft, cx - 11, cy - 6, cx - 3, cy - 6, coral());
            thickLine(tft, cx - 7, cy - 10, cx - 7, cy - 2, coral());
            thickLine(tft, cx + 3, cy + 5, cx + 12, cy + 5, ink());
            thickLine(tft, cx + 3, cy + 9, cx + 12, cy + 9, ink());
            drawText(tft, "3", cx + 6, cy - 7, 1, ink(), paper());
            break;
        case LauncherIcon::Multiplication:
            drawPlate(tft, cx, cy, fill);
            for (uint8_t row = 0; row < 3; ++row) {
                for (uint8_t col = 0; col < 4; ++col) {
                    tft.fillCircle(static_cast<int16_t>(cx - 10 + col * 7),
                                   static_cast<int16_t>(cy - 8 + row * 7), 2,
                                   (row + col) % 2 == 0 ? sky() : sun());
                }
            }
            thickLine(tft, cx - 11, cy + 12, cx + 11, cy + 12, ink());
            break;
        case LauncherIcon::Time:
            drawPlate(tft, cx, cy, fill);
            tft.drawCircle(cx, cy, 12, ink());
            tft.drawCircle(cx, cy, 13, inkSoft());
            tft.drawFastVLine(cx, cy - 12, 3, ink());
            tft.drawFastVLine(cx, cy + 10, 3, ink());
            tft.drawFastHLine(cx - 12, cy, 3, ink());
            tft.drawFastHLine(cx + 10, cy, 3, ink());
            thickLine(tft, cx, cy, cx, cy - 8, ink());
            thickLine(tft, cx, cy, cx + 8, cy + 5, coral());
            tft.fillCircle(cx, cy, 2, ink());
            break;
        case LauncherIcon::WhackAMole:
            drawPlate(tft, cx, cy, fill);
            tft.fillRoundRect(cx - 14, cy + 7, 28, 7, 4, inkSoft());
            tft.fillCircle(cx, cy, 10, Ui::rgb(151, 95, 56));
            tft.fillCircle(cx - 4, cy - 2, 1, TFT_BLACK);
            tft.fillCircle(cx + 4, cy - 2, 1, TFT_BLACK);
            tft.fillCircle(cx, cy + 3, 2, Ui::rgb(255, 195, 180));
            tft.fillRoundRect(cx + 9, cy - 15, 4, 19, 2, sun());
            tft.fillRoundRect(cx + 5, cy - 17, 12, 5, 2, sun());
            tft.drawRoundRect(cx + 5, cy - 17, 12, 5, 2, ink());
            break;
        case LauncherIcon::Cinnamon:
            drawPlate(tft, cx, cy, fill);
            tft.fillCircle(cx - 8, cy - 8, 7, coral());
            tft.fillCircle(cx + 8, cy - 8, 7, sky());
            tft.fillCircle(cx - 8, cy + 8, 7, mint());
            tft.fillCircle(cx + 8, cy + 8, 7, sun());
            tft.drawLine(cx - 8, cy - 8, cx + 8, cy - 8, ink());
            tft.drawLine(cx + 8, cy - 8, cx + 8, cy + 8, ink());
            break;
        case LauncherIcon::Microku:
            smallCard(tft, cx - 16, cy - 16, 32, 32, paper(), ink());
            for (uint8_t i = 1; i < 4; ++i) {
                const int16_t x = static_cast<int16_t>(cx - 16 + i * 8);
                const int16_t y = static_cast<int16_t>(cy - 16 + i * 8);
                const uint16_t c = i == 2 ? ink() : inkSoft();
                tft.drawFastVLine(x, cy - 16, 32, c);
                tft.drawFastHLine(cx - 16, y, 32, c);
            }
            drawText(tft, "1", cx - 12, cy - 12, 1, ink(), paper());
            drawText(tft, "4", cx + 12, cy + 12, 1, ink(), paper());
            break;
        case LauncherIcon::ShapeColor:
            drawPlate(tft, cx, cy, fill);
            tft.fillCircle(cx - 8, cy - 3, 7, sky());
            Ui::drawTriangleShape(tft, cx + 8, cy + 6, 8, sun(), true);
            tft.fillRoundRect(cx + 2, cy - 13, 11, 11, 2, coral());
            tft.drawCircle(cx - 8, cy - 3, 7, ink());
            break;
        case LauncherIcon::Counting:
            drawPlate(tft, cx, cy, fill);
            for (uint8_t i = 0; i < 5; ++i) {
                tft.fillCircle(static_cast<int16_t>(cx - 12 + i * 6),
                               static_cast<int16_t>(cy - 5 + (i % 2) * 7), 3,
                               i < 3 ? sky() : sun());
            }
            drawText(tft, "5", cx, cy + 10, 2, ink(), paper());
            break;
        case LauncherIcon::Money:
            drawPlate(tft, cx, cy, fill);
            tft.fillCircle(cx - 6, cy - 1, 10, Ui::rgb(178, 190, 198));
            tft.fillCircle(cx + 7, cy + 4, 11, coinGold());
            tft.drawCircle(cx - 6, cy - 1, 10, inkSoft());
            tft.drawCircle(cx + 7, cy + 4, 11, ink());
            drawText(tft, "1", cx - 6, cy - 1, 1, ink(), Ui::rgb(178, 190, 198));
            drawText(tft, "5", cx + 7, cy + 4, 1, ink(), coinGold());
            break;
        case LauncherIcon::Fractions:
            drawPlate(tft, cx, cy, fill);
            tft.fillCircle(cx, cy, 14, paper());
            tft.fillTriangle(cx, cy, cx, cy - 14, cx + 14, cy, sun());
            tft.fillTriangle(cx, cy, cx + 14, cy, cx, cy + 14, sky());
            tft.drawCircle(cx, cy, 14, ink());
            tft.drawFastVLine(cx, cy - 14, 28, inkSoft());
            tft.drawFastHLine(cx - 14, cy, 28, inkSoft());
            break;
        case LauncherIcon::Maze:
            drawPlate(tft, cx, cy, fill);
            tft.drawRoundRect(cx - 14, cy - 13, 28, 26, 2, ink());
            thickLine(tft, cx - 8, cy - 13, cx - 8, cy + 6, ink());
            thickLine(tft, cx - 2, cy - 4, cx + 10, cy - 4, ink());
            thickLine(tft, cx + 5, cy - 13, cx + 5, cy - 7, ink());
            thickLine(tft, cx - 12, cy + 8, cx + 8, cy + 8, mint());
            tft.fillCircle(cx - 11, cy + 8, 3, sun());
            tft.fillCircle(cx + 11, cy - 10, 3, coral());
            break;
        case LauncherIcon::Sort:
            drawPlate(tft, cx, cy, fill);
            for (uint8_t i = 0; i < 3; ++i) {
                const int16_t h = static_cast<int16_t>(8 + i * 5);
                const int16_t x = static_cast<int16_t>(cx - 12 + i * 10);
                tft.fillRoundRect(x, static_cast<int16_t>(cy + 12 - h), 7, h, 2,
                                  i == 0 ? sky() : (i == 1 ? mint() : sun()));
            }
            thickLine(tft, cx - 13, cy + 14, cx + 14, cy - 13, ink());
            tft.fillTriangle(cx + 14, cy - 13, cx + 8, cy - 12, cx + 13, cy - 7, ink());
            break;
        case LauncherIcon::ColorMix:
            drawPlate(tft, cx, cy, fill);
            tft.fillCircle(cx - 8, cy - 3, 8, Ui::rgb(250, 80, 84));
            tft.fillCircle(cx + 8, cy - 3, 8, Ui::rgb(64, 154, 245));
            tft.fillCircle(cx, cy + 8, 8, sun());
            tft.fillCircle(cx, cy, 5, Ui::rgb(135, 74, 180));
            tft.drawCircle(cx - 8, cy - 3, 8, ink());
            tft.drawCircle(cx + 8, cy - 3, 8, ink());
            tft.drawCircle(cx, cy + 8, 8, ink());
            break;
        case LauncherIcon::SlidingPuzzle:
            drawPlate(tft, cx, cy, fill);
            for (uint8_t i = 0; i < 9; ++i) {
                const uint8_t col = i % 3;
                const uint8_t row = i / 3;
                const int16_t x = static_cast<int16_t>(cx - 13 + col * 9);
                const int16_t y = static_cast<int16_t>(cy - 13 + row * 9);
                if (i == 8) {
                    tft.drawRoundRect(x, y, 8, 8, 2, inkSoft());
                } else {
                    const uint16_t c = (i % 3 == 0) ? sky() : (i % 3 == 1 ? sun() : mint());
                    tft.fillRoundRect(x, y, 8, 8, 2, c);
                    tft.drawRoundRect(x, y, 8, 8, 2, ink());
                }
            }
            break;
        case LauncherIcon::OddOneOut:
            drawPlate(tft, cx, cy, fill);
            for (uint8_t i = 0; i < 4; ++i) {
                tft.fillCircle(static_cast<int16_t>(cx - 12 + i * 8), cy - 1, 3, sky());
            }
            Ui::drawTriangleShape(tft, cx + 13, cy - 1, 5, sun(), true);
            tft.fillCircle(cx + 13, cy + 11, 2, coral());
            break;
        case LauncherIcon::Settings:
            drawPlate(tft, cx, cy, fill);
            Ui::drawGearIcon(tft, Rect{static_cast<int16_t>(cx - 12), static_cast<int16_t>(cy - 12), 24, 24}, ink());
            break;
        case LauncherIcon::WiFi:
            drawPlate(tft, cx, cy, fill);
            tft.drawCircle(cx, cy + 7, 14, ink());
            tft.drawCircle(cx, cy + 7, 9, ink());
            tft.drawCircle(cx, cy + 7, 4, ink());
            tft.fillRect(cx - 16, cy + 8, 32, 12, paper());
            tft.fillCircle(cx, cy + 8, 2, ink());
            break;
        case LauncherIcon::ObjectAdd:
            drawPlate(tft, cx, cy, fill);
            tft.fillCircle(cx - 8, cy - 4, 6, sky());
            tft.fillCircle(cx + 4, cy - 4, 6, mint());
            tft.fillCircle(cx - 2, cy + 8, 6, sun());
            thickLine(tft, cx + 11, cy + 2, cx + 18, cy + 2, ink());
            thickLine(tft, cx + 15, cy - 2, cx + 15, cy + 6, ink());
            break;
        case LauncherIcon::FingerCount:
            drawFingerIcon(tft, cx, cy, fill);
            break;
        case LauncherIcon::Sequence:
            drawPlate(tft, cx, cy, fill);
            smallCard(tft, cx - 14, cy - 14, 28, 28, paper(), ink());
            for (uint8_t i = 0; i < 3; ++i) {
                tft.fillCircle(cx - 8, static_cast<int16_t>(cy - 7 + i * 8), 2,
                               i == 1 ? sun() : sky());
                tft.drawFastHLine(cx - 2, static_cast<int16_t>(cy - 7 + i * 8), 10, inkSoft());
            }
            tft.fillTriangle(cx + 11, cy + 10, cx + 5, cy + 7, cx + 5, cy + 13, coral());
            break;
        case LauncherIcon::NumberLine:
            drawPlate(tft, cx, cy, fill);
            tft.drawFastHLine(cx - 15, cy + 6, 30, ink());
            for (uint8_t tick = 0; tick < 5; ++tick) {
                const int16_t x = static_cast<int16_t>(cx - 15 + tick * 7);
                tft.drawFastVLine(x, cy + 2, 7, ink());
            }
            Ui::drawHopArc(tft, cx - 10, cx + 7, cy + 6, 13, coral(), true);
            tft.fillCircle(cx + 7, cy + 6, 3, sun());
            break;
        case LauncherIcon::Flag:
            drawPlate(tft, cx, cy, fill);
            drawFlagShape(tft, cx, cy, coral(), sun());
            break;
        case LauncherIcon::States:
            drawPlate(tft, cx, cy, fill);
            drawMapSilhouette(tft, cx, cy, Ui::rgb(86, 131, 212));
            tft.fillCircle(cx + 7, cy - 1, 4, sun());
            tft.fillCircle(cx + 7, cy - 1, 1, ink());
            break;
        case LauncherIcon::Trace:
            drawPlate(tft, cx, cy, fill);
            drawText(tft, "A", cx - 5, cy - 2, 4, ink(), paper());
            for (uint8_t i = 0; i < 5; ++i) {
                tft.fillCircle(cx + 11, static_cast<int16_t>(cy - 12 + i * 6),
                               1, mint());
            }
            thickLine(tft, cx + 9, cy + 11, cx + 16, cy + 4, sun());
            tft.fillTriangle(cx + 16, cy + 4, cx + 14, cy + 9, cx + 11, cy + 6, ink());
            break;
        case LauncherIcon::StateFlag:
            drawPlate(tft, cx, cy, fill);
            drawFlagShape(tft, cx, cy, Ui::rgb(78, 134, 228), sun());
            tft.fillCircle(cx + 8, cy + 9, 4, coral());
            break;
        case LauncherIcon::StateMap:
            drawPlate(tft, cx, cy, fill);
            drawMapSilhouette(tft, cx, cy, Ui::rgb(228, 235, 242));
            tft.fillCircle(cx + 9, cy + 7, 4, coral());
            tft.fillCircle(cx + 9, cy + 7, 1, paper());
            break;
        case LauncherIcon::Percent:
            drawPercentIcon(tft, cx, cy, fill);
            break;
        case LauncherIcon::GreWords:
            smallCard(tft, cx - 13, cy - 13, 25, 25, paper(), ink());
            tft.fillRoundRect(cx - 10, cy - 10, 7, 19, 2, sky());
            tft.fillRoundRect(cx - 2, cy - 10, 7, 19, 2, sun());
            drawText(tft, "Aa", cx + 2, cy + 1, 2, ink(), paper());
            break;
        case LauncherIcon::Dice:
            drawPlate(tft, cx, cy, fill);
            drawDieFace(tft, cx - 1, cy - 16, 18, Ui::rgb(226, 235, 244), inkSoft(), 0x101);
            drawDieFace(tft, cx - 15, cy - 2, 21, paper(), ink(), 0x155);
            break;
        case LauncherIcon::CoinFlip:
            drawPlate(tft, cx, cy, fill);
            tft.fillCircle(cx + 5, cy - 1, 12, coinGold());
            tft.drawCircle(cx + 5, cy - 1, 12, ink());
            tft.drawCircle(cx + 5, cy - 1, 8, Ui::shade(coinGold(), 70));
            tft.fillRoundRect(cx - 15, cy - 10, 8, 20, 4, Ui::shade(coinGold(), 128));
            tft.drawRoundRect(cx - 15, cy - 10, 8, 20, 4, ink());
            break;
        case LauncherIcon::Profiles:
            drawPlate(tft, cx, cy, fill);
            tft.fillCircle(cx - 6, cy - 6, 5, sky());
            tft.fillCircle(cx - 6, cy + 8, 9, sky());
            tft.fillRect(cx - 16, cy + 9, 20, 8, paper());
            tft.fillCircle(cx + 8, cy - 4, 5, sun());
            tft.fillCircle(cx + 8, cy + 9, 8, sun());
            tft.fillRect(cx + 1, cy + 10, 16, 8, paper());
            break;
        case LauncherIcon::Scores:
            drawPlate(tft, cx, cy, fill);
            for (uint8_t b = 0; b < 3; ++b) {
                const int16_t h = static_cast<int16_t>(8 + b * 6);
                const int16_t x = static_cast<int16_t>(cx - 13 + b * 10);
                tft.fillRoundRect(x, static_cast<int16_t>(cy + 13 - h), 8, h, 2,
                                  b == 2 ? sun() : sky());
                tft.drawRoundRect(x, static_cast<int16_t>(cy + 13 - h), 8, h, 2, ink());
            }
            break;
        case LauncherIcon::About:
            drawPlate(tft, cx, cy, fill);
            drawText(tft, "i", cx, cy + 1, 4, ink(), paper());
            break;
        case LauncherIcon::SystemInfo:
            drawPlate(tft, cx, cy, fill);
            tft.drawRoundRect(cx - 14, cy - 10, 28, 19, 2, ink());
            tft.fillRoundRect(cx - 10, cy - 6, 20, 11, 2, Ui::rgb(226, 237, 248));
            thickLine(tft, cx - 6, cy, cx - 2, cy - 4, coral());
            thickLine(tft, cx - 2, cy - 4, cx + 2, cy + 4, coral());
            thickLine(tft, cx + 2, cy + 4, cx + 7, cy - 2, coral());
            tft.fillRoundRect(cx - 6, cy + 11, 12, 4, 2, inkSoft());
            break;
    }
    tft.setTextDatum(TL_DATUM);
}
