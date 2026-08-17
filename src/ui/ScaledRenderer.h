#pragma once

#include <math.h>
#include "BoardConfig.h"
#include "ui/Renderer.h"

namespace Ui {

/* Wraps another Renderer and stretches shape/layout coordinates from the
 * fixed 320x240 game canvas onto a physically bigger panel -- built once for
 * the 4" board (480x320) but computed from the actual panel size, not that
 * board specifically, so a future board with yet another resolution gets the
 * same treatment automatically. Two things deliberately do NOT scale:
 *
 * - Images (setAddrWindow's w/h, pushPixels' pixel data) stay native size.
 *   Only the address window's origin moves, so a flag keeps its real pixel
 *   dimensions but lands in the same relative spot on the bigger screen.
 * - Text renders at a fixed whole-number size (TFT_eSPI's font renderer has
 *   no arbitrary-scale option), not proportionally stretched; only its
 *   position moves with everything else.
 *
 * On a panel that is already exactly 320x240 (the 2.8" board), scaleX_ and
 * scaleY_ are both 1.0 and textScale_ is 1, so every method here is a
 * value-preserving passthrough -- this class changes nothing there. */
class ScaledRenderer final : public Renderer {
public:
    /* physicalWidth/Height default to the logical canvas (scale 1.0, a
     * no-op) so this is safe to construct before Board::begin() has run;
     * call configure() once the real panel size is known. */
    explicit ScaledRenderer(Renderer& inner, uint8_t textScale = 1)
        : inner_(inner), textScale_(textScale) {}

    void configure(int16_t physicalWidth, int16_t physicalHeight) {
        scaleX_ = static_cast<float>(physicalWidth) / SCREEN_WIDTH;
        scaleY_ = static_cast<float>(physicalHeight) / SCREEN_HEIGHT;
    }

    // Games only ever know about the fixed logical canvas.
    int16_t width() override { return SCREEN_WIDTH; }
    int16_t height() override { return SCREEN_HEIGHT; }

    void fillScreen(uint16_t color) override { inner_.fillScreen(color); }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override {
        inner_.fillRect(sx(x), sy(y), sw(w), sh(h), color);
    }
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override {
        inner_.drawRect(sx(x), sy(y), sw(w), sh(h), color);
    }
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h,
                       int32_t radius, uint16_t color) override {
        inner_.fillRoundRect(sx(x), sy(y), sw(w), sh(h), sr(radius), color);
    }
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h,
                       int32_t radius, uint16_t color) override {
        inner_.drawRoundRect(sx(x), sy(y), sw(w), sh(h), sr(radius), color);
    }
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color) override {
        inner_.drawFastHLine(sx(x), sy(y), sw(w), color);
    }
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t color) override {
        inner_.drawFastVLine(sx(x), sy(y), sh(h), color);
    }
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                  uint16_t color) override {
        inner_.drawLine(sx(x0), sy(y0), sx(x1), sy(y1), color);
    }
    void drawPixel(int32_t x, int32_t y, uint16_t color) override {
        inner_.drawPixel(sx(x), sy(y), color);
    }
    void drawCircle(int32_t x, int32_t y, int32_t radius, uint16_t color) override {
        inner_.drawCircle(sx(x), sy(y), sr(radius), color);
    }
    /* fillCircle uses the averaged radius (sr) which draws a true circle.
     * fillTriangle scales x and y independently, so wedge points computed
     * with the same logical radius land on a different ellipse. To keep
     * pie-slice fills aligned with their enclosing circle, fillCircle must
     * also use the averaged radius -- which it already does via sr(). The
     * games' fillSlice functions pass the same logical radius to both, so
     * they stay consistent as long as sr() is used here. */
    void fillCircle(int32_t x, int32_t y, int32_t radius, uint16_t color) override {
        inner_.fillCircle(sx(x), sy(y), sr(radius), color);
    }
    void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint16_t color) override {
        inner_.drawTriangle(sx(x0), sy(y0), sx(x1), sy(y1), sx(x2), sy(y2), color);
    }
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint16_t color) override {
        inner_.fillTriangle(sx(x0), sy(y0), sx(x1), sy(y1), sx(x2), sy(y2), color);
    }

    void setTextDatum(uint8_t datum) override { inner_.setTextDatum(datum); }
    void setTextColor(uint16_t fg) override { inner_.setTextColor(fg); }
    void setTextColor(uint16_t fg, uint16_t bg, bool bgFill = false) override {
        inner_.setTextColor(fg, bg, bgFill);
    }
    void setTextSize(uint8_t size) override { inner_.setTextSize(size); }
    int16_t drawString(const char* text, int32_t x, int32_t y, uint8_t font = 2) override {
        inner_.setTextSize(textScale_);
        return inner_.drawString(text, sx(x), sy(y), font);
    }
    int16_t textWidth(const char* text, uint8_t font = 2) override {
        inner_.setTextSize(textScale_);
        return inner_.textWidth(text, font);
    }

    void setViewport(int32_t x, int32_t y, int32_t w, int32_t h,
                     bool vpDatum = true) override {
        inner_.setViewport(sx(x), sy(y), sw(w), sh(h), vpDatum);
    }
    void resetViewport() override { inner_.resetViewport(); }

    void startWrite() override { inner_.startWrite(); }
    void endWrite() override { inner_.endWrite(); }
    // Only the origin moves -- w/h stay native pixel dimensions (see class comment).
    void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) override {
        inner_.setAddrWindow(sx(x), sy(y), w, h);
    }
    bool getSwapBytes() override { return inner_.getSwapBytes(); }
    void setSwapBytes(bool swap) override { inner_.setSwapBytes(swap); }
    void pushPixels(uint16_t* data, uint32_t len) override { inner_.pushPixels(data, len); }

    /* Average of the two axis scales -- used for radii (so circles stay
     * circles) and for choosing an integer image scale. Returns 1.0 on the
     * 2.8" board where scaleX_ == scaleY_ == 1.0. */
    float imageScale() const override { return (scaleX_ + scaleY_) * 0.5f; }
    float imageScaleX() const override { return scaleX_; }
    float imageScaleY() const override { return scaleY_; }
    Renderer* physicalRenderer() override { return &inner_; }

private:
    static int32_t roundf32(float v) { return static_cast<int32_t>(lroundf(v)); }
    int32_t sx(int32_t v) const { return roundf32(v * scaleX_); }
    int32_t sy(int32_t v) const { return roundf32(v * scaleY_); }
    int32_t sw(int32_t v) const { return roundf32(v * scaleX_); }
    int32_t sh(int32_t v) const { return roundf32(v * scaleY_); }
    // Radii have one dimension to scale non-uniform axes into -- split the
    // difference so circles stay circles instead of becoming ellipses.
    int32_t sr(int32_t v) const { return roundf32(v * (scaleX_ + scaleY_) * 0.5f); }

    Renderer& inner_;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    uint8_t textScale_;
};

}  // namespace Ui
