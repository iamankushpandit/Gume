#include "Ui.h"
#include "hal/Clock.h"

namespace {
constexpr uint16_t COLOR_BG = 0x0843;
constexpr uint16_t COLOR_BAR = 0x10A6;
constexpr uint16_t COLOR_SURFACE = 0x18E8;
constexpr uint16_t COLOR_PANEL = 0x212B;
constexpr uint16_t COLOR_TEXT = 0xF7BE;
constexpr uint16_t COLOR_MUTED = 0xA534;
constexpr uint16_t COLOR_OUTLINE = 0x52AA;
constexpr uint16_t COLOR_SUCCESS = 0x37F0;
constexpr uint16_t COLOR_ERROR = 0xF9EA;
constexpr uint16_t COLOR_WARNING = 0xFFE6;
constexpr uint16_t COLOR_BAR_TEXT = TFT_WHITE;
constexpr uint16_t COLOR_SHADOW = 0x0000;
}

namespace Ui {

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t bg() {
    return COLOR_BG;
}

uint16_t surface() {
    return COLOR_SURFACE;
}

uint16_t panel() {
    return COLOR_PANEL;
}

uint16_t text() {
    return COLOR_TEXT;
}

uint16_t muted() {
    return COLOR_MUTED;
}

uint16_t outline() {
    return COLOR_OUTLINE;
}

uint16_t success() {
    return COLOR_SUCCESS;
}

uint16_t error() {
    return COLOR_ERROR;
}

uint16_t warning() {
    return COLOR_WARNING;
}

void clear(TFT_eSPI& tft) {
    tft.fillScreen(COLOR_BG);
}

void drawHomeIcon(TFT_eSPI& tft, const Rect& r) {
    const int16_t cx = r.x + r.w / 2;
    const int16_t roofY = r.y + 6;
    tft.fillTriangle(cx, roofY, r.x + 8, r.y + 16, r.x + r.w - 8, r.y + 16, TFT_WHITE);
    tft.fillRoundRect(r.x + 12, r.y + 15, r.w - 24, r.h - 20, 2, TFT_WHITE);
    tft.fillRect(cx - 4, r.y + r.h - 12, 8, 7, COLOR_BAR);
}

void drawTopBar(TFT_eSPI& tft, const String& title) {
    tft.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_HEIGHT, COLOR_BAR);
    drawHomeIcon(tft, Rect{0, 0, 42, TOP_BAR_HEIGHT});
    tft.setTextColor(COLOR_BAR_TEXT, COLOR_BAR);
    tft.setTextDatum(ML_DATUM);
    String fitted = title;
    while (fitted.length() > 2 && tft.textWidth(fitted, 2) > 178) {
        fitted.remove(fitted.length() - 1);
    }
    tft.drawString(fitted, 48, TOP_BAR_HEIGHT / 2, 2);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(Clock::timeText(), SCREEN_WIDTH - 6, TOP_BAR_HEIGHT / 2, 2);
    tft.setTextDatum(TL_DATUM);
}

void drawButton(TFT_eSPI& tft, const Rect& r, const String& label, uint16_t fill, uint16_t outline, uint16_t text, bool pressed, uint8_t font) {
    const int16_t yOffset = pressed ? 1 : 0;
    if (!pressed) {
        tft.fillRoundRect(r.x + 2, r.y + 3, r.w, r.h, 6, COLOR_SHADOW);
    }
    tft.fillRoundRect(r.x, r.y + yOffset, r.w, r.h, 6, fill);
    tft.drawRoundRect(r.x, r.y + yOffset, r.w, r.h, 6, outline);
    tft.setTextColor(text, fill);
    tft.setTextDatum(MC_DATUM);

    String fitted = label;
    while (fitted.length() > 2 && tft.textWidth(fitted, font) > r.w - 12) {
        fitted.remove(fitted.length() - 1);
    }
    if (fitted.length() < label.length() && fitted.length() > 1) {
        fitted.setCharAt(fitted.length() - 1, '.');
    }
    tft.drawString(fitted, r.x + r.w / 2, r.y + yOffset + r.h / 2, font);
    tft.setTextDatum(TL_DATUM);
}

void drawLabel(TFT_eSPI& tft, const Rect& r, const String& text, uint16_t color, uint8_t font, Align align) {
    tft.setTextColor(color, COLOR_BG);
    if (align == Align::Center) {
        tft.setTextDatum(MC_DATUM);
        tft.drawString(text, r.x + r.w / 2, r.y + r.h / 2, font);
    } else {
        tft.setTextDatum(TL_DATUM);
        tft.drawString(text, r.x, r.y, font);
    }
    tft.setTextDatum(TL_DATUM);
}

int16_t drawWrappedText(TFT_eSPI& tft, const String& text, const Rect& r, uint16_t color, uint8_t font, Align align) {
    tft.setTextColor(color, COLOR_BG);
    const int16_t lineHeight = font == 4 ? 26 : 18;
    int16_t y = r.y;
    String line;
    String word;

    auto drawLine = [&]() {
        if (line.length() == 0 || y > r.y + r.h - lineHeight) {
            return;
        }
        int16_t x = r.x;
        if (align == Align::Center) {
            x = r.x + (r.w - tft.textWidth(line, font)) / 2;
        }
        tft.drawString(line, x, y, font);
        y += lineHeight;
        line = "";
    };

    for (uint16_t i = 0; i <= text.length(); ++i) {
        const char c = i < text.length() ? text.charAt(i) : ' ';
        if (c == ' ' || c == '\n' || c == '\t' || i == text.length()) {
            if (word.length() > 0) {
                String candidate = line.length() == 0 ? word : line + " " + word;
                if (tft.textWidth(candidate, font) > r.w && line.length() > 0) {
                    drawLine();
                    candidate = word;
                }
                line = candidate;
                word = "";
            }
            if (c == '\n') {
                drawLine();
            }
        } else {
            word += c;
        }
    }
    drawLine();
    return y;
}

void drawTriangleShape(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color, bool filled) {
    const int16_t x0 = cx;
    const int16_t y0 = cy - radius;
    const int16_t x1 = cx - radius;
    const int16_t y1 = cy + radius;
    const int16_t x2 = cx + radius;
    const int16_t y2 = cy + radius;
    if (filled) {
        tft.fillTriangle(x0, y0, x1, y1, x2, y2, color);
    } else {
        tft.drawTriangle(x0, y0, x1, y1, x2, y2, color);
        tft.drawTriangle(x0, y0 + 1, x1 + 1, y1 - 1, x2 - 1, y2 - 1, color);
    }
}

void drawStarShape(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color, bool filled) {
    const int16_t inner = radius / 2;
    int16_t px[10];
    int16_t py[10];
    for (int i = 0; i < 10; ++i) {
        const float angle = -PI / 2.0f + i * PI / 5.0f;
        const int16_t rr = (i % 2 == 0) ? radius : inner;
        px[i] = cx + static_cast<int16_t>(cosf(angle) * rr);
        py[i] = cy + static_cast<int16_t>(sinf(angle) * rr);
    }
    if (filled) {
        for (int i = 1; i < 9; ++i) {
            tft.fillTriangle(px[0], py[0], px[i], py[i], px[i + 1], py[i + 1], color);
        }
    }
    for (int i = 0; i < 10; ++i) {
        const int next = (i + 1) % 10;
        tft.drawLine(px[i], py[i], px[next], py[next], color);
    }
}

}
