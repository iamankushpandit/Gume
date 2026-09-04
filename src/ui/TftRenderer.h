#pragma once

#include <TFT_eSPI.h>
#include "ui/Renderer.h"

namespace Ui {

class TftRenderer final : public Renderer {
public:
    using Renderer::drawString;
    using Renderer::textWidth;

    explicit TftRenderer(TFT_eSPI& tft) : tft_(tft) {}

    int16_t width() override { return static_cast<int16_t>(tft_.width()); }
    int16_t height() override { return static_cast<int16_t>(tft_.height()); }

    void fillScreen(uint16_t color) override { tft_.fillScreen(color); }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override {
        tft_.fillRect(x, y, w, h, color);
    }
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) override {
        tft_.drawRect(x, y, w, h, color);
    }
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h,
                       int32_t radius, uint16_t color) override {
        tft_.fillRoundRect(x, y, w, h, radius, color);
    }
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h,
                       int32_t radius, uint16_t color) override {
        tft_.drawRoundRect(x, y, w, h, radius, color);
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

    void setViewport(int32_t x, int32_t y, int32_t w, int32_t h,
                     bool vpDatum = true) override {
        tft_.setViewport(x, y, w, h, vpDatum);
    }
    void resetViewport() override { tft_.resetViewport(); }

    void startWrite() override { tft_.startWrite(); }
    void endWrite() override { tft_.endWrite(); }
    void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) override {
        tft_.setAddrWindow(x, y, w, h);
    }
    bool getSwapBytes() override { return tft_.getSwapBytes(); }
    void setSwapBytes(bool swap) override { tft_.setSwapBytes(swap); }
    void pushPixels(uint16_t* data, uint32_t len) override { tft_.pushPixels(data, len); }

private:
    TFT_eSPI& tft_;
    uint8_t textSize_ = 1;
};

}  // namespace Ui
