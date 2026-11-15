// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <TFT_eSPI.h>
#include "ui/FocusRecorder.h"
#include "ui/PaintClip.h"
#include "ui/Renderer.h"

namespace Ui {

class TftRenderer final : public Renderer {
public:
    using Renderer::drawString;
    using Renderer::textWidth;

    explicit TftRenderer(TFT_eSPI& tft) : tft_(tft) {}

    int16_t width() override { return static_cast<int16_t>(tft_.width()); }
    int16_t height() override { return static_cast<int16_t>(tft_.height()); }

    /* Every fill and outline is also reported to the d-pad's selector,
     * which finds the controls on the panel by how they are drawn
     * (FocusRecorder.h). On a board without keys the reports are empty
     * inlines and cost nothing. */
    void fillScreen(uint16_t color) override {
        tft_.fillScreen(color);
        Focus::noteScreenClear();
    }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override {
        tft_.fillRect(x, y, w, h, color);
        Focus::noteFill(x, y, w, h, 0, color);
    }
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override {
        tft_.drawRect(x, y, w, h, color);
        Focus::noteOutline(x, y, w, h, 0, color);
    }
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h,
                       int32_t radius, uint16_t color) override {
        tft_.fillRoundRect(x, y, w, h, radius, color);
        Focus::noteFill(x, y, w, h, radius, color);
    }
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h,
                       int32_t radius, uint16_t color) override {
        tft_.drawRoundRect(x, y, w, h, radius, color);
        Focus::noteOutline(x, y, w, h, radius, color);
    }
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color) override {
        tft_.drawFastHLine(x, y, w, color);
    }
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t color) override {
        tft_.drawFastVLine(x, y, h, color);
    }
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                  uint16_t color) override {
        tft_.drawLine(x0, y0, x1, y1, color);
    }
    void drawPixel(int32_t x, int32_t y, uint16_t color) override {
        tft_.drawPixel(x, y, color);
    }
    void drawCircle(int32_t x, int32_t y, int32_t radius, uint16_t color) override {
        tft_.drawCircle(x, y, radius, color);
    }
    void fillCircle(int32_t x, int32_t y, int32_t radius, uint16_t color) override {
        tft_.fillCircle(x, y, radius, color);
    }
    void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint16_t color) override {
        tft_.drawTriangle(x0, y0, x1, y1, x2, y2, color);
    }
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint16_t color) override {
        tft_.fillTriangle(x0, y0, x1, y1, x2, y2, color);
    }

    void setTextDatum(uint8_t datum) override { tft_.setTextDatum(datum); }
    void setTextColor(uint16_t fg) override { tft_.setTextColor(fg); }
    void setTextColor(uint16_t fg, uint16_t bg, bool bgFill = false) override {
        tft_.setTextColor(fg, bg, bgFill);
    }
    /* Remembered rather than pushed straight at the driver, and re-applied
     * before every text op.
     *
     * The driver instance is shared with ScaledRenderer, which sets a bigger
     * size for playable games, so a screen that never asks for a size must not
     * inherit whatever a game left behind. The first version of this forced 1x
     * inside drawString to guarantee that -- which also made setTextSize()
     * impossible to use, silently: callers set a size, the next drawString
     * threw it away, and text came out 1x with no error anywhere. Tracking it
     * here gives both properties: an explicit size is honoured, and the
     * default is always 1x rather than a leftover. */
    void setTextSize(uint8_t size) override { textSize_ = size < 1 ? 1 : size; }
    int16_t drawString(const char* text, int32_t x, int32_t y, uint8_t font = 2) override {
        tft_.setTextSize(textSize_);
        return static_cast<int16_t>(tft_.drawString(text, x, y, font));
    }
    int16_t textWidth(const char* text, uint8_t font = 2) override {
        tft_.setTextSize(textSize_);
        return static_cast<int16_t>(tft_.textWidth(text, font));
    }

    /* A screen's own viewport is remembered as well as applied, because a
     * paint clip may be laid over it -- see setPaintClip(). With no paint clip
     * these two are what they always were: straight through to the driver. */
    void setViewport(int32_t x, int32_t y, int32_t w, int32_t h,
                     bool vpDatum = true) override {
        ownSet_ = true;
        own_ = PaintClip::Box{x, y, w, h};
        ownDatum_ = vpDatum;
        applyViewport();
        /* The viewport the screen set, never the paint clip laid over it:
         * the selector's controls are read against what a screen means to
         * draw, and a clip for a notice hides pixels and nothing else. */
        Focus::noteViewport(x, y, w, h, vpDatum);
    }
    void resetViewport() override {
        ownSet_ = false;
        applyViewport();
        Focus::noteViewportReset();
    }

    /* A paint clip: while one is set, nothing drawn through this renderer
     * lands outside it. Not a screen's fills and text, not the chrome, not the
     * d-pad's selector, not the rows of an image.
     *
     * The runtime sets one for a notice (BrainoApp::settleNotice). For as
     * long as the notice is up, every painter is kept out of the strip it is
     * on, so it is painted once and nothing can scribble on it; and when it
     * goes, the screen is rendered once more with the clip turned round, so
     * that the strip is the only thing repainted. Either way the screen draws
     * exactly as it always does and knows nothing about it, which is the
     * point: forty games did not each have to learn where a notice goes.
     *
     * It is laid over the viewport a screen sets for itself, not instead of
     * it, and outlives any number of setViewport()/resetViewport() pairs. The
     * panel driver has one viewport and no second clip, so the two are
     * resolved into one here (PaintClip::resolve). The PC build's panel keeps
     * a paint clip of its own instead: it reads the d-pad's controls off what
     * a screen draws, clipped to the viewport the screen set, and a clip laid
     * over that for a notice has to hide pixels and nothing else. */
    void setPaintClip(int32_t x, int32_t y, int32_t w, int32_t h) {
        clipSet_ = true;
        clip_ = PaintClip::Box{x, y, w, h};
        applyViewport();
    }
    void clearPaintClip() {
        if (!clipSet_) return;
        clipSet_ = false;
        applyViewport();
    }

    void startWrite() override { tft_.startWrite(); }
    void endWrite() override { tft_.endWrite(); }
    /* An image goes to the panel through a raw window, which no viewport
     * clips. Under a paint clip the window is therefore held back until its
     * pixels arrive, and cut to the clip then. Every blit in Ui.cpp sends one
     * row at a time, which is what makes that a matter of skipping and
     * shortening; a taller window is not drawn at all under a clip, rather
     * than drawn across it. */
    void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) override {
        window_ = PaintClip::Box{x, y, w, h};
        if (!clipSet_) tft_.setAddrWindow(x, y, w, h);
    }
    bool getSwapBytes() override { return tft_.getSwapBytes(); }
    void setSwapBytes(bool swap) override { tft_.setSwapBytes(swap); }
    void pushPixels(uint16_t* data, uint32_t len) override {
        if (!clipSet_) {
            tft_.pushPixels(data, len);
            return;
        }
        int32_t skip = 0;
        int32_t count = 0;
        if (window_.h != 1 || len < static_cast<uint32_t>(window_.w) ||
            !PaintClip::clipRow(clip_, window_.x, window_.y, window_.w, skip, count)) {
            return;
        }
        tft_.setAddrWindow(window_.x + skip, window_.y, count, 1);
        tft_.pushPixels(data + skip, static_cast<uint32_t>(count));
    }

private:
    void applyViewport() {
#ifdef TFT_ESPI_PAINT_CLIP
        if (ownSet_) {
            tft_.setViewport(own_.x, own_.y, own_.w, own_.h, ownDatum_);
        } else {
            tft_.resetViewport();
        }
        if (clipSet_) {
            tft_.setPaintClip(clip_.x, clip_.y, clip_.w, clip_.h);
        } else {
            tft_.clearPaintClip();
        }
#else
        const PaintClip::Viewport v =
            PaintClip::resolve(ownSet_, own_, ownDatum_, clipSet_, clip_);
        if (v.set) {
            tft_.setViewport(v.box.x, v.box.y, v.box.w, v.box.h, v.datum);
        } else {
            tft_.resetViewport();
        }
#endif
    }

    TFT_eSPI& tft_;
    uint8_t textSize_ = 1;
    bool ownSet_ = false;
    bool ownDatum_ = false;
    bool clipSet_ = false;
    PaintClip::Box own_;
    PaintClip::Box clip_;
    PaintClip::Box window_;
};

}  // namespace Ui
