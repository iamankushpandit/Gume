// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The memory panel behind host/shim/TFT_eSPI.h. See that header for what it
 * promises; this file is how. */

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "HostLed.h"
#include "HostPlatform.h"
#include "HostScreenPower.h"
#include "ui/LauncherLayout.h"

/* TFT_eSPI's built-in fonts, from the copy of the library CMake fetches at the
 * version platformio.ini pins. Including the data rather than vendoring it
 * keeps it under its own licence, in its own tree. Only the three this
 * firmware loads (LOAD_GLCD, LOAD_FONT2, LOAD_FONT4 in platformio.ini). */
#include <Fonts/glcdfont.c>
#include <Fonts/Font16.h>
#include <Fonts/Font32rle.h>

namespace {

struct FontInfo {
    const unsigned char* widths;
    const unsigned char* const* glyphs;
    uint8_t height;
    uint8_t baseline;
};

/* Indexed by font number. 1 is the GLCD font and handled apart; 3, 5 and 6-8
 * are not loaded by this firmware and answer zero, as on the device. */
const FontInfo* fontInfo(uint8_t font) {
    static const FontInfo f2{widtbl_f16, chrtbl_f16, chr_hgt_f16, baseline_f16};
    static const FontInfo f4{widtbl_f32, chrtbl_f32, chr_hgt_f32, baseline_f32};
    if (font == 2) return &f2;
    if (font == 4) return &f4;
    return nullptr;
}

/* TFT_eSPI decodes UTF-8 before drawing. Fonts 2 and 4 then drop anything
 * outside 32..127; the GLCD font draws codes up to 255. */
uint16_t nextCodepoint(const char*& s) {
    const uint8_t c = static_cast<uint8_t>(*s++);
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && (*s & 0xC0) == 0x80) {
        const uint16_t cp = static_cast<uint16_t>(((c & 0x1F) << 6) | (*s & 0x3F));
        ++s;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
        const uint16_t cp = static_cast<uint16_t>(((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) |
                                                  (s[1] & 0x3F));
        s += 2;
        return cp;
    }
    return c;
}

}   // namespace

void TFT_eSPI::begin(uint8_t) {
    rotation_ = 1;
    resetViewport();
    forgetMarks();
    std::fill(std::begin(frame_), std::end(frame_), 0);
    ++frameGen_;
    HostPlatform::attachPanel(this);
}

void TFT_eSPI::setRotation(uint8_t r) {
    rotation_ = r & 3;
    resetViewport();
    forgetMarks();
}

void TFT_eSPI::writecommand(uint8_t c) {
    switch (c) {
        case 0x10:   // Sleep In
            sleeping_ = true;
            HostLed::setDisplayAwake(false);
            HostScreenPower::setAwake(false);
            break;
        case 0x11:   // Sleep Out
            sleeping_ = false;
            HostLed::setDisplayAwake(true);
            HostScreenPower::setAwake(true);
            break;
        case 0x28: displayOn_ = false; break;    // Display Off
        case 0x29: displayOn_ = true; break;     // Display On
        default: break;
    }
}

int16_t TFT_eSPI::width() const {
    if (vpDatum_) return static_cast<int16_t>(vpW_);
    return (rotation_ & 1) ? FRAME_W : FRAME_H;
}

int16_t TFT_eSPI::height() const {
    if (vpDatum_) return static_cast<int16_t>(vpH_);
    return (rotation_ & 1) ? FRAME_H : FRAME_W;
}

void TFT_eSPI::setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool vpDatum) {
    const int32_t lw = (rotation_ & 1) ? FRAME_W : FRAME_H;
    const int32_t lh = (rotation_ & 1) ? FRAME_H : FRAME_W;
    int32_t x1 = std::min(x + w, lw);
    int32_t y1 = std::min(y + h, lh);
    x = std::max<int32_t>(x, 0);
    y = std::max<int32_t>(y, 0);
    vpX_ = x;
    vpY_ = y;
    vpW_ = std::max<int32_t>(x1 - x, 0);
    vpH_ = std::max<int32_t>(y1 - y, 0);
    vpDatum_ = vpDatum;
    vpActive_ = true;
}

void TFT_eSPI::resetViewport() {
    vpX_ = 0;
    vpY_ = 0;
    vpW_ = (rotation_ & 1) ? FRAME_W : FRAME_H;
    vpH_ = (rotation_ & 1) ? FRAME_H : FRAME_W;
    vpDatum_ = false;
    vpActive_ = false;
}

bool TFT_eSPI::toFrame(int32_t x, int32_t y, int32_t& fx, int32_t& fy) const {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    if (x < vpX_ || y < vpY_ || x >= vpX_ + vpW_ || y >= vpY_ + vpH_) return false;
    /* The inverse of Board::pollTouch()'s rotation switch: logical
     * coordinates at this rotation into the rotation-1 frame. */
    switch (rotation_) {
        case 1: fx = x; fy = y; break;
        case 3: fx = FRAME_W - 1 - x; fy = FRAME_H - 1 - y; break;
        case 2: fx = FRAME_W - 1 - y; fy = x; break;
        default: fx = y; fy = FRAME_H - 1 - x; break;   // 0
    }
    return fx >= 0 && fy >= 0 && fx < FRAME_W && fy < FRAME_H;
}

void TFT_eSPI::plot(int32_t x, int32_t y, uint16_t color) {
    int32_t fx = 0, fy = 0;
    if (toFrame(x, y, fx, fy)) {
        frame_[fy * FRAME_W + fx] = color;
        ++frameGen_;
    }
}

void TFT_eSPI::span(int32_t x0, int32_t x1, int32_t y, uint16_t color) {
    if (x1 < x0) std::swap(x0, x1);
    for (int32_t x = x0; x <= x1; ++x) plot(x, y, color);
}

void TFT_eSPI::fillScreen(uint32_t color) {
    /* Like the driver: the whole panel, ignoring any viewport offset but
     * still clipped to it. */
    const bool datum = vpDatum_;
    vpDatum_ = false;
    forgetMarks();
    for (int32_t y = vpY_; y < vpY_ + vpH_; ++y) {
        for (int32_t x = vpX_; x < vpX_ + vpW_; ++x) plot(x, y, static_cast<uint16_t>(color));
    }
    vpDatum_ = datum;
}

void TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t color) {
    plot(x, y, static_cast<uint16_t>(color));
}

void TFT_eSPI::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) {
    if (w <= 0) return;
    span(x, x + w - 1, y, static_cast<uint16_t>(color));
}

void TFT_eSPI::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
    for (int32_t i = 0; i < h; ++i) plot(x, y + i, static_cast<uint16_t>(color));
}

void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    forgetMarksUnder(x, y, w, h);
    noteTabSquare(x, y, w, h);
    for (int32_t j = 0; j < h; ++j) span(x, x + w - 1, y + j, static_cast<uint16_t>(color));
}

void TFT_eSPI::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

/* Midpoint circle, one quadrant arc per bit of `corners`:
 * 1 top-left, 2 top-right, 4 bottom-right, 8 bottom-left. */
void TFT_eSPI::circleQuarters(int32_t x0, int32_t y0, int32_t r, uint8_t corners,
                              uint16_t color) {
    int32_t f = 1 - r;
    int32_t ddx = 1;
    int32_t ddy = -2 * r;
    int32_t x = 0;
    int32_t y = r;
    while (x < y) {
        if (f >= 0) {
            --y;
            ddy += 2;
            f += ddy;
        }
        ++x;
        ddx += 2;
        f += ddx;
        if (corners & 4) { plot(x0 + x, y0 + y, color); plot(x0 + y, y0 + x, color); }
        if (corners & 2) { plot(x0 + x, y0 - y, color); plot(x0 + y, y0 - x, color); }
        if (corners & 8) { plot(x0 - y, y0 + x, color); plot(x0 - x, y0 + y, color); }
        if (corners & 1) { plot(x0 - y, y0 - x, color); plot(x0 - x, y0 - y, color); }
    }
}

/* Filled halves: bit 1 the right half, bit 2 the left, each stretched
 * vertically by `stretch` -- which is what a rounded rectangle's sides are. */
void TFT_eSPI::fillCircleHalves(int32_t x0, int32_t y0, int32_t r, uint8_t sides,
                                int32_t stretch, uint16_t color) {
    int32_t f = 1 - r;
    int32_t ddx = 1;
    int32_t ddy = -2 * r;
    int32_t x = 0;
    int32_t y = r;
    while (x < y) {
        if (f >= 0) {
            --y;
            ddy += 2;
            f += ddy;
        }
        ++x;
        ddx += 2;
        f += ddx;
        if (sides & 1) {
            drawFastVLine(x0 + x, y0 - y, 2 * y + 1 + stretch, color);
            drawFastVLine(x0 + y, y0 - x, 2 * x + 1 + stretch, color);
        }
        if (sides & 2) {
            drawFastVLine(x0 - x, y0 - y, 2 * y + 1 + stretch, color);
            drawFastVLine(x0 - y, y0 - x, 2 * x + 1 + stretch, color);
        }
    }
}

void TFT_eSPI::drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    if (r < 0) return;
    noteIconPrim(Prim::CircleOutline, x - r, y - r, x + r, y + r);
    const uint16_t c = static_cast<uint16_t>(color);
    plot(x, y + r, c);
    plot(x, y - r, c);
    plot(x + r, y, c);
    plot(x - r, y, c);
    circleQuarters(x, y, r, 0x0F, c);
}

void TFT_eSPI::fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    if (r < 0) return;
    noteIconPrim(Prim::CircleFill, x - r, y - r, x + r, y + r);
    drawFastVLine(x, y - r, 2 * r + 1, color);
    fillCircleHalves(x, y, r, 3, 0, static_cast<uint16_t>(color));
}

void TFT_eSPI::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                             uint32_t color) {
    if (w <= 0 || h <= 0) return;
    noteScrollOutline(x, y, w, h, r);
    noteFocusOutline(x, y, w, h);
    noteIconPrim(Prim::RoundOutline, x, y, x + w - 1, y + h - 1);
    r = std::min(r, std::min(w, h) / 2);
    if (r < 0) r = 0;
    drawFastHLine(x + r, y, w - 2 * r, color);
    drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
    drawFastVLine(x, y + r, h - 2 * r, color);
    drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
    const uint16_t c = static_cast<uint16_t>(color);
    circleQuarters(x + r, y + r, r, 1, c);
    circleQuarters(x + w - r - 1, y + r, r, 2, c);
    circleQuarters(x + w - r - 1, y + h - r - 1, r, 4, c);
    circleQuarters(x + r, y + h - r - 1, r, 8, c);
}

void TFT_eSPI::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                             uint32_t color) {
    if (w <= 0 || h <= 0) return;
    noteScrollFill(x, y, w, h, r);
    noteIconPrim(Prim::RoundFill, x, y, x + w - 1, y + h - 1);
    noteTabFill(x, y, w, h, r);
    noteFocusFill(x, y, w, h);
    r = std::min(r, std::min(w, h) / 2);
    if (r < 0) r = 0;
    fillRect(x + r, y, w - 2 * r, h, color);
    const uint16_t c = static_cast<uint16_t>(color);
    fillCircleHalves(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, c);
    fillCircleHalves(x + r, y + r, r, 2, h - 2 * r - 1, c);
}

void TFT_eSPI::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    noteIconPrim(Prim::Line, x0, y0, x1, y1);
    const uint16_t c = static_cast<uint16_t>(color);
    const int32_t dx = std::abs(x1 - x0);
    const int32_t dy = -std::abs(y1 - y0);
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        plot(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void TFT_eSPI::drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                            int32_t y2, uint32_t color) {
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

void TFT_eSPI::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                            int32_t y2, uint32_t color) {
    noteIconPrim(Prim::Triangle, std::min({x0, x1, x2}), std::min({y0, y1, y2}),
                 std::max({x0, x1, x2}), std::max({y0, y1, y2}));
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    const uint16_t c = static_cast<uint16_t>(color);
    if (y0 == y2) {
        span(std::min({x0, x1, x2}), std::max({x0, x1, x2}), y0, c);
        return;
    }
    /* Scanline fill with edges interpolated in integers, so the fill meets
     * the outline drawTriangle() would draw. */
    for (int32_t y = y0; y <= y2; ++y) {
        const int32_t xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int32_t xb;
        if (y < y1 || y1 == y2) {
            xb = (y1 == y0) ? x1 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        } else {
            xb = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        }
        span(xa, xb, y, c);
    }
}

int16_t TFT_eSPI::fontHeight(int16_t font) const {
    if (font == 1) return static_cast<int16_t>(8 * textSize_);
    const FontInfo* info = fontInfo(static_cast<uint8_t>(font));
    return info ? static_cast<int16_t>(info->height * textSize_) : 0;
}

int16_t TFT_eSPI::textWidth(const char* text, uint8_t font) const {
    if (!text) return 0;
    if (font == 1) {
        int32_t n = 0;
        while (*text) {
            if (nextCodepoint(text)) ++n;
        }
        return static_cast<int16_t>(n * 6 * textSize_);
    }
    const FontInfo* info = fontInfo(font);
    if (!info) return 0;
    /* Byte by byte, as TFT_eSPI measures: anything outside 32..127 counts as
     * a space. */
    int32_t width = 0;
    for (; *text; ++text) {
        const uint8_t c = static_cast<uint8_t>(*text);
        width += (c > 31 && c < 128) ? info->widths[c - 32] : info->widths[0];
    }
    return static_cast<int16_t>(width * textSize_);
}

int16_t TFT_eSPI::drawChar(uint16_t code, int32_t x, int32_t y, uint8_t font) {
    if (code == 0) return 0;
    const int32_t size = textSize_;
    const bool fillBg = textFg_ != textBg_;

    if (font == 1) {
        if (code > 255) return 0;
        if (code > 175) ++code;   // TFT_eSPI's default, _cp437 = false
        for (int32_t col = 0; col < 6; ++col) {
            uint8_t line = col < 5 ? ::font[code * 5 + col] : 0;
            for (int32_t row = 0; row < 8; ++row, line >>= 1) {
                if (line & 1) {
                    fillRect(x + col * size, y + row * size, size, size, textFg_);
                } else if (fillBg) {
                    fillRect(x + col * size, y + row * size, size, size, textBg_);
                }
            }
        }
        return static_cast<int16_t>(6 * size);
    }

    const FontInfo* info = fontInfo(font);
    if (!info || code < 32 || code > 127) return 0;
    const int32_t width = info->widths[code - 32];
    const int32_t height = info->height;
    const unsigned char* data = info->glyphs[code - 32];

    if (font == 2) {
        /* One bit per pixel, rows padded to (width + 6) / 8 bytes. */
        const int32_t rowBytes = (width + 6) / 8;
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const bool on = col < rowBytes * 8
                    && (data[row * rowBytes + col / 8] & (0x80 >> (col % 8)));
                if (on) {
                    fillRect(x + col * size, y + row * size, size, size, textFg_);
                } else if (fillBg) {
                    fillRect(x + col * size, y + row * size, size, size, textBg_);
                }
            }
        }
        return static_cast<int16_t>(width * size);
    }

    /* Run-length: bit 7 set is a run of (n & 0x7F) + 1 ink pixels, clear is a
     * run of n + 1 background pixels, row-major across the glyph cell. */
    int32_t pc = 0;
    const int32_t total = width * height;
    while (pc < total) {
        const uint8_t b = *data++;
        const bool ink = (b & 0x80) != 0;
        int32_t run = (b & 0x7F) + 1;
        while (run-- > 0 && pc < total) {
            const int32_t col = pc % width;
            const int32_t row = pc / width;
            if (ink) {
                fillRect(x + col * size, y + row * size, size, size, textFg_);
            } else if (fillBg) {
                fillRect(x + col * size, y + row * size, size, size, textBg_);
            }
            ++pc;
        }
    }
    return static_cast<int16_t>(width * size);
}

int16_t TFT_eSPI::drawString(const char* text, int32_t x, int32_t y, uint8_t font) {
    if (!text) return 0;
    const int32_t w = textWidth(text, font);
    int32_t h = 8 * textSize_;
    int32_t baseline = 0;
    if (font != 1) {
        const FontInfo* info = fontInfo(font);
        h = info ? info->height * textSize_ : 0;
        baseline = info ? info->baseline * textSize_ : 0;
    }
    switch (textDatum_) {
        case TC_DATUM: x -= w / 2; break;
        case TR_DATUM: x -= w; break;
        case ML_DATUM: y -= h / 2; break;
        case MC_DATUM: x -= w / 2; y -= h / 2; break;
        case MR_DATUM: x -= w; y -= h / 2; break;
        case BL_DATUM: y -= h; break;
        case BC_DATUM: x -= w / 2; y -= h; break;
        case BR_DATUM: x -= w; y -= h; break;
        case L_BASELINE: y -= baseline; break;
        case C_BASELINE: x -= w / 2; y -= baseline; break;
        case R_BASELINE: x -= w; y -= baseline; break;
        default: break;
    }
    int32_t advance = 0;
    const char* s = text;
    while (*s) {
        const uint16_t code = nextCodepoint(s);
        advance += drawChar(code, x + advance, y, font);
    }
    notePagerLabel(text, x, y, advance, h);
    if (advance > 0 && h > 0) noteIconPrim(Prim::Text, x, y, x + advance - 1, y + h - 1);
    return static_cast<int16_t>(advance);
}

/* ---- the page keys' target (see pagerTarget() in TFT_eSPI.h) ---------- */

void TFT_eSPI::notePagerLabel(const char* text, int32_t x, int32_t y, int32_t w, int32_t h) {
    int slot = -1;
    if (std::strcmp(text, "Prev") == 0) slot = 0;
    else if (std::strcmp(text, "Next") == 0) slot = 1;
    if (slot < 0) return;
    const int32_t cx = x + w / 2;
    const int32_t cy = y + h / 2;
    int32_t fx = 0, fy = 0;
    PagerMark& m = pager_[slot];
    m.valid = toFrame(cx, cy, fx, fy);   // a clipped label is not a target
    m.lx = vpDatum_ ? cx + vpX_ : cx;
    m.ly = vpDatum_ ? cy + vpY_ : cy;
    m.fx = static_cast<int16_t>(fx);
    m.fy = static_cast<int16_t>(fy);
}

void TFT_eSPI::forgetMarks() {
    pager_[0].valid = pager_[1].valid = false;
    scroll_.valid = scroll_.pending = false;
    iconsRot_ = 0xFF;   // re-placed, with every count zeroed, on the next draw
    for (TabMark& t : tabs_) t.valid = false;
    pendingTab_.valid = false;
    forgetFocus();
}

void TFT_eSPI::forgetMarksUnder(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    auto under = [&](int32_t px, int32_t py) {
        return px >= x && px < x + w && py >= y && py < y + h;
    };
    for (PagerMark& m : pager_) {
        if (m.valid && under(m.lx, m.ly)) m.valid = false;
    }
    /* The track's left edge, which its own thumb never covers: a fill there is
     * the list being repainted or taken away. */
    if (scroll_.valid && under(scroll_.x, scroll_.y + scroll_.h / 2)) scroll_.valid = false;
    forgetIconsUnder(x, y, w, h);
    forgetFocusUnder(x, y, w, h);
    /* A tab's left edge near its foot: its own label never reaches it, and a
     * strip being repainted covers it. */
    for (TabMark& t : tabs_) {
        if (t.valid && under(t.x, t.bottom - 2)) t.valid = false;
    }
}

/* ---- tabs (see nextTabTarget() in TFT_eSPI.h) ------------------------- */

void TFT_eSPI::noteTabFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    pendingTab_.valid = r == 6 && h > 7;
    pendingTab_.rot = rotation_;
    pendingTab_.x = x;
    pendingTab_.top = y;
    pendingTab_.w = w;
    pendingTab_.bottom = y + h;
}

/* Called from fillRect() after it has forgotten whatever it covered, so the
 * tab it completes is recorded after, not wiped by, its own square-off. */
void TFT_eSPI::noteTabSquare(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!pendingTab_.valid) return;
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    if (h != 7 || x != pendingTab_.x || w != pendingTab_.w || y + h != pendingTab_.bottom) return;
    pendingTab_.valid = false;
    TabMark* slot = nullptr;
    for (TabMark& t : tabs_) {
        if (t.valid && t.x == pendingTab_.x && t.bottom == pendingTab_.bottom) {
            slot = &t;   // the same tab, redrawn -- selected or not
            break;
        }
        if (!t.valid && slot == nullptr) slot = &t;
    }
    if (slot == nullptr) return;
    *slot = pendingTab_;
    slot->valid = true;
}

bool TFT_eSPI::nextTabTarget(int16_t& fx, int16_t& fy) const {
    /* The selected tab is the one standing taller than another in its strip. */
    const TabMark* active = nullptr;
    for (const TabMark& a : tabs_) {
        if (!a.valid || a.rot != rotation_) continue;
        for (const TabMark& b : tabs_) {
            if (b.valid && b.rot == rotation_ && &b != &a && b.bottom == a.bottom &&
                a.top < b.top) {
                active = &a;
                break;
            }
        }
        if (active) break;
    }
    if (active == nullptr) return false;
    /* Next to the right in the same strip, or the leftmost if it was last. */
    const TabMark* next = nullptr;
    const TabMark* first = nullptr;
    for (const TabMark& t : tabs_) {
        if (!t.valid || t.rot != rotation_ || t.bottom != active->bottom || &t == active) continue;
        if (t.x > active->x && (next == nullptr || t.x < next->x)) next = &t;
        if (first == nullptr || t.x < first->x) first = &t;
    }
    if (next == nullptr) next = first;
    if (next == nullptr) return false;
    logicalToFrame(next->x + next->w / 2, (next->top + next->bottom) / 2, fx, fy);
    return true;
}

void TFT_eSPI::logicalToFrame(int32_t x, int32_t y, int16_t& fx, int16_t& fy) const {
    switch (rotation_) {   // as toFrame(), without the viewport
        case 1: fx = static_cast<int16_t>(x); fy = static_cast<int16_t>(y); break;
        case 3: fx = static_cast<int16_t>(FRAME_W - 1 - x); fy = static_cast<int16_t>(FRAME_H - 1 - y); break;
        case 2: fx = static_cast<int16_t>(FRAME_W - 1 - y); fy = static_cast<int16_t>(x); break;
        default: fx = static_cast<int16_t>(y); fy = static_cast<int16_t>(FRAME_H - 1 - x); break;
    }
}

/* ---- the header icons (see iconTarget() in TFT_eSPI.h) ---------------- */

void TFT_eSPI::placeIcons() {
    using Mode = Board::LayoutMode;
    const int16_t w = width();
    const Rect places[ICON_KINDS][ICON_SLOTS] = {
        {LauncherLayout::topBarSettingsRect(w), LauncherLayout::gearRect(Mode::Horizontal, w),
         LauncherLayout::gearRect(Mode::Vertical, w), Rect{}},
        {LauncherLayout::topBarLockRect(w), LauncherLayout::lockRect(Mode::Horizontal, w),
         LauncherLayout::lockRect(Mode::Vertical, w), LauncherLayout::pickerLockRect(w)},
        {LauncherLayout::topBarSpeakerRect(w), LauncherLayout::speakerRect(Mode::Horizontal, w),
         LauncherLayout::speakerRect(Mode::Vertical, w), LauncherLayout::pickerSpeakerRect(w)},
        /* Only the top bar: the launcher has no Home, being it. The empty rects
         * count nothing, and iconTrack() passes over them. */
        {LauncherLayout::topBarHomeRect(), Rect{}, Rect{}, Rect{}},
        /* Only the launcher: the top bar names no player. */
        {Rect{}, LauncherLayout::profileRect(Mode::Horizontal, w),
         LauncherLayout::profileRect(Mode::Vertical, w), Rect{}},
    };
    for (int i = 0; i < ICON_KINDS; ++i) {
        for (int j = 0; j < ICON_SLOTS; ++j) {
            IconTrack& t = icons_[i][j];
            t = IconTrack{};
            t.x = places[i][j].x;
            t.y = places[i][j].y;
            t.w = places[i][j].w;
            t.h = places[i][j].h;
        }
    }
    iconsRot_ = rotation_;
}

void TFT_eSPI::noteIconPrim(Prim prim, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (iconsRot_ != rotation_) placeIcons();
    if (vpDatum_) {
        x0 += vpX_; x1 += vpX_;
        y0 += vpY_; y1 += vpY_;
    }
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    for (auto& kind : icons_) {
        for (IconTrack& t : kind) {
            /* A pixel of slack: the gear's teeth reach one past its radius. A
             * name only has to start inside: portrait draws it 2px in from the
             * rect it was fitted to, so a long one ends just past it. */
            const int32_t right = prim == Prim::Text ? x0 : x1;
            if (x0 >= t.x - 1 && right <= t.x + t.w && y0 >= t.y - 1 && y1 <= t.y + t.h) {
                uint8_t& n = t.count[static_cast<uint8_t>(prim)];
                if (prim == Prim::Text) {
                    const bool first = n == 0;
                    t.tx0 = first ? x0 : std::min(t.tx0, x0);
                    t.ty0 = first ? y0 : std::min(t.ty0, y0);
                    t.tx1 = first ? x1 : std::max(t.tx1, x1);
                    t.ty1 = first ? y1 : std::max(t.ty1, y1);
                }
                if (n < 255) ++n;
            }
        }
    }
}

void TFT_eSPI::forgetIconsUnder(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (iconsRot_ != rotation_) return;   // nothing counted at this rotation yet
    for (auto& kind : icons_) {
        for (IconTrack& t : kind) {
            if (x <= t.x && y <= t.y && x + w >= t.x + t.w && y + h >= t.y + t.h) {
                std::fill(std::begin(t.count), std::end(t.count), 0);
            }
        }
    }
}

bool TFT_eSPI::iconDrawn(Icon icon, int slot) const {
    const IconTrack& t = icons_[static_cast<int>(icon)][slot];
    if (t.w <= 0 || t.h <= 0) return false;
    auto n = [&](Prim p) { return t.count[static_cast<uint8_t>(p)]; };
    switch (icon) {
        case Icon::Settings:   // eight teeth of four lines, three hub circles
            return n(Prim::Line) >= 24 && n(Prim::CircleOutline) >= 2;
        case Icon::Lock:       // shackle, body, keyhole
            return n(Prim::RoundOutline) >= 1 && n(Prim::RoundFill) >= 1 && n(Prim::CircleFill) >= 1;
        case Icon::Mute:       // the cone, as two triangles
            return n(Prim::Triangle) >= 2;
        case Icon::Home:       // the roof and the body
            return n(Prim::Triangle) >= 1 && n(Prim::RoundFill) >= 1;
        case Icon::Profile:    // any text there, beside this layout's launcher gear
            return n(Prim::Text) >= 1 && iconDrawn(Icon::Settings, slot);
    }
    return false;
}

const TFT_eSPI::IconTrack* TFT_eSPI::iconTrack(Icon icon) const {
    if (iconsRot_ != rotation_) return nullptr;
    for (int slot = 0; slot < ICON_SLOTS; ++slot) {
        if (iconDrawn(icon, slot)) return &icons_[static_cast<int>(icon)][slot];
    }
    return nullptr;
}

bool TFT_eSPI::iconTarget(Icon icon, int16_t& fx, int16_t& fy) const {
    const IconTrack* t = iconTrack(icon);
    if (!t) return false;
    logicalToFrame(t->x + t->w / 2, t->y + t->h / 2, fx, fy);
    return true;
}

/* ---- the list's scroll bar (see scrollArea() in TFT_eSPI.h) ----------- *
 *
 * RowList::drawScrollBar() draws a SCROLLBAR_W (6) track with corner radius 3,
 * outlines exactly that rectangle, then fills a thumb one pixel inside it with
 * radius 2. The pair of the first two is what identifies it. */

void TFT_eSPI::noteScrollFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    if (w == 6 && r == 3 && h >= 24) {
        scroll_.pending = true;
        scroll_.rot = rotation_;
        scroll_.x = x;
        scroll_.y = y;
        scroll_.w = w;
        scroll_.h = h;
        return;
    }
    scroll_.pending = false;
    if (scroll_.valid && r == 2 && x == scroll_.x + 1 && w == scroll_.w - 2) {
        scroll_.thumbTop = y - 1;
        scroll_.thumbBottom = y + h + 1;
    }
}

void TFT_eSPI::noteScrollOutline(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    if (scroll_.pending && r == 3 && x == scroll_.x && y == scroll_.y && w == scroll_.w &&
        h == scroll_.h) {
        scroll_.valid = true;
        /* Until the thumb arrives, which is the next thing drawn: nowhere to go. */
        scroll_.thumbTop = y;
        scroll_.thumbBottom = y + h;
    }
    scroll_.pending = false;
}

bool TFT_eSPI::scrollArea(ScrollArea& out) const {
    if (!scroll_.valid || scroll_.rot != rotation_ || (rotation_ & 1) == 0) return false;
    /* The list RowList clipped to: the track sits 3px inside its top and
     * bottom and 2px inside its right edge. Its left edge is not drawn
     * anywhere this can see; every list in the firmware starts at x = 0. */
    const int32_t left = 0;
    const int32_t right = scroll_.x + scroll_.w + 1;
    const int32_t top = scroll_.y - 3;
    const int32_t bottom = scroll_.y + scroll_.h + 2;
    const bool moreUp = scroll_.thumbTop > scroll_.y;
    const bool moreDown = scroll_.thumbBottom < scroll_.y + scroll_.h;
    const int32_t grab = scroll_.x + scroll_.w / 2;
    if (rotation_ == 1) {
        out.x0 = static_cast<int16_t>(left);
        out.x1 = static_cast<int16_t>(right);
        out.y0 = static_cast<int16_t>(top);
        out.y1 = static_cast<int16_t>(bottom);
        out.grabX = static_cast<int16_t>(grab);
        out.moreAbove = moreUp;
        out.moreBelow = moreDown;
    } else {   // 3: the same picture turned half a turn
        out.x0 = static_cast<int16_t>(FRAME_W - 1 - right);
        out.x1 = static_cast<int16_t>(FRAME_W - 1 - left);
        out.y0 = static_cast<int16_t>(FRAME_H - 1 - bottom);
        out.y1 = static_cast<int16_t>(FRAME_H - 1 - top);
        out.grabX = static_cast<int16_t>(FRAME_W - 1 - grab);
        out.moreAbove = moreDown;
        out.moreBelow = moreUp;
    }
    if (out.y0 < 0) out.y0 = 0;
    if (out.y1 > FRAME_H - 1) out.y1 = FRAME_H - 1;
    return true;
}

bool TFT_eSPI::pagerTarget(bool next, int16_t& fx, int16_t& fy) const {
    const PagerMark& m = pager_[next ? 1 : 0];
    if (!m.valid) return false;
    fx = m.fx;
    fy = m.fy;
    return true;
}

int16_t TFT_eSPI::drawCentreString(const char* text, int32_t x, int32_t y, uint8_t font) {
    const uint8_t saved = textDatum_;
    textDatum_ = TC_DATUM;
    const int16_t w = drawString(text, x, y, font);
    textDatum_ = saved;
    return w;
}

void TFT_eSPI::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) {
    winX_ = x;
    winY_ = y;
    winW_ = w;
    winH_ = h;
    winAt_ = 0;
}

void TFT_eSPI::pushPixels(const void* data, uint32_t len) {
    const uint16_t* px = static_cast<const uint16_t*>(data);
    for (uint32_t i = 0; i < len; ++i) pushColor(px[i]);
}

void TFT_eSPI::pushColor(uint16_t color) {
    if (winW_ <= 0 || winH_ <= 0 || winAt_ >= winW_ * winH_) return;
    /* The driver sends words high byte first only when swap is ON, so a
     * caller with swap off has handed over words already in bus order. The
     * frame holds values, so it is that case that needs turning round. */
    if (!swapBytes_) color = static_cast<uint16_t>((color >> 8) | (color << 8));
    plot(winX_ + winAt_ % winW_, winY_ + winAt_ / winW_, color);
    ++winAt_;
}
