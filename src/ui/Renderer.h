#pragma once

#include <Arduino.h>

#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#endif
#ifndef TFT_WHITE
#define TFT_WHITE 0xFFFF
#endif
#ifndef TFT_DARKGREY
#define TFT_DARKGREY 0x7BEF
#endif

#ifndef TL_DATUM
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8
#endif

namespace Ui {

/* App-facing drawing surface. It is deliberately the same small set of RGB565
 * primitives the firmware already used, just without exposing the concrete
 * panel driver to games or system apps. */
class Renderer {
public:
    virtual ~Renderer() = default;

    virtual int16_t width() = 0;
    virtual int16_t height() = 0;

    virtual void fillScreen(uint16_t color) = 0;
    virtual void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) = 0;
    virtual void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) = 0;
    virtual void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t radius, uint16_t color) = 0;
    virtual void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t radius, uint16_t color) = 0;
    virtual void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color) = 0;
    virtual void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t color) = 0;
    virtual void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                          uint16_t color) = 0;
    virtual void drawPixel(int32_t x, int32_t y, uint16_t color) = 0;
    virtual void drawCircle(int32_t x, int32_t y, int32_t radius, uint16_t color) = 0;
    virtual void fillCircle(int32_t x, int32_t y, int32_t radius, uint16_t color) = 0;
    virtual void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2, uint16_t color) = 0;
    virtual void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                              int32_t x2, int32_t y2, uint16_t color) = 0;

    virtual void setTextDatum(uint8_t datum) = 0;
    virtual void setTextColor(uint16_t fg) = 0;
    virtual void setTextColor(uint16_t fg, uint16_t bg, bool bgFill = false) = 0;
    /* Whole-number glyph scale for subsequent drawString()/textWidth() calls.
     * Implementations must apply this before every text op rather than
     * relying on it staying set, since the same driver instance is shared
     * across screens that want different scales. */
    virtual void setTextSize(uint8_t size) = 0;
    virtual int16_t drawString(const char* text, int32_t x, int32_t y, uint8_t font = 2) = 0;
    template <typename Text>
    auto drawString(const Text& text, int32_t x, int32_t y, uint8_t font = 2)
        -> decltype(text.c_str(), int16_t()) {
        return drawString(text.c_str(), x, y, font);
    }
    virtual int16_t textWidth(const char* text, uint8_t font = 2) = 0;
    template <typename Text>
    auto textWidth(const Text& text, uint8_t font = 2)
        -> decltype(text.c_str(), int16_t()) {
        return textWidth(text.c_str(), font);
    }

    virtual void setViewport(int32_t x, int32_t y, int32_t w, int32_t h,
                             bool vpDatum = true) = 0;
    virtual void resetViewport() = 0;

    virtual void startWrite() = 0;
    virtual void endWrite() = 0;
    virtual void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) = 0;
    virtual bool getSwapBytes() = 0;
    virtual void setSwapBytes(bool swap) = 0;
    virtual void pushPixels(uint16_t* data, uint32_t len) = 0;

    /* Average scale factor relative to the fixed game canvas. Returns 1.0
     * when the panel is exactly the canvas size, so nothing changes there.
     * ScaledRenderer overrides this so image-blitting helpers can choose an
     * appropriate integer scale rather than drawing at native size on a
     * physically larger panel. */
    virtual float imageScale() const { return 1.0f; }

    /* Per-axis scale factors. Used by trig-based pie/wedge drawing to
     * pre-correct radii so wedge points land on the same circle that
     * fillCircle draws (which uses the averaged radius). */
    virtual float imageScaleX() const { return 1.0f; }
    virtual float imageScaleY() const { return 1.0f; }

    /* The underlying physical renderer, bypassing any coordinate transform.
     * Image-blitting code that computes its own scaled origin and pushes raw
     * pixel data needs this -- ScaledRenderer's setAddrWindow would otherwise
     * scale coordinates that have already been scaled. Defaults to `this`. */
    virtual Renderer* physicalRenderer() { return this; }
};

}  // namespace Ui
