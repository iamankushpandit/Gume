// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* Every launcher icon stays inside the box its file promises.
 *
 * src/ui/LauncherIcons.cpp draws each tile's icon centred on (cx, cy) and
 * states the rule (rule 3, ICON_HALF): the art reaches no further than 18
 * pixels from that centre, either way. The launcher depends on it. Paging
 * repaints only a tile's INTERIOR -- from two pixels inside the frame, which
 * on the 2.8-inch panel's 42-pixel tile is cy - 19 to cy + 18 -- and leaves
 * the bevel, the outline and the shadow alone, because they do not change
 * from page to page. An icon that draws past cy + 18 draws over the frame,
 * and what it drew there stays behind on the next page: Cinnamon's highlight
 * ring reached cy + 20 and left the bottom of its rim on whatever tile came
 * next, on every board.
 *
 * This draws every icon through a renderer that only records the box each
 * primitive covers, the way TFT_eSPI covers it (a circle of radius r spans
 * cx - r .. cx + r), and fails on any icon whose box leaves the square.
 *
 * Build and run from the repository root:
 *   g++ -std=c++17 -O2 -Wall -Wextra -I src -I include -I host -I host/shim -DGUME_BOARD_HEADER='"boards/host.h"' test/host/launcher_icons_test.cpp src/ui/LauncherIcons.cpp src/ui/LauncherIconArt.cpp -o /tmp/launcher_icons_test && /tmp/launcher_icons_test
 */

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ui/LauncherIcons.h"

namespace {

struct Box {
    int32_t x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    void add(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1) {
        x0 = std::min({x0, ax0, ax1});
        y0 = std::min({y0, ay0, ay1});
        x1 = std::max({x1, ax0, ax1});
        y1 = std::max({y1, ay0, ay1});
    }
    bool empty() const { return x1 < x0; }
};

Box g_box;

/* Records what each primitive covers and draws nothing. */
class Recorder final : public Ui::Renderer {
public:
    int16_t width() override { return 320; }
    int16_t height() override { return 240; }
    void fillScreen(uint16_t) override {}
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t) override {
        if (w > 0 && h > 0) g_box.add(x, y, x + w - 1, y + h - 1);
    }
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) override { fillRect(x, y, w, h, c); }
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t, uint16_t c) override {
        fillRect(x, y, w, h, c);
    }
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t, uint16_t c) override {
        fillRect(x, y, w, h, c);
    }
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t c) override { fillRect(x, y, w, 1, c); }
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t c) override { fillRect(x, y, 1, h, c); }
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t) override {
        g_box.add(x0, y0, x1, y1);
    }
    void drawPixel(int32_t x, int32_t y, uint16_t) override { g_box.add(x, y, x, y); }
    void drawCircle(int32_t x, int32_t y, int32_t r, uint16_t) override {
        g_box.add(x - r, y - r, x + r, y + r);
    }
    void fillCircle(int32_t x, int32_t y, int32_t r, uint16_t c) override { drawCircle(x, y, r, c); }
    void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                      uint16_t) override {
        g_box.add(x0, y0, x1, y1);
        g_box.add(x2, y2, x2, y2);
    }
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                      uint16_t c) override {
        drawTriangle(x0, y0, x1, y1, x2, y2, c);
    }
    void setTextDatum(uint8_t d) override { datum_ = d; }
    void setTextColor(uint16_t) override {}
    void setTextColor(uint16_t, uint16_t, bool) override {}
    void setTextSize(uint8_t s) override { size_ = s ? s : 1; }
    /* Text as the bitmap fonts measure it: 6 a character in font 1, about 8
     * in font 2, cell heights 8 and 16. */
    int16_t textWidth(const char* text, uint8_t font) override {
        return static_cast<int16_t>(std::strlen(text) * (font == 1 ? 6 : 8) * size_);
    }
    int16_t drawString(const char* text, int32_t x, int32_t y, uint8_t font) override {
        const int32_t w = textWidth(text, font);
        const int32_t h = (font == 1 ? 8 : 16) * size_;
        int32_t left = x, top = y;
        if (datum_ == MC_DATUM || datum_ == TC_DATUM || datum_ == BC_DATUM) left -= w / 2;
        if (datum_ == MR_DATUM || datum_ == TR_DATUM || datum_ == BR_DATUM) left -= w;
        if (datum_ == MC_DATUM || datum_ == ML_DATUM || datum_ == MR_DATUM) top -= h / 2;
        if (datum_ == BC_DATUM || datum_ == BL_DATUM || datum_ == BR_DATUM) top -= h;
        if (w > 0) g_box.add(left, top, left + w - 1, top + h - 1);
        return static_cast<int16_t>(w);
    }
    void setViewport(int32_t, int32_t, int32_t, int32_t, bool) override {}
    void resetViewport() override {}
    void startWrite() override {}
    void endWrite() override {}
    void setAddrWindow(int32_t, int32_t, int32_t, int32_t) override {}
    bool getSwapBytes() override { return false; }
    void setSwapBytes(bool) override {}
    void pushPixels(uint16_t*, uint32_t) override {}

private:
    uint8_t datum_ = TL_DATUM;
    uint8_t size_ = 1;
};

}   // namespace

/* The four Ui helpers the icon file calls, standing in for Ui.cpp, which
 * would bring the whole firmware with it. The two drawing ones record the box
 * they are given, which is the box they draw in. */
namespace Ui {
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
uint16_t shade(uint16_t color, uint8_t) { return color; }
void drawGearIcon(Ui::Renderer&, const Rect& r, uint16_t) {
    g_box.add(r.x, r.y, r.x + r.w - 1, r.y + r.h - 1);
}
void drawTriangleShape(Ui::Renderer&, int16_t cx, int16_t cy, int16_t radius, uint16_t, bool) {
    g_box.add(cx - radius, cy - radius, cx + radius, cy + radius);
}
}   // namespace Ui

int main() {
    /* ICON_HALF in LauncherIcons.cpp. */
    constexpr int32_t HALF = 18;
    constexpr int32_t CX = 100, CY = 100;
    const Rect tile{60, 79, 147, 42};   // the 2.8-inch panel's landscape tile
    Recorder rec;
    int failures = 0, icons = 0;
    for (int i = 0; i <= static_cast<int>(LauncherIcon::Nearby); ++i) {
        g_box = Box{};
        drawLauncherIcon(rec, static_cast<LauncherIcon>(i), tile, 0, CX, CY);
        ++icons;
        if (g_box.empty()) continue;
        const int32_t l = CX - g_box.x0, r = g_box.x1 - CX, t = CY - g_box.y0, b = g_box.y1 - CY;
        const bool ok = l <= HALF && r <= HALF && t <= HALF && b <= HALF;
        std::printf("%s icon %2d reaches left %2d right %2d up %2d down %2d\n",
                    ok ? "ok  " : "FAIL", i, l, r, t, b);
        if (!ok) ++failures;
    }
    std::printf("launcher icons: %d of %d inside +/-%d\n", icons - failures, icons, HALF);
    return failures == 0 ? 0 : 1;
}
