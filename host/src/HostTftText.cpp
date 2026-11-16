// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The panel shim's text: TFT_eSPI's bitmap fonts for their cells, and
 * GNU FreeSans drawn into those cells at the panel's real resolution
 * (HostText.h). Split from HostTft.cpp. See host/shim/TFT_eSPI.h. */

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HostText.h"

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

/* ---- text in the TrueType face (HostText.h) ------------------------------
 *
 * Laid out exactly as the bitmap fonts are -- the same cell height, the same
 * baseline, the same datums -- so a screen's boxes hold it; drawn from
 * FreeSans with capitals as tall as the bitmap font's, at the real
 * resolution. Fonts 6, 7 and 8 are not loaded by this firmware and still draw
 * nothing; fonts 2 and 4 still draw only 32..126, as TFT_eSPI does. */

namespace {

/* The rows a bitmap font's 'H' inks, at text size 1: its capital's top and
 * bottom inside the cell, which place the TrueType capital. */
struct CapRows { int8_t top = -1, bottom = -1; };

CapRows bitmapCapRows(uint8_t font) {
    CapRows r;
    auto ink = [&](int row) {
        if (r.top < 0 || row < r.top) r.top = static_cast<int8_t>(row);
        if (row > r.bottom) r.bottom = static_cast<int8_t>(row);
    };
    const uint16_t code = 'H';
    if (font == 1) {
        for (int col = 0; col < 5; ++col) {
            uint8_t line = ::font[code * 5 + col];
            for (int row = 0; row < 8; ++row, line >>= 1) {
                if (line & 1) ink(row);
            }
        }
        return r;
    }
    const FontInfo* info = fontInfo(font);
    if (!info) return r;
    const int width = info->widths[code - 32];
    const unsigned char* data = info->glyphs[code - 32];
    if (font == 2) {
        const int rowBytes = (width + 6) / 8;
        for (int row = 0; row < info->height; ++row) {
            for (int col = 0; col < width && col < rowBytes * 8; ++col) {
                if (data[row * rowBytes + col / 8] & (0x80 >> (col % 8))) ink(row);
            }
        }
        return r;
    }
    int pc = 0;
    const int total = width * info->height;
    while (pc < total) {
        const uint8_t b = *data++;
        int run = (b & 0x7F) + 1;
        while (run-- > 0 && pc < total) {
            if (b & 0x80) ink(pc / width);
            ++pc;
        }
    }
    return r;
}

const CapRows& capRows(uint8_t font) {
    static CapRows rows[5];
    static bool done[5] = {false};
    static const CapRows none;
    if (font != 1 && font != 2 && font != 4) return none;
    if (!done[font]) {
        rows[font] = bitmapCapRows(font);
        done[font] = true;
    }
    return rows[font];
}

/* Whether this firmware font draws `code` at all, as TFT_eSPI decides. */
bool drawsCode(uint8_t font, uint16_t code) {
    if (code == 0) return false;
    if (font == 1) return code <= 255;
    return code >= 32 && code <= 126;
}

uint16_t blend565(uint16_t under, uint16_t over, uint8_t a) {
    if (a == 255) return over;
    if (a == 0) return under;
    const uint32_t ia = 255u - a;
    const uint32_t r = (((over >> 11) & 31) * a + ((under >> 11) & 31) * ia + 127) / 255;
    const uint32_t g = (((over >> 5) & 63) * a + ((under >> 5) & 63) * ia + 127) / 255;
    const uint32_t b = ((over & 31) * a + (under & 31) * ia + 127) / 255;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

}   // namespace

/* How wide FreeSans is drawn for this firmware font, in thousandths of its
 * own proportions: 1000 unless, at the bitmap font's capital height, some
 * word the screens draw would run wider than the bitmap font draws it --
 * then narrow enough that none does, and never below 85%. The screens
 * measure their boxes for the bitmap fonts' widths, so a word that ran wider
 * is cut where a board shows it whole: System Info's "Network" tab read
 * "Netwo" where a board reads "Networ". It is the WIDEST word that decides,
 * not the average: on average FreeSans matches font 2, and on words with m
 * and w it runs a tenth wider. Measured once; the log says the result. */
int32_t TFT_eSPI::ttWidthPermille(uint8_t font) const {
    static int32_t cached[5] = {0};
    if (font != 1 && font != 2 && font != 4) return 0;
    if (cached[font]) return cached[font];
    /* The screens' own words, which are what gets cut. */
    static const char* const WORDS[] = {
        "Network", "Memory", "Battery", "Settings", "Board", "About", "Scores",
        "Profiles", "Nearby", "Tap the answer", "How many objects?",
        "Which time is shown?", "Level", "Score", "Streak", "Best", "Correct",
        "Next", "Prev", "Done", "Add Player", "Guest", "Admin", "Who is playing?",
        "Power", "Sound", "Theme", "Wi-Fi", "Sync now", "Back", "Multiplication",
        "Whack A Mole", "Finger Counting", "Roman Numerals", "Backgammon",
    };
    const CapRows& rows = capRows(font);
    const int capUnits = HostText::capHeightUnits();
    if (rows.top < 0 || capUnits <= 0) return cached[font] = 1000;
    const FontInfo* info = fontInfo(font);
    const int capRowsN = rows.bottom - rows.top + 1;
    double worst = 1.0;   // FreeSans width over bitmap width, widest word
    for (const char* word : WORDS) {
        int64_t bitmap = 0, tt = 0;
        for (const char* p = word; *p; ++p) {
            const uint8_t c = static_cast<uint8_t>(*p);
            bitmap += font == 1 ? 6 : info->widths[c - 32];
            const int adv = HostText::advanceUnits(c);
            if (adv > 0) tt += adv;
        }
        const double ttPx = static_cast<double>(tt) * capRowsN / capUnits;
        if (bitmap > 0 && ttPx / bitmap > worst) worst = ttPx / bitmap;
    }
    int32_t permille = static_cast<int32_t>(1000.0 / worst);
    if (permille < 850) permille = 850;   // never squeeze a face out of shape
    std::printf("[text] font %u: FreeSans at %d.%d%% of its width\n", font,
                static_cast<int>(permille / 10), static_cast<int>(permille % 10));
    return cached[font] = permille;
}

int32_t TFT_eSPI::ttCapPx(uint8_t font) const {
    const CapRows& r = capRows(font);
    if (r.top < 0) return 0;
    return static_cast<int32_t>(r.bottom - r.top + 1) * textSize_ * scale_;
}

/* Real pixels the string advances, glyph by glyph, exactly as ttDraw() moves
 * its pen; -1 when this font is not drawn in the TrueType face. */
int32_t TFT_eSPI::ttAdvance(const char* text, uint8_t font) const {
    const int32_t cap = ttCapPx(font);
    const int32_t wide = ttWidthPermille(font);
    if (cap <= 0 || !HostText::glyph(cap, wide, 'H')) return -1;
    int32_t pen = 0;
    while (*text) {
        const uint16_t code = nextCodepoint(text);
        if (!drawsCode(font, code)) continue;
        if (const HostText::Glyph* g = HostText::glyph(cap, wide, code)) {
            pen += g->advance;
        } else if (font == 1) {
            pen += 6 * textSize_ * scale_;   // drawn from the bitmap instead
        }
    }
    return pen;
}

/* One real pixel of a glyph, at (px, py) in the rotation's own coordinates
 * times the UI scale, with the viewport and the paint clip applied to the
 * logical pixel it falls in -- what plot() does for a whole one. */
void TFT_eSPI::blendReal(int32_t px, int32_t py, uint16_t color, uint8_t a) {
    if (a == 0 || px < 0 || py < 0) return;
    const int32_t d = scale_;
    const int32_t lx = px / d, ly = py / d;   // absolute logical
    if (clipOn_ && (lx < clipX_ || ly < clipY_ || lx >= clipX_ + clipW_ || ly >= clipY_ + clipH_)) return;
    if (lx < vpX_ || ly < vpY_ || lx >= vpX_ + vpW_ || ly >= vpY_ + vpH_) return;
    const int32_t pw = static_cast<int32_t>(frameW_) * d;
    const int32_t ph = static_cast<int32_t>(frameH_) * d;
    int32_t fx = 0, fy = 0;
    switch (rotation_) {   // toFrame()'s transforms, at the real resolution
        case 1: fx = px; fy = py; break;
        case 3: fx = pw - 1 - px; fy = ph - 1 - py; break;
        case 2: fx = pw - 1 - py; fy = px; break;
        default: fx = py; fy = ph - 1 - px; break;
    }
    if (fx < 0 || fy < 0 || fx >= pw || fy >= ph) return;
    uint16_t& at = frame_[static_cast<size_t>(fy) * pw + fx];
    at = blend565(at, color, a);
    ++frameGen_;
}

void TFT_eSPI::blendArt(int32_t x, int32_t y, int32_t dx, int32_t dy, const uint8_t* alpha,
                        int32_t w, int32_t h, uint16_t color) {
    if (!alpha || w <= 0 || h <= 0) return;
    const int32_t ax = vpDatum_ ? x + vpX_ : x;
    const int32_t ay = vpDatum_ ? y + vpY_ : y;
    const int32_t px = ax * scale_ + dx, py = ay * scale_ + dy;
    for (int32_t j = 0; j < h; ++j) {
        const uint8_t* row = alpha + j * w;
        for (int32_t i = 0; i < w; ++i) {
            if (row[i]) blendReal(px + i, py + j, color, row[i]);
        }
    }
}

/* Draw `text` with its cell's top-left at logical (x, y), already moved for
 * the datum; returns the logical advance. */
int32_t TFT_eSPI::ttDraw(const char* text, int32_t x, int32_t y, uint8_t font, int32_t w,
                         int32_t h) {
    const int32_t cap = ttCapPx(font);
    const int32_t wide = ttWidthPermille(font);
    const int32_t d = scale_;
    if (textFg_ != textBg_ && w > 0 && h > 0) fillRectQuiet(x, y, w, h, textBg_);
    const int32_t ax = vpDatum_ ? x + vpX_ : x;
    const int32_t ay = vpDatum_ ? y + vpY_ : y;
    const int32_t baseline = (ay + static_cast<int32_t>(capRows(font).bottom + 1) * textSize_) * d;
    int32_t pen = ax * d;
    const int32_t start = pen;
    while (*text) {
        const uint16_t code = nextCodepoint(text);
        if (!drawsCode(font, code)) continue;
        const HostText::Glyph* g = HostText::glyph(cap, wide, code);
        if (!g) {
            if (font == 1) {
                /* No such glyph in the face: the bitmap's, at the logical
                 * pixel the pen is in. */
                const uint16_t savedBg = textBg_;
                textBg_ = textFg_;   // the cell is already filled
                drawChar(code, x + (pen - start) / d, y, font);
                textBg_ = savedBg;
                pen += 6 * textSize_ * d;
            }
            continue;
        }
        if (g->w > 0) {
            const int32_t gx = pen + g->xoff, gy = baseline + g->yoff;
            const int32_t lx0 = gx / d - (vpDatum_ ? vpX_ : 0);
            const int32_t ly0 = gy / d - (vpDatum_ ? vpY_ : 0);
            forgetMarksUnder(lx0, ly0, (g->w + d - 1) / d + 1, (g->h + d - 1) / d + 1);
            for (int32_t j = 0; j < g->h; ++j) {
                const uint8_t* row = g->alpha.data() + j * g->w;
                for (int32_t i = 0; i < g->w; ++i) {
                    if (row[i]) blendReal(gx + i, gy + j, textFg_, row[i]);
                }
            }
        }
        pen += g->advance;
    }
    return (pen - start + d - 1) / d;
}

int16_t TFT_eSPI::fontHeight(int16_t font) const {
    if (font == 1) return static_cast<int16_t>(8 * textSize_);
    const FontInfo* info = fontInfo(static_cast<uint8_t>(font));
    return info ? static_cast<int16_t>(info->height * textSize_) : 0;
}

int16_t TFT_eSPI::textWidth(const char* text, uint8_t font) const {
    if (!text) return 0;
    /* The same advances ttDraw() moves by, so a width measured here is the
     * width drawn: Ui::fitted(), wrapping and centring depend on it. */
    const int32_t real = ttAdvance(text, font);
    if (real >= 0) return static_cast<int16_t>((real + scale_ - 1) / scale_);
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
                    fillRectQuiet(x + col * size, y + row * size, size, size, textFg_);
                } else if (fillBg) {
                    fillRectQuiet(x + col * size, y + row * size, size, size, textBg_);
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
                    fillRectQuiet(x + col * size, y + row * size, size, size, textFg_);
                } else if (fillBg) {
                    fillRectQuiet(x + col * size, y + row * size, size, size, textBg_);
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
                fillRectQuiet(x + col * size, y + row * size, size, size, textFg_);
            } else if (fillBg) {
                fillRectQuiet(x + col * size, y + row * size, size, size, textBg_);
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
    if (ttAdvance(text, font) >= 0) {
        advance = ttDraw(text, x, y, font, w, h);
    } else {
        const char* s = text;
        while (*s) {
            const uint16_t code = nextCodepoint(s);
            advance += drawChar(code, x + advance, y, font);
        }
    }
    notePagerLabel(text, x, y, advance, h);
    if (advance > 0 && h > 0) noteIconPrim(Prim::Text, x, y, x + advance - 1, y + h - 1);
    return static_cast<int16_t>(advance);
}

int16_t TFT_eSPI::drawCentreString(const char* text, int32_t x, int32_t y, uint8_t font) {
    const uint8_t saved = textDatum_;
    textDatum_ = TC_DATUM;
    const int16_t w = drawString(text, x, y, font);
    textDatum_ = saved;
    return w;
}
