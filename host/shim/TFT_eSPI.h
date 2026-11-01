// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* A TFT_eSPI-shaped panel that draws into memory.
 *
 * Board owns one of these exactly as it owns the real driver on a board, so
 * TftRenderer, the HAL and every screen above them run unchanged. Only the
 * calls the firmware makes are here.
 *
 * The pixels live in ONE frame, 320x240 RGB565, in the landscape orientation
 * of rotation 1 -- the way the host board's panel is "mounted" -- and the host
 * window shows that frame as it stands. setRotation() changes how logical
 * coordinates land in it, with the same four transforms Board::pollTouch()
 * undoes for touch, so a portrait screen appears turned on a landscape window
 * exactly as it would on a landscape device.
 *
 * Text uses TFT_eSPI's own font data (fetched with the library at the pinned
 * version, never copied into this tree) and follows its rules where screens
 * depend on them: datum offsets from textWidth() and fontHeight(), fonts 2
 * and 4 limited to ASCII 32..126, a background cell painted when foreground
 * and background differ, and fonts this build does not load -- 6, 7 and 8 --
 * drawing nothing and measuring zero, as they do on the device. */

#include <cstddef>
#include <cstdint>
#include "WString.h"

#define TFT_BLACK       0x0000
#define TFT_NAVY        0x000F
#define TFT_DARKGREEN   0x03E0
#define TFT_DARKCYAN    0x03EF
#define TFT_MAROON      0x7800
#define TFT_PURPLE      0x780F
#define TFT_OLIVE       0x7BE0
#define TFT_LIGHTGREY   0xD69A
#define TFT_DARKGREY    0x7BEF
#define TFT_BLUE        0x001F
#define TFT_GREEN       0x07E0
#define TFT_CYAN        0x07FF
#define TFT_RED         0xF800
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_WHITE       0xFFFF
#define TFT_ORANGE      0xFDA0
#define TFT_GREENYELLOW 0xB7E0
#define TFT_PINK        0xFE19
#define TFT_BROWN       0x9A60
#define TFT_GOLD        0xFEA0
#define TFT_SILVER      0xC618
#define TFT_SKYBLUE     0x867D
#define TFT_VIOLET      0x915C

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define CL_DATUM 3
#define MC_DATUM 4
#define CC_DATUM 4
#define MR_DATUM 5
#define CR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8
#define L_BASELINE 9
#define C_BASELINE 10
#define R_BASELINE 11

class TFT_eSPI {
public:
    static constexpr int16_t FRAME_W = 320;
    static constexpr int16_t FRAME_H = 240;

    TFT_eSPI(int16_t w = 240, int16_t h = 320) { (void)w; (void)h; }

    void init(uint8_t tc = 0) { (void)tc; begin(); }
    void begin(uint8_t tc = 0);
    void setRotation(uint8_t r);
    uint8_t getRotation() const { return rotation_; }
    void invertDisplay(bool invert) { inverted_ = invert; }
    void setTextWrap(bool wrapX, bool wrapY = false) { (void)wrapX; (void)wrapY; }

    /* Panel commands. Sleep In/Out and Display On/Off are the only ones the
     * firmware sends, and the window honours them by going dark. */
    void writecommand(uint8_t c);
    void writedata(uint8_t d) { (void)d; }
    uint8_t readcommand8(uint8_t cmd, uint8_t index = 0) { (void)cmd; (void)index; return 0; }
    uint8_t readcommand(uint8_t cmd) { (void)cmd; return 0; }

    int16_t width() const;
    int16_t height() const;

    void fillScreen(uint32_t color);
    void drawPixel(int32_t x, int32_t y, uint32_t color);
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color);
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color);
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color);
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color);
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
    void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color);
    void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color);
    void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                      uint32_t color);
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                      uint32_t color);

    void setTextDatum(uint8_t datum) { textDatum_ = datum; }
    uint8_t getTextDatum() const { return textDatum_; }
    void setTextColor(uint16_t fg) { textFg_ = fg; textBg_ = fg; }
    void setTextColor(uint16_t fg, uint16_t bg, bool bgFill = false) {
        (void)bgFill;
        textFg_ = fg;
        textBg_ = bg;
    }
    void setTextSize(uint8_t size) { textSize_ = size < 1 ? 1 : size; }
    void setTextFont(uint8_t font) { textFont_ = font; }
    void setTextPadding(uint16_t pad) { (void)pad; }
    int16_t fontHeight(int16_t font) const;
    int16_t fontHeight() const { return fontHeight(textFont_); }
    int16_t textWidth(const char* text, uint8_t font) const;
    int16_t textWidth(const char* text) const { return textWidth(text, textFont_); }
    int16_t textWidth(const String& text, uint8_t font) const { return textWidth(text.c_str(), font); }
    int16_t drawString(const char* text, int32_t x, int32_t y, uint8_t font);
    int16_t drawString(const char* text, int32_t x, int32_t y) { return drawString(text, x, y, textFont_); }
    int16_t drawString(const String& text, int32_t x, int32_t y, uint8_t font) {
        return drawString(text.c_str(), x, y, font);
    }
    int16_t drawCentreString(const char* text, int32_t x, int32_t y, uint8_t font);

    void setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool vpDatum = true);
    void resetViewport();

    void startWrite() {}
    void endWrite() {}
    void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h);
    bool getSwapBytes() const { return swapBytes_; }
    void setSwapBytes(bool swap) { swapBytes_ = swap; }
    void pushPixels(const void* data, uint32_t len);
    void pushColor(uint16_t color);

    /* What the host window shows. */
    const uint16_t* frame() const { return frame_; }

    /* Where the screen's "Prev" (next=false) or "Next" button is, in the
     * rotation-1 frame, or false when none is showing. The host's page keys
     * press it. Read off the labels as they are drawn, because every pager in
     * the firmware is Ui::drawPagerButton() with those words; forgotten when
     * the screen is cleared, the rotation changes, or something is filled over
     * the label without it being drawn again. */
    bool pagerTarget(bool next, int16_t& fx, int16_t& fy) const;

    /* The scrolling list on screen, in the rotation-1 frame -- what the host's
     * cursor needs to scroll it by dragging, as a finger would. Read off
     * RowList's scroll bar as it is drawn (a 6px track, its outline, and a
     * thumb inside it), which is the only scroll bar the firmware has. False
     * when no list on screen is long enough to have one, and in portrait,
     * where the list's up and down are the frame's left and right. */
    struct ScrollArea {
        int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // the list, inclusive
        int16_t grabX = 0;                        // a column on the track: no control sits there
        bool moreAbove = false;                   // as the screen is seen
        bool moreBelow = false;
    };
    bool scrollArea(ScrollArea& out) const;

    /* Where the header's Settings gear, padlock or speaker is, in the
     * rotation-1 frame, or false when it is not on screen. The host's M, R2
     * and L2 press them. Each is recognised by how Ui draws it (the gear's
     * eight teeth and hub, the padlock's shackle, body and keyhole, the
     * speaker's two cone triangles) inside the rect LauncherLayout gives it,
     * in the top bar or the launcher header, and forgotten when that rect is
     * painted over whole. */
    enum class Icon : uint8_t { Settings, Lock, Mute };
    bool iconTarget(Icon icon, int16_t& fx, int16_t& fy) const;

    /* The tab after the selected one in the screen's tab strip, wrapping to
     * the first, in the rotation-1 frame; false when no strip is showing.
     * Read off Ui::drawTab() as it draws: a radius-6 fill, then the 7px fill
     * that squares off its bottom, and the selected tab 4px taller than the
     * rest. The host's M presses it. */
    bool nextTabTarget(int16_t& fx, int16_t& fy) const;
    bool displayOn() const { return displayOn_ && !sleeping_; }
    bool asleep() const { return sleeping_; }
    bool inverted() const { return inverted_; }

private:
    /* Logical (x, y) at the current rotation into the frame, or false when it
     * falls outside the clip. Every pixel the shim writes passes through here,
     * so clipping is exact and per pixel -- which is also what TFT_eSPI's
     * drop-the-whole-glyph-past-the-edge rule reduces to for anything
     * visible. */
    bool toFrame(int32_t x, int32_t y, int32_t& fx, int32_t& fy) const;
    void plot(int32_t x, int32_t y, uint16_t color);
    void span(int32_t x0, int32_t x1, int32_t y, uint16_t color);
    void circleQuarters(int32_t x0, int32_t y0, int32_t r, uint8_t corners, uint16_t color);
    void fillCircleHalves(int32_t x0, int32_t y0, int32_t r, uint8_t sides, int32_t stretch,
                          uint16_t color);
    int16_t drawChar(uint16_t code, int32_t x, int32_t y, uint8_t font);
    void notePagerLabel(const char* text, int32_t x, int32_t y, int32_t w, int32_t h);
    void forgetMarks();
    void forgetMarksUnder(int32_t x, int32_t y, int32_t w, int32_t h);

    /* A pager label's centre: logical and absolute (for fills, which arrive
     * in logical coordinates) and in the frame (for the tap). */
    struct PagerMark {
        bool valid = false;
        int32_t lx = 0, ly = 0;
        int16_t fx = 0, fy = 0;
    };
    PagerMark pager_[2];   // [0] Prev, [1] Next

    /* The scroll bar, in absolute logical coordinates at `rot`. `pending` is
     * a track filled but not yet outlined; the outline confirms it, and the
     * thumb drawn inside it says how far the list has scrolled. */
    struct ScrollMark {
        bool pending = false;
        bool valid = false;
        uint8_t rot = 1;
        int32_t x = 0, y = 0, w = 0, h = 0;      // the track
        int32_t thumbTop = 0, thumbBottom = 0;
    };
    ScrollMark scroll_;
    void noteScrollFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r);
    void noteScrollOutline(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r);

    /* The icon trackers: three places each icon can be (the top bar, the
     * launcher header in landscape, and in portrait), with a count of the
     * primitives seen inside each since it was last painted over. */
    enum class Prim : uint8_t { Line, CircleOutline, RoundOutline, RoundFill, CircleFill, Triangle };
    struct IconTrack {
        int32_t x = 0, y = 0, w = 0, h = 0;
        uint8_t count[6] = {0};
    };
    IconTrack icons_[3][3];
    uint8_t iconsRot_ = 0xFF;
    void placeIcons();
    void noteIconPrim(Prim prim, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void forgetIconsUnder(int32_t x, int32_t y, int32_t w, int32_t h);

    /* Tabs, in absolute logical coordinates. `pendingTab_` is a radius-6 fill
     * waiting for the square-off fill that makes it a tab. */
    struct TabMark {
        bool valid = false;
        uint8_t rot = 1;
        int32_t x = 0, top = 0, w = 0, bottom = 0;
    };
    static constexpr int MAX_TABS = 8;
    TabMark tabs_[MAX_TABS];
    TabMark pendingTab_;
    void noteTabFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r);
    void noteTabSquare(int32_t x, int32_t y, int32_t w, int32_t h);
    void logicalToFrame(int32_t x, int32_t y, int16_t& fx, int16_t& fy) const;

    uint16_t frame_[FRAME_W * FRAME_H] = {0};
    uint8_t rotation_ = 1;
    bool inverted_ = false;
    bool sleeping_ = false;
    bool displayOn_ = true;
    bool swapBytes_ = false;

    int32_t vpX_ = 0, vpY_ = 0, vpW_ = FRAME_W, vpH_ = FRAME_H;
    bool vpDatum_ = false;
    bool vpActive_ = false;

    int32_t winX_ = 0, winY_ = 0, winW_ = 0, winH_ = 0, winAt_ = 0;

    uint8_t textDatum_ = TL_DATUM;
    uint16_t textFg_ = TFT_WHITE;
    uint16_t textBg_ = TFT_WHITE;
    uint8_t textSize_ = 1;
    uint8_t textFont_ = 1;
};
