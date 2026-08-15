#include "ui/LauncherIcons.h"

void drawLauncherIcon(Ui::Renderer& tft, LauncherIcon icon, const Rect& r,
                      uint16_t fill, int16_t cx, int16_t cy) {
    (void)r;
    switch (icon) {
        case LauncherIcon::TicTacToe:
            tft.drawLine(cx - 10, cy - 14, cx - 10, cy + 14, TFT_WHITE);
            tft.drawLine(cx + 10, cy - 14, cx + 10, cy + 14, TFT_WHITE);
            tft.drawLine(cx - 16, cy - 5, cx + 16, cy - 5, TFT_WHITE);
            tft.drawLine(cx - 16, cy + 7, cx + 16, cy + 7, TFT_WHITE);
            break;
        case LauncherIcon::Memory:
            tft.fillRoundRect(cx - 14, cy - 12, 18, 24, 3, TFT_WHITE);
            tft.fillRoundRect(cx - 2, cy - 10, 18, 24, 3, Ui::rgb(255, 246, 178));
            break;
        case LauncherIcon::Math:
            tft.fillRoundRect(cx - 16, cy - 14, 32, 28, 4, TFT_WHITE);
            tft.setTextColor(Ui::rgb(36, 132, 204), TFT_WHITE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("+", cx - 7, cy - 5, 2);
            tft.drawString("-", cx + 8, cy + 6, 2);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::Multiplication:
            tft.fillRoundRect(cx - 17, cy - 14, 34, 28, 4, TFT_WHITE);
            tft.setTextColor(Ui::rgb(36, 132, 204), TFT_WHITE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("x", cx, cy - 3, 2);
            tft.drawString("12", cx + 2, cy + 10, 1);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::Time:
            tft.fillCircle(cx, cy, 16, TFT_WHITE);
            tft.drawCircle(cx, cy, 16, Ui::rgb(36, 132, 204));
            tft.drawLine(cx, cy, cx, cy - 10, Ui::rgb(36, 132, 204));
            tft.drawLine(cx, cy, cx + 9, cy + 4, Ui::rgb(222, 83, 83));
            tft.fillCircle(cx, cy, 2, Ui::rgb(36, 132, 204));
            break;
        case LauncherIcon::WhackAMole:
            tft.drawRect(cx - 16, cy - 16, 32, 32, TFT_WHITE);
            tft.drawLine(cx - 5, cy - 16, cx - 5, cy + 16, TFT_WHITE);
            tft.drawLine(cx + 6, cy - 16, cx + 6, cy + 16, TFT_WHITE);
            tft.drawLine(cx - 16, cy - 5, cx + 16, cy - 5, TFT_WHITE);
            tft.drawLine(cx - 16, cy + 6, cx + 16, cy + 6, TFT_WHITE);
            tft.fillCircle(cx + 11, cy - 10, 5, Ui::rgb(255, 246, 178));
            break;
        case LauncherIcon::Cinnamon:
            tft.fillCircle(cx - 10, cy - 8, 7, Ui::rgb(255, 112, 112));
            tft.fillCircle(cx + 10, cy - 8, 7, Ui::rgb(94, 190, 255));
            tft.fillCircle(cx - 10, cy + 10, 7, Ui::rgb(108, 232, 148));
            tft.fillCircle(cx + 10, cy + 10, 7, Ui::rgb(255, 232, 94));
            break;
        case LauncherIcon::Microku:
            tft.fillRoundRect(cx - 16, cy - 16, 32, 32, 2, TFT_WHITE);
            tft.drawLine(cx, cy - 16, cx, cy + 16, Ui::rgb(36, 132, 204));
            tft.drawLine(cx - 16, cy, cx + 16, cy, Ui::rgb(36, 132, 204));
            tft.setTextColor(Ui::rgb(36, 132, 204), TFT_WHITE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("1", cx - 8, cy - 8, 1);
            tft.drawString("4", cx + 8, cy + 8, 1);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::ShapeColor:
            tft.fillCircle(cx - 7, cy - 1, 10, TFT_WHITE);
            Ui::drawTriangleShape(tft, cx + 10, cy + 8, 10, Ui::rgb(255, 246, 178), true);
            break;
        case LauncherIcon::Counting:
            for (uint8_t i = 0; i < 5; ++i) {
                tft.fillCircle(cx - 12 + i * 6, cy + (i % 2) * 8 - 4, 4, TFT_WHITE);
            }
            break;
        case LauncherIcon::Money:
            tft.fillCircle(cx - 10, cy - 2, 9, Ui::rgb(184, 96, 52));
            tft.fillCircle(cx + 6, cy + 4, 11, Ui::rgb(160, 170, 176));
            tft.setTextColor(TFT_BLACK, Ui::rgb(160, 170, 176));
            tft.setTextDatum(MC_DATUM);
            tft.drawString("5", cx + 6, cy + 4, 1);
            tft.setTextColor(TFT_WHITE, Ui::rgb(184, 96, 52));
            tft.drawString("1", cx - 10, cy - 2, 1);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::Fractions:
            tft.fillCircle(cx, cy, 16, TFT_WHITE);
            tft.fillTriangle(cx, cy, cx, cy - 16, cx + 16, cy, Ui::rgb(255, 202, 84));
            tft.fillTriangle(cx, cy, cx + 16, cy, cx, cy + 16, Ui::rgb(255, 202, 84));
            tft.drawCircle(cx, cy, 16, Ui::rgb(36, 132, 204));
            tft.drawLine(cx, cy - 16, cx, cy + 16, Ui::rgb(36, 132, 204));
            tft.drawLine(cx - 16, cy, cx + 16, cy, Ui::rgb(36, 132, 204));
            break;
        case LauncherIcon::Maze:
            tft.drawRect(cx - 16, cy - 14, 32, 28, TFT_WHITE);
            tft.drawLine(cx - 6, cy - 14, cx - 6, cy + 8, TFT_WHITE);
            tft.drawLine(cx + 6, cy - 2, cx + 16, cy - 2, TFT_WHITE);
            tft.fillCircle(cx - 10, cy + 8, 4, Ui::rgb(255, 246, 178));
            break;
        case LauncherIcon::Sort:
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("1", cx - 12, cy + 8, 2);
            tft.drawString("2", cx, cy, 2);
            tft.drawString("3", cx + 12, cy - 8, 2);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::ColorMix:
            tft.fillCircle(cx - 10, cy, 10, 0xF800);
            tft.fillCircle(cx + 10, cy, 10, 0x041F);
            tft.fillCircle(cx, cy + 10, 10, 0x9813);
            break;
        case LauncherIcon::SlidingPuzzle:
            for (uint8_t i = 0; i < 4; ++i) {
                const int16_t ox = (i % 2) * 16 - 16;
                const int16_t oy = (i / 2) * 16 - 16;
                if (i < 3) {
                    tft.fillRoundRect(cx + ox, cy + oy, 14, 14, 2, TFT_WHITE);
                }
            }
            break;
        case LauncherIcon::OddOneOut:
            for (uint8_t i = 0; i < 5; ++i) {
                tft.fillCircle(cx - 16 + i * 8, cy, 4, i == 3 ? Ui::rgb(255, 246, 178) : TFT_WHITE);
            }
            break;
        case LauncherIcon::Settings:
            Ui::drawGearIcon(tft, Rect{static_cast<int16_t>(cx - 12), static_cast<int16_t>(cy - 12), 24, 24});
            break;
        case LauncherIcon::WiFi:
            tft.drawCircle(cx, cy + 6, 12, TFT_WHITE);
            tft.drawCircle(cx, cy + 6, 7, TFT_WHITE);
            tft.fillCircle(cx, cy + 8, 2, TFT_WHITE);
            tft.fillRect(cx - 13, cy + 7, 26, 14, fill);
            break;
        case LauncherIcon::ObjectAdd:
            tft.fillCircle(cx - 10, cy - 4, 7, TFT_WHITE);
            tft.fillCircle(cx + 2, cy - 4, 7, Ui::rgb(108, 232, 148));
            tft.fillCircle(cx - 4, cy + 10, 7, Ui::rgb(255, 246, 178));
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("+", cx + 14, cy + 4, 2);
            break;
        case LauncherIcon::FingerCount:
            for (uint8_t f = 0; f < 5; ++f) {
                tft.fillRoundRect(cx - 14 + f * 6, cy - 14, 5, 16, 2,
                    f < 3 ? TFT_WHITE : Ui::rgb(108, 232, 148));
            }
            tft.fillRoundRect(cx - 14, cy + 4, 28, 8, 2, TFT_WHITE);
            break;
        case LauncherIcon::Sequence:
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("Mon", cx - 4, cy - 8, 1);
            tft.drawString("Tue", cx - 4, cy + 2, 1);
            tft.drawString("Wed", cx - 4, cy + 12, 1);
            break;
        case LauncherIcon::NumberLine:
            tft.drawFastHLine(cx - 17, cy + 4, 34, TFT_WHITE);
            for (uint8_t t = 0; t < 5; ++t) {
                tft.drawFastVLine(cx - 17 + t * 8, cy, 5, TFT_WHITE);
            }
            tft.fillCircle(cx + 7, cy - 4, 5, Ui::rgb(255, 246, 178));
            break;
        case LauncherIcon::Flag:
            tft.drawFastVLine(cx - 12, cy - 15, 30, TFT_WHITE);
            tft.fillRect(cx - 11, cy - 14, 24, 15, Ui::rgb(230, 90, 90));
            tft.fillRect(cx - 11, cy - 14, 24, 5, Ui::rgb(255, 246, 178));
            tft.drawRect(cx - 11, cy - 14, 24, 15, TFT_WHITE);
            break;
        case LauncherIcon::States:
            tft.fillRoundRect(cx - 16, cy - 11, 32, 22, 3, Ui::rgb(60, 90, 170));
            tft.fillRect(cx - 16, cy - 11, 14, 10, Ui::rgb(232, 232, 242));
            tft.drawRoundRect(cx - 16, cy - 11, 32, 22, 3, TFT_WHITE);
            break;
        case LauncherIcon::Trace:
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("A", cx - 6, cy - 2, 4);
            for (int16_t d = 0; d < 12; d += 3) {
                tft.fillCircle(cx + 12, cy - 10 + d, 1, Ui::rgb(108, 232, 148));
            }
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::StateFlag:
            tft.drawFastVLine(cx - 12, cy - 15, 30, TFT_WHITE);
            tft.fillRect(cx - 11, cy - 14, 24, 15, Ui::rgb(90, 130, 230));
            tft.fillRect(cx - 11, cy - 14, 24, 5, Ui::rgb(255, 246, 178));
            tft.drawRect(cx - 11, cy - 14, 24, 15, TFT_WHITE);
            tft.fillCircle(cx + 10, cy + 8, 4, Ui::rgb(255, 200, 0));
            break;
        case LauncherIcon::StateMap:
            tft.fillRoundRect(cx - 14, cy - 14, 28, 28, 2, Ui::bg());
            tft.drawRoundRect(cx - 14, cy - 14, 28, 28, 2, TFT_WHITE);
            tft.fillTriangle(cx - 6, cy - 8, cx + 10, cy - 2, cx - 2, cy + 10, Ui::rgb(36, 132, 204));
            tft.fillCircle(cx + 10, cy + 8, 4, Ui::rgb(255, 200, 0));
            break;
        case LauncherIcon::Percent:
            tft.fillCircle(cx, cy, 16, TFT_WHITE);
            tft.fillTriangle(cx, cy, cx, cy - 16, cx + 16, cy - 16, Ui::rgb(255, 202, 84));
            tft.fillTriangle(cx, cy, cx + 16, cy - 16, cx + 16, cy, Ui::rgb(255, 202, 84));
            tft.drawCircle(cx, cy, 16, Ui::rgb(36, 132, 204));
            tft.setTextColor(Ui::rgb(36, 132, 204), fill);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("%", cx, cy, 1);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::GreWords:
            tft.fillRoundRect(cx - 12, cy - 12, 20, 20, 3, Ui::rgb(160, 170, 176));
            tft.fillRoundRect(cx - 14, cy - 14, 20, 20, 3, TFT_WHITE);
            tft.setTextColor(Ui::rgb(36, 132, 204), TFT_WHITE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("Aa", cx - 2, cy - 2, 2);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::Profiles:
            tft.fillCircle(cx - 6, cy - 5, 6, TFT_WHITE);
            tft.fillCircle(cx - 6, cy + 9, 10, TFT_WHITE);
            tft.fillRect(cx - 18, cy + 10, 24, 8, fill);
            tft.fillCircle(cx + 9, cy - 3, 5, Ui::rgb(255, 226, 90));
            tft.fillCircle(cx + 9, cy + 9, 8, Ui::rgb(255, 226, 90));
            tft.fillRect(cx + 1, cy + 10, 18, 8, fill);
            break;
        case LauncherIcon::Scores:
            for (uint8_t b = 0; b < 3; ++b) {
                const int16_t h = static_cast<int16_t>(8 + b * 7);
                tft.fillRect(static_cast<int16_t>(cx - 14 + b * 10),
                             static_cast<int16_t>(cy + 12 - h), 8, h,
                             b == 2 ? Ui::rgb(255, 226, 90) : TFT_WHITE);
            }
            break;
        case LauncherIcon::About:
            tft.fillCircle(cx, cy, 16, TFT_WHITE);
            tft.setTextColor(Ui::rgb(36, 132, 204), TFT_WHITE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("i", cx, cy + 1, 4);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::SystemInfo:
            tft.drawRoundRect(cx - 15, cy - 10, 30, 20, 2, TFT_WHITE);
            tft.fillTriangle(cx - 2, cy - 8, cx - 6, cy + 1, cx - 1, cy + 1, Ui::rgb(255, 226, 90));
            tft.fillTriangle(cx + 2, cy + 8, cx + 6, cy - 1, cx + 1, cy - 1, Ui::rgb(255, 226, 90));
            break;
    }
}
