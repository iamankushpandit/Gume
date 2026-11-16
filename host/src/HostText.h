#pragma once

/* The typeface the Linux build draws the firmware's text in: GNU FreeSans
 * (host/fonts/), compiled into the binary and rasterised by stb_truetype
 * (host/third_party/stb/) at the panel's own resolution, anti-aliased.
 *
 * The firmware still asks for TFT_eSPI's fonts 1, 2 and 4 and lays text out
 * against their cells, so the panel shim keeps their heights and baselines
 * and asks this for a face sized to match: a capital as tall as the bitmap
 * font's capital, times the text size, times the UI scale. Widths come from
 * here too, from the same advances that draw, so Ui::fitted(), wrapping and
 * centring measure exactly what is drawn.
 *
 * Main thread only: the firmware draws from its loop, and the cache is not
 * locked. */

#include <cstdint>
#include <vector>

namespace HostText {

struct Glyph {
    int16_t w = 0, h = 0;        // the coverage bitmap
    int16_t xoff = 0, yoff = 0;  // its top-left from the pen, on the baseline
    int16_t advance = 0;         // whole real pixels to the next pen position
    std::vector<uint8_t> alpha;  // w * h, 0..255
};

/* The glyph for `code` with capitals `capPx` real pixels tall, or nullptr if
 * the font has none for it (the caller falls back to the bitmap font or draws
 * nothing, as TFT_eSPI would). Cached for the life of the program. */
const Glyph* glyph(int capPx, uint32_t code);

}   // namespace HostText
