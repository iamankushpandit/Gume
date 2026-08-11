#include "Ui.h"
#include "hal/Clock.h"
#include <WiFi.h>
#include "map_n_flag.h"

namespace {
constexpr uint16_t COLOR_BAR_TEXT = TFT_WHITE;

/* Status colours used to be theme-independent brights. On the light theme's
 * white background a pale yellow warning was almost invisible and the green
 * washed out, so each has a darker light-theme counterpart. */
constexpr uint16_t DARK_SUCCESS  = 0x37F0;   // bright green on dark
constexpr uint16_t DARK_ERROR    = 0xF9EA;
constexpr uint16_t DARK_WARNING  = 0xFFE6;   // pale yellow on dark
constexpr uint16_t LIGHT_SUCCESS = 0x04A0;   // deep green on white
constexpr uint16_t LIGHT_ERROR   = 0xC0C3;   // deep red on white
constexpr uint16_t LIGHT_WARNING = 0xBB40;   // amber on white
constexpr uint16_t COLOR_SHADOW   = 0x0000;

// Dark palette
constexpr uint16_t DARK_BG      = 0x0843;
constexpr uint16_t DARK_BAR     = 0x10A6;
constexpr uint16_t DARK_SURFACE = 0x18E8;
constexpr uint16_t DARK_PANEL   = 0x212B;
constexpr uint16_t DARK_TEXT    = 0xF7BE;
constexpr uint16_t DARK_MUTED   = 0xA534;
constexpr uint16_t DARK_OUTLINE = 0x52AA;

// Light palette
constexpr uint16_t LIGHT_BG      = 0xFFFF;
constexpr uint16_t LIGHT_BAR     = 0x10A6; // keep dark header both themes
constexpr uint16_t LIGHT_SURFACE = 0xEF7D;
constexpr uint16_t LIGHT_PANEL   = 0xDEFB;
constexpr uint16_t LIGHT_TEXT    = 0x2124;
constexpr uint16_t LIGHT_MUTED   = 0x8410;
constexpr uint16_t LIGHT_OUTLINE = 0xC618;

// Runtime palette (updated by setTheme)
uint16_t COLOR_BG      = DARK_BG;
uint16_t COLOR_BAR     = DARK_BAR;
uint16_t COLOR_SURFACE = DARK_SURFACE;
uint16_t COLOR_PANEL   = DARK_PANEL;
uint16_t COLOR_TEXT    = DARK_TEXT;
uint16_t COLOR_MUTED   = DARK_MUTED;
uint16_t COLOR_OUTLINE = DARK_OUTLINE;
uint16_t COLOR_SUCCESS = DARK_SUCCESS;
uint16_t COLOR_ERROR   = DARK_ERROR;
uint16_t COLOR_WARNING = DARK_WARNING;
}

namespace Ui {

static Theme s_theme = Theme::Dark;

void setTheme(Theme t) {
    s_theme = t;
    const bool dark = (t == Theme::Dark);
    COLOR_BG      = dark ? DARK_BG      : LIGHT_BG;
    COLOR_BAR     = dark ? DARK_BAR     : LIGHT_BAR;
    COLOR_SURFACE = dark ? DARK_SURFACE : LIGHT_SURFACE;
    COLOR_PANEL   = dark ? DARK_PANEL   : LIGHT_PANEL;
    COLOR_TEXT    = dark ? DARK_TEXT    : LIGHT_TEXT;
    COLOR_MUTED   = dark ? DARK_MUTED   : LIGHT_MUTED;
    COLOR_OUTLINE = dark ? DARK_OUTLINE : LIGHT_OUTLINE;
    COLOR_SUCCESS = dark ? DARK_SUCCESS  : LIGHT_SUCCESS;
    COLOR_ERROR   = dark ? DARK_ERROR    : LIGHT_ERROR;
    COLOR_WARNING = dark ? DARK_WARNING  : LIGHT_WARNING;
}

Theme currentTheme() {
    return s_theme;
}

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

uint16_t shade(uint16_t color, uint8_t percent) {
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;
    r = static_cast<uint16_t>((r * percent) / 100);
    g = static_cast<uint16_t>((g * percent) / 100);
    b = static_cast<uint16_t>((b * percent) / 100);
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
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

void drawGearIcon(TFT_eSPI& tft, const Rect& r, uint16_t color) {
    const int16_t cx = r.x + r.w / 2;
    const int16_t cy = r.y + r.h / 2;
    const int16_t outer = min<int16_t>(r.w, r.h) / 2 - 2;
    const int16_t inner = max<int16_t>(3, outer - 4);
    for (uint8_t i = 0; i < 8; ++i) {
        const float angle = i * PI / 4.0f;
        const int16_t x0 = cx + static_cast<int16_t>(cosf(angle) * inner);
        const int16_t y0 = cy + static_cast<int16_t>(sinf(angle) * inner);
        const int16_t x1 = cx + static_cast<int16_t>(cosf(angle) * (outer + 2));
        const int16_t y1 = cy + static_cast<int16_t>(sinf(angle) * (outer + 2));
        tft.drawLine(x0, y0, x1, y1, color);
    }
    tft.drawCircle(cx, cy, outer, color);
    tft.drawCircle(cx, cy, inner - 1, color);
    tft.fillCircle(cx, cy, 2, color);
}

void drawTopBar(TFT_eSPI& tft, const String& title) {
    tft.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_HEIGHT, COLOR_BAR);
    // Raised edge: highlight along the top, shadow along the bottom seam.
    tft.drawFastHLine(0, 0, SCREEN_WIDTH, shade(COLOR_BAR, 145));
    tft.drawFastHLine(0, TOP_BAR_HEIGHT - 1, SCREEN_WIDTH, shade(COLOR_BAR, 60));
    drawHomeIcon(tft, Rect{0, 0, 42, TOP_BAR_HEIGHT});
    // The top bar stays dark in both themes, so this gear is always white.
    drawGearIcon(tft, Rect{SCREEN_WIDTH - 34, 3, 26, 24}, COLOR_BAR_TEXT);
    tft.setTextColor(COLOR_BAR_TEXT, COLOR_BAR);
    tft.setTextDatum(ML_DATUM);
    String fitted = title;
    while (fitted.length() > 2 && tft.textWidth(fitted, 2) > 110) {
        fitted.remove(fitted.length() - 1);
    }
    tft.drawString(fitted, 48, TOP_BAR_HEIGHT / 2, 2);
    tft.setTextDatum(MR_DATUM);
    /* Right side: clock and its sync badge kept together (the badge describes
     * the clock), then the wifi badge, then the gear. Wedging wifi between the
     * time and its own sync state read as unrelated. */
    tft.drawString(Clock::timeText(), SCREEN_WIDTH - 98, TOP_BAR_HEIGHT / 2, 2);
    Ui::drawSyncBadge(tft, SCREEN_WIDTH - 88, TOP_BAR_HEIGHT / 2, Clock::synced(), COLOR_BAR);
    Ui::drawWifiBadge(tft, SCREEN_WIDTH - 64, TOP_BAR_HEIGHT / 2, COLOR_BAR);
    tft.setTextDatum(TL_DATUM);
}

void drawSyncBadge(TFT_eSPI& tft, int16_t cx, int16_t cy, bool synced, uint16_t bg) {
    const uint16_t col = synced ? COLOR_SUCCESS : COLOR_WARNING;
    // Light theme uses dark fills, so the glyph flips to white to stay legible.
    const uint16_t glyph = (s_theme == Theme::Light) ? TFT_WHITE : TFT_BLACK;
    tft.fillCircle(cx, cy, 6, col);
    tft.drawCircle(cx, cy, 6, bg);
    if (synced) {
        // tick
        tft.drawLine(cx - 3, cy,     cx - 1, cy + 2, glyph);
        tft.drawLine(cx - 3, cy + 1, cx - 1, cy + 3, glyph);
        tft.drawLine(cx - 1, cy + 2, cx + 3, cy - 2, glyph);
        tft.drawLine(cx - 1, cy + 3, cx + 3, cy - 1, glyph);
    } else {
        // exclamation
        tft.drawFastVLine(cx, cy - 3, 4, glyph);
        tft.drawFastVLine(cx + 1, cy - 3, 4, glyph);
        tft.drawPixel(cx, cy + 3, glyph);
        tft.drawPixel(cx + 1, cy + 3, glyph);
    }
}

bool wifiUp() {
    return WiFi.status() == WL_CONNECTED;
}

void drawWifiBadge(TFT_eSPI& tft, int16_t cx, int16_t cy, uint16_t bg) {
    const bool up = wifiUp();

    /* Bars track the real RSSI rather than always showing full strength.
     * Thresholds follow the usual desktop convention. */
    uint8_t lit = 0;
    if (up) {
        const int32_t rssi = WiFi.RSSI();
        lit = rssi > -55 ? 4 : (rssi > -65 ? 3 : (rssi > -75 ? 2 : 1));
    }

    const uint16_t on  = up ? COLOR_SUCCESS : rgb(120, 126, 138);
    // Unlit bars must read against the background in both themes.
    const uint16_t off = (s_theme == Theme::Light) ? rgb(190, 194, 202) : rgb(70, 74, 84);
    const int16_t base = static_cast<int16_t>(cy + 6);

    for (uint8_t i = 0; i < 4; ++i) {
        const int16_t h = static_cast<int16_t>(3 + i * 3);   // 3,6,9,12
        const int16_t x = static_cast<int16_t>(cx - 7 + i * 4);
        tft.fillRect(x, static_cast<int16_t>(base - h), 3, h, i < lit ? on : off);
    }
    if (!up) {
        tft.drawLine(cx - 8, cy - 6, cx + 8, cy + 7, COLOR_ERROR);
        tft.drawLine(cx - 8, cy - 7, cx + 8, cy + 6, COLOR_ERROR);
    }
    (void)bg;
}

void drawSlider(TFT_eSPI& tft, const Rect& r, uint8_t pct, uint8_t minPct) {
    if (pct < minPct) pct = minPct;
    if (pct > 100) pct = 100;

    const uint16_t accent = rgb(36, 132, 204);
    const int16_t cy = static_cast<int16_t>(r.y + r.h / 2);
    const int16_t trackH = 8;
    const int16_t pad = 11;                       // keeps the handle inside r
    const int16_t x0 = static_cast<int16_t>(r.x + pad);
    const int16_t span = static_cast<int16_t>(r.w - 2 * pad);

    const uint16_t range = static_cast<uint16_t>(100 - minPct);
    const int16_t fill = range == 0 ? span
        : static_cast<int16_t>((static_cast<int32_t>(pct - minPct) * span) / range);

    tft.fillRoundRect(x0, static_cast<int16_t>(cy - trackH / 2), span, trackH, trackH / 2, COLOR_PANEL);
    tft.drawRoundRect(x0, static_cast<int16_t>(cy - trackH / 2), span, trackH, trackH / 2, COLOR_OUTLINE);
    if (fill > 2) {
        tft.fillRoundRect(x0, static_cast<int16_t>(cy - trackH / 2), fill, trackH, trackH / 2, accent);
    }

    const int16_t hx = static_cast<int16_t>(x0 + fill);
    tft.fillCircle(hx, cy, 10, COLOR_SURFACE);
    tft.drawCircle(hx, cy, 10, COLOR_OUTLINE);
    tft.fillCircle(hx, cy, 6, accent);
}

uint8_t sliderValueAt(const Rect& r, int16_t x, uint8_t minPct) {
    const int16_t pad = 11;
    const int16_t x0 = static_cast<int16_t>(r.x + pad);
    const int16_t span = static_cast<int16_t>(r.w - 2 * pad);
    if (span <= 0) return minPct;

    int32_t rel = x - x0;
    if (rel < 0) rel = 0;
    if (rel > span) rel = span;
    return static_cast<uint8_t>(minPct + (rel * (100 - minPct)) / span);
}

void drawTab(TFT_eSPI& tft, const Rect& r, const String& label, bool active) {
    /* Active tab: full height, page-coloured, rounded top corners only -- the
     * square bottom is what lets it merge into the content area. Inactive:
     * inset from the top so it reads as sitting behind, and darker. */
    const int16_t top = static_cast<int16_t>(active ? r.y : r.y + 4);
    const int16_t h   = static_cast<int16_t>(active ? r.h : r.h - 4);
    const uint16_t fill = active ? COLOR_SURFACE : COLOR_PANEL;

    tft.fillRoundRect(r.x, top, r.w, h, 6, fill);
    // Square off the bottom so the rounded corners only appear at the top.
    tft.fillRect(r.x, static_cast<int16_t>(top + h - 7), r.w, 7, fill);

    tft.drawFastHLine(static_cast<int16_t>(r.x + 6), top, static_cast<int16_t>(r.w - 12), COLOR_OUTLINE);
    tft.drawFastVLine(r.x, static_cast<int16_t>(top + 6), static_cast<int16_t>(h - 6), COLOR_OUTLINE);
    tft.drawFastVLine(static_cast<int16_t>(r.x + r.w - 1), static_cast<int16_t>(top + 6),
                      static_cast<int16_t>(h - 6), COLOR_OUTLINE);
    if (active) {
        // A brighter lip along the top reads as the selected sheet.
        tft.drawFastHLine(static_cast<int16_t>(r.x + 6), static_cast<int16_t>(top + 1),
                          static_cast<int16_t>(r.w - 12), shade(fill, 150));
    }

    tft.setTextColor(active ? COLOR_TEXT : COLOR_MUTED, fill);
    tft.setTextDatum(MC_DATUM);
    String fitted = label;
    while (fitted.length() > 2 && tft.textWidth(fitted, 2) > r.w - 10) {
        fitted.remove(fitted.length() - 1);
    }
    tft.drawString(fitted, static_cast<int16_t>(r.x + r.w / 2),
                   static_cast<int16_t>(top + h / 2), 2);
    tft.setTextDatum(TL_DATUM);
}

void drawTabBaseline(TFT_eSPI& tft, int16_t y, int16_t x0, int16_t x1,
                     const Rect& activeTab) {
    // Draw the rule under the strip, but skip the active tab so it joins the
    // page below -- the detail that makes it read as a tab rather than a button.
    tft.drawFastHLine(x0, y, static_cast<int16_t>(activeTab.x - x0), COLOR_OUTLINE);
    const int16_t rightStart = static_cast<int16_t>(activeTab.x + activeTab.w);
    tft.drawFastHLine(rightStart, y, static_cast<int16_t>(x1 - rightStart), COLOR_OUTLINE);
}

void drawButton(TFT_eSPI& tft, const Rect& r, const String& label, uint16_t fill, uint16_t outline, uint16_t text, bool pressed, uint8_t font) {
    const int16_t yOffset = pressed ? 1 : 0;
    if (!pressed) {
        tft.fillRoundRect(r.x + 2, r.y + 3, r.w, r.h, 6, COLOR_SHADOW);
    }
    tft.fillRoundRect(r.x, r.y + yOffset, r.w, r.h, 6, fill);

    /* Bevel: a lighter line under the top edge and a darker one above the
     * bottom edge reads as a raised surface. Inverted while pressed so the
     * button visibly sinks. Two hlines, so the cost is negligible. */
    if (r.w > 10 && r.h > 8) {
        const uint16_t hi = shade(fill, 138);
        const uint16_t lo = shade(fill, 68);
        tft.drawFastHLine(r.x + 4, r.y + yOffset + 1, r.w - 8, pressed ? lo : hi);
        tft.drawFastHLine(r.x + 4, r.y + yOffset + r.h - 2, r.w - 8, pressed ? hi : lo);
    }

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

// ---------------------------------------------------------------------------
// map-n-flag image blitting
// ---------------------------------------------------------------------------
namespace {
// Widest thing we ever draw is max(flag width, map box) pixels.
constexpr int16_t MNF_MAX_ROW = (MNF_FLAG_WIDTH > MNF_MAP_BOX) ? MNF_FLAG_WIDTH : MNF_MAP_BOX;
}

/*
 * TFT_eSPI's pushPixels() sends the raw 16-bit values straight out; whether the
 * high or low byte goes first is governed by the swapBytes flag. Our buffers
 * hold host-order RGB565, so without swapBytes the panel reads the halves
 * transposed: red 0xF800 arrives as 0x00F8 (blue) and blue 0x001F as 0x1F00
 * (green). White survives, which is why only the coloured parts looked wrong.
 *
 * Save/restore rather than setting it globally, so we do not disturb any other
 * drawing code that relies on the current setting.
 */
void drawCountryImage(TFT_eSPI& tft, const void* img, int16_t x, int16_t y, uint16_t bgColor) {
    const mnf_img_t* im = static_cast<const mnf_img_t*>(img);
    if (im == nullptr) return;

    uint16_t row[MNF_MAX_ROW];
    const bool prevSwap = tft.getSwapBytes();
    tft.setSwapBytes(true);
    tft.startWrite();
    for (uint16_t yy = 0; yy < im->h; ++yy) {
        mnf_row_rgb565(im, yy, row, bgColor);
        tft.setAddrWindow(x, static_cast<int16_t>(y + yy), im->w, 1);
        tft.pushPixels(row, im->w);
    }
    tft.endWrite();
    tft.setSwapBytes(prevSwap);
}

bool drawCountryImageCentred(TFT_eSPI& tft, const void* img, const Rect& r, uint16_t bgColor) {
    const mnf_img_t* im = static_cast<const mnf_img_t*>(img);
    if (im == nullptr) return false;
    drawCountryImage(tft, im,
                     static_cast<int16_t>(r.x + (r.w - im->w) / 2),
                     static_cast<int16_t>(r.y + (r.h - im->h) / 2), bgColor);
    return true;
}

bool drawCountryImageTinted(TFT_eSPI& tft, const void* img, const Rect& r,
                            uint16_t bgColor, uint16_t inkColor) {
    const mnf_img_t* im = static_cast<const mnf_img_t*>(img);
    if (im == nullptr) return false;

    const int16_t x = static_cast<int16_t>(r.x + (r.w - im->w) / 2);
    const int16_t y = static_cast<int16_t>(r.y + (r.h - im->h) / 2);

    uint16_t row[MNF_MAX_ROW];
    const bool prevSwap = tft.getSwapBytes();
    tft.setSwapBytes(true);
    tft.startWrite();
    for (uint16_t yy = 0; yy < im->h; ++yy) {
        mnf_row_rgb565_tint(im, yy, row, bgColor, inkColor);
        tft.setAddrWindow(x, static_cast<int16_t>(y + yy), im->w, 1);
        tft.pushPixels(row, im->w);
    }
    tft.endWrite();
    tft.setSwapBytes(prevSwap);
    return true;
}

bool drawCountryImageScaled(TFT_eSPI& tft, const void* img, const Rect& r,
                            uint16_t bgColor, uint8_t scale) {
    const mnf_img_t* im = static_cast<const mnf_img_t*>(img);
    if (im == nullptr) return false;
    if (scale < 1) scale = 1;

    // Cap the scale so the result still fits the target rect.
    while (scale > 1 && (im->w * scale > r.w || im->h * scale > r.h)) --scale;
    if (scale == 1) return drawCountryImageCentred(tft, im, r, bgColor);

    const int16_t outW = static_cast<int16_t>(im->w * scale);
    const int16_t x = static_cast<int16_t>(r.x + (r.w - outW) / 2);
    const int16_t y = static_cast<int16_t>(r.y + (r.h - im->h * scale) / 2);

    uint16_t src[MNF_MAX_ROW];
    uint16_t dst[MNF_MAX_ROW * 3];   // scale is clamped to 3 by the rect check below
    if (outW > static_cast<int16_t>(sizeof(dst) / sizeof(dst[0]))) {
        return drawCountryImageCentred(tft, im, r, bgColor);
    }

    const bool prevSwap = tft.getSwapBytes();
    tft.setSwapBytes(true);
    tft.startWrite();
    for (uint16_t yy = 0; yy < im->h; ++yy) {
        mnf_row_rgb565(im, yy, src, bgColor);
        for (uint16_t xx = 0; xx < im->w; ++xx) {
            for (uint8_t s = 0; s < scale; ++s) dst[xx * scale + s] = src[xx];
        }
        for (uint8_t s = 0; s < scale; ++s) {
            tft.setAddrWindow(x, static_cast<int16_t>(y + yy * scale + s), outW, 1);
            tft.pushPixels(dst, outW);
        }
    }
    tft.endWrite();
    tft.setSwapBytes(prevSwap);
    return true;
}

}
