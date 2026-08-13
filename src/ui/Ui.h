#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "BoardConfig.h"

struct Rect {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;

    bool contains(int16_t px, int16_t py, int16_t pad = 0) const {
        return px >= x - pad && px < x + w + pad && py >= y - pad && py < y + h + pad;
    }
};

enum class Align {
    Left,
    Center
};

namespace Ui {
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);

/* Lighten or darken an RGB565 colour by a percentage (100 = unchanged).
 * Integer maths only -- used for the button bevels, so it runs per-draw. */
uint16_t shade(uint16_t color, uint8_t percent);
uint16_t bg();
uint16_t surface();
uint16_t panel();
uint16_t text();
uint16_t muted();
uint16_t outline();
uint16_t success();
uint16_t error();
uint16_t warning();
void clear(TFT_eSPI& tft);
void drawTopBar(TFT_eSPI& tft, const String& title);
void drawHomeIcon(TFT_eSPI& tft, const Rect& r);
void drawGearIcon(TFT_eSPI& tft, const Rect& r, uint16_t color = TFT_WHITE);

/* Small badge shown beside the clock: a tick when the time came from NTP, a
 * warning dot when it is still the free-running build-time estimate. Drawn at
 * (cx, cy) as a centre point; about 12px across. */
void drawSyncBadge(TFT_eSPI& tft, int16_t cx, int16_t cy, bool synced, uint16_t bg);

/* Wi-Fi state beside the clock: signal arcs when associated, greyed with a red
 * slash when not. Centred on (cx, cy), about 16px across. */
void drawWifiBadge(TFT_eSPI& tft, int16_t cx, int16_t cy, uint16_t bg);
/** True when the station interface is associated. */
bool wifiUp();
/* Browser-style tab. The active one is rounded on top only, filled with the
 * page colour and merged into the content below by leaving its bottom edge
 * open; inactive tabs sit lower, darker and separated by the divider line.
 * Draw the tabs first, then call drawTabBaseline(). */
/* Web-style slider: rounded track, filled portion, round handle.
 * `pct` and the returned value are both in [minPct, 100] -- the full travel
 * maps to that range, so the value can never fall below the floor. */
void drawSlider(TFT_eSPI& tft, const Rect& r, uint8_t pct, uint8_t minPct);
uint8_t sliderValueAt(const Rect& r, int16_t x, uint8_t minPct);

void drawTab(TFT_eSPI& tft, const Rect& r, const String& label, bool active);
void drawTabBaseline(TFT_eSPI& tft, int16_t y, int16_t x0, int16_t x1,
                     const Rect& activeTab);

/* Prev/Next style button that visibly disables. Several screens greyed only
 * the fill and left the label at full strength, so a dead button still looked
 * live; routing them all through here keeps that consistent. */
void drawPagerButton(TFT_eSPI& tft, const Rect& r, const String& label, bool enabled);

void drawButton(TFT_eSPI& tft, const Rect& r, const String& label, uint16_t fill, uint16_t outline, uint16_t text, bool pressed = false, uint8_t font = 2);
void drawLabel(TFT_eSPI& tft, const Rect& r, const String& text, uint16_t color, uint8_t font = 2, Align align = Align::Left);
int16_t drawWrappedText(TFT_eSPI& tft, const String& text, const Rect& r, uint16_t color, uint8_t font = 2, Align align = Align::Left);
/* Smooth semicircular hop from x1 to x2, peaking `height` above baseY.
 * Plotted as short chords around a half ellipse -- drawing two straight lines
 * to a midpoint (as the number line game did) renders a triangle, not an arc. */
void drawHopArc(TFT_eSPI& tft, int16_t x1, int16_t x2, int16_t baseY,
                int16_t height, uint16_t color, bool arrowAtEnd = true);

void drawTriangleShape(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color, bool filled);
void drawStarShape(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color, bool filled);

/*
 * map-n-flag image blitting.
 *
 * The library's images are 4-bit indexed and live in flash. These helpers
 * decode one row at a time into a small stack buffer and push it, so drawing a
 * flag costs ~200 bytes of scratch rather than a framebuffer.
 *
 * `img` is an mnf_img_t*, taken as void* so Ui.h does not have to pull in
 * map_n_flag.h for every translation unit. Passing nullptr is a no-op, which
 * means callers can hand over mnf_flag()/mnf_map() results unchecked.
 */

/** Draw at native size with the top-left corner at (x, y). */
void drawCountryImage(TFT_eSPI& tft, const void* img, int16_t x, int16_t y, uint16_t bgColor);

/** Draw centred inside `r`, at native size. Returns false if img was null. */
bool drawCountryImageCentred(TFT_eSPI& tft, const void* img, const Rect& r, uint16_t bgColor);

/**
 * Draw centred inside `r` using only the image's alpha, painted in `inkColor`.
 * Map outlines store a 16-step alpha ramp, so this recolours them at draw time.
 */
bool drawCountryImageTinted(TFT_eSPI& tft, const void* img, const Rect& r,
                            uint16_t bgColor, uint16_t inkColor);

/** Integer-scaled draw (2x, 3x...) centred in `r`, nearest-neighbour. */
bool drawCountryImageScaled(TFT_eSPI& tft, const void* img, const Rect& r,
                            uint16_t bgColor, uint8_t scale);

enum class Theme { Dark, Light };
void setTheme(Theme t);
Theme currentTheme();
}
