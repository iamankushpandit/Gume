#pragma once

#include <math.h>

#include "BoardConfig.h"
#include "ui/Renderer.h"

namespace Ui {

/* Wraps another Renderer and stretches shape/layout coordinates from the fixed
 * game canvas (GAME_CANVAS_WIDTH x GAME_CANVAS_HEIGHT) onto a physically
 * bigger panel. It is computed from the panel's actual size rather than from
 * any one board, so a future board with yet another resolution gets the same
 * treatment without touching this file.
 *
 * This exists because the playable games are authored against a fixed canvas
 * and position everything by arithmetic on it -- see BoardConfig.h, where that
 * canvas is stated rather than derived. Scaling here is what lets a bigger
 * panel fill its screen without every game learning a second layout.
 *
 * Two things deliberately do NOT scale:
 *
 * - Images (setAddrWindow's w/h, pushPixels' pixel data) stay native size.
 *   Only the address window's origin moves, so a flag keeps its real pixel
 *   dimensions but lands in the same relative spot on the bigger screen.
 * - Text renders at a fixed whole-number size (TFT_eSPI's font renderer has no
 *   arbitrary-scale option), not proportionally stretched; only its position
 *   moves with everything else.
 *
 * On a panel that is exactly the canvas size, scaleX_ and scaleY_ are both 1.0
 * and textScale_ is 1, so every method here is a value-preserving passthrough
 * and this class changes nothing. */
class ScaledRenderer final : public Renderer {
public:
    using Renderer::drawString;
    using Renderer::textWidth;

    /* Constructed at scale 1.0 -- a no-op -- so it is safe to build before
     * Board::begin() has run; call configure() once the panel size is known. */
    explicit ScaledRenderer(Renderer& inner, uint8_t textScale = 1)
        : inner_(inner), textScale_(textScale) {}

    void configure(int16_t physicalWidth, int16_t physicalHeight) {
        scaleX_ = static_cast<float>(physicalWidth) / GAME_CANVAS_WIDTH;
        scaleY_ = static_cast<float>(physicalHeight) / GAME_CANVAS_HEIGHT;
    }

    /* Scale directly, for callers that are not stretching the game canvas.
     *
     * The launcher icons use this. They are vector art with their geometry
     * written inline across thirty-odd cases, so there is no size argument to
     * pass and adding one would mean editing every icon. Drawing them through
     * a renderer scaled by s, with the centre point divided by s, scales the
     * art about that centre and leaves the icon code untouched. */
    void setScale(float x, float y) {
        scaleX_ = x;
        scaleY_ = y;
    }

    // Games only ever know about the fixed logical canvas.
    int16_t width() override { return GAME_CANVAS_WIDTH; }
    int16_t height() override { return GAME_CANVAS_HEIGHT; }

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
    /* Radii go through sr(), the averaged scale, so a circle stays a circle
     * rather than becoming an ellipse. fillTriangle scales x and y
     * independently, so the games' pie-slice fills -- which pass the same
     * logical radius to both -- only line up with their enclosing circle
     * because this uses sr() too. Changing one without the other detaches
     * every wedge from its circle. */
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
    /* Only the origin moves; w/h stay native pixel dimensions.
     *
     * Nothing in the firmware should reach this while scaling is active, and
     * that is deliberate. A blit is one scanline per call, so a fractional row
     * pitch makes consecutive rows overlap and gap and the image tears into
     * bands. Ui.cpp's image helpers therefore compute a physical destination
     * once and write through physicalRenderer(), which keeps rows 1:1 and
     * centres the artwork properly -- see physicalImageOrigin() there. This
     * override remains only so the interface is honoured. */
    void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) override {
        inner_.setAddrWindow(sx(x), sy(y), w, h);
    }
    bool getSwapBytes() override { return inner_.getSwapBytes(); }
    void setSwapBytes(bool swap) override { inner_.setSwapBytes(swap); }
    void pushPixels(uint16_t* data, uint32_t len) override { inner_.pushPixels(data, len); }

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
    // Radii have one dimension to scale two axes into -- split the difference
    // so circles stay circles.
    int32_t sr(int32_t v) const { return roundf32(v * (scaleX_ + scaleY_) * 0.5f); }

    Renderer& inner_;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    uint8_t textScale_;
};

}  // namespace Ui
