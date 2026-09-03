#pragma once

#include <Arduino.h>
#include "BoardConfig.h"
#include "ui/Renderer.h"

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

class Board;

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
void clear(Ui::Renderer& tft);
void drawTopBar(Board& board, const String& title);
void drawHomeIcon(Ui::Renderer& tft, const Rect& r);

/* Padlock, drawn to fill `r`. `bg` is whatever is behind it: the keyhole is
 * punched back out in that colour, which is what makes the glyph read as a
 * lock at 18px rather than as a filled blob. Used by the top bar, by the
 * launcher header and at four times the size by the lock screen, so it takes
 * its proportions from the rect rather than from constants. */
void drawLockIcon(Ui::Renderer& tft, const Rect& r, uint16_t color, uint16_t bg);
void drawGearIcon(Ui::Renderer& tft, const Rect& r, uint16_t color = TFT_WHITE);

/* Small badge shown beside the clock: a tick when the time came from NTP, a
 * warning dot when it is still the free-running build-time estimate. Drawn at
 * (cx, cy) as a centre point; about 12px across. */
void drawSyncBadge(Ui::Renderer& tft, int16_t cx, int16_t cy, bool synced, uint16_t bg);

/* Wi-Fi state beside the clock: signal arcs when associated, greyed with a red
 * slash when not. Centred on (cx, cy), about 16px across. */
void drawWifiBadge(Ui::Renderer& tft, int16_t cx, int16_t cy, uint16_t bg);

/* What the badge should say about where the power is coming from. Kept as a
 * Ui-level type rather than Board::ChargingState so this header stays free of
 * the HAL; powerHint() below is the one place the two meet. */
enum class PowerHint : uint8_t {
    OnBattery,   // running down: the fill colour is the whole story
    Charging,    // cable in and climbing: bolt over the fill
    Charged      // cable in, topped off: bolt over a full fill
};

/** Read the board's current source and charge verdict as a PowerHint. */
PowerHint powerHint(Board& board);

/* Battery state beside Wi-Fi: a battery shell holding the percentage as
 * numerals, a bordered two-pixel level gauge along its inside bottom, and a
 * lightning bolt inside the shell while the charger is attached. This is the iOS /
 * Android status-bar pattern: eleven pixels of fill is not a number anybody
 * can read, so the badge says both -- the digits for the parent, the colour
 * for the player.
 *
 * At or below Board::BATTERY_LOW_PERCENT the *shell and the digits* go red.
 * The outline changing colour is what makes "charge me" visible across the
 * room, and it only works while it is rare, so every other level leaves the
 * shell neutral and lets the gauge carry green/amber. That emphasis is
 * dropped the moment the charger is attached, because by then the user has
 * already done the thing it was asking for.
 *
 * THE BADGE IS VARIABLE WIDTH -- 22px for a two-digit charge, 35px for "100"
 * while plugged in. Lay out from batteryBadgeWidth() instead of assuming a
 * size; `cx` is the centre of the whole badge, terminal nub included. */
void drawBatteryBadge(Ui::Renderer& tft, int16_t cx, int16_t cy, int8_t percent, PowerHint power, uint16_t bg);

/** Width the badge will occupy in this state. Lay headers out from the right. */
int16_t batteryBadgeWidth(Ui::Renderer& tft, int8_t percent, PowerHint power);

/* BLE beacon indicator: the Bluetooth rune, drawn only while the radio is
 * actually advertising. There is no "off" variant on purpose -- an icon that is
 * always present but sometimes greyed makes "is it transmitting?" a question of
 * shade, and that is the one question this icon exists to answer at a glance.
 * Centred on (cx, cy), 10x16. */
void drawBleBadge(Ui::Renderer& tft, int16_t cx, int16_t cy, uint16_t bg);

/* Transient notification strip, painted over the top of whatever header is
 * already there. Nearby play raises these when another console arrives or
 * beats a record; the runtime paints one for a few seconds and then repaints
 * the header underneath, so the strip is genuinely removed rather than
 * accumulating into a list nobody clears.
 *
 * Takes the full width at TOP_BAR_HEIGHT, so it works over the top bar and
 * over the launcher's taller header alike. */
void drawNotification(Ui::Renderer& tft, const char* text);

/** True when the station interface is associated. */
bool wifiUp();
/* Browser-style tab. The active one is rounded on top only, filled with the
 * page colour and merged into the content below by leaving its bottom edge
 * open; inactive tabs sit lower, darker and separated by the divider line.
 * Draw the tabs first, then call drawTabBaseline(). */
/* Web-style slider: rounded track, filled portion, round handle.
 * `pct` and the returned value are both in [minPct, maxPct] -- the full travel
 * maps to that range, so the value can never fall outside it.
 *
 * `maxPct` exists because a setting can have a ceiling as well as a floor, and
 * the two must be expressed the same way. Volume is capped at
 * `Board::AUDIO_VOLUME_MAX` for a handheld held near a child's ears; the
 * honest way to show that is a slider whose travel ends at 80 and a readout
 * that says 80, not a full-width slider relabelled so that 80 reads as 100.
 * A control that lies about its range is worse than one with a shorter range. */
void drawSlider(Ui::Renderer& tft, const Rect& r, uint8_t pct, uint8_t minPct,
                uint8_t maxPct = 100);
uint8_t sliderValueAt(const Rect& r, int16_t x, uint8_t minPct, uint8_t maxPct = 100);

void drawTab(Ui::Renderer& tft, const Rect& r, const String& label, bool active);
void drawTabBaseline(Ui::Renderer& tft, int16_t y, int16_t x0, int16_t x1,
                     const Rect& activeTab);

/* Prev/Next style button that visibly disables. Several screens greyed only
 * the fill and left the label at full strength, so a dead button still looked
 * live; routing them all through here keeps that consistent. */
void drawPagerButton(Ui::Renderer& tft, const Rect& r, const String& label, bool enabled);

void drawButton(Ui::Renderer& tft, const Rect& r, const String& label, uint16_t fill, uint16_t outline, uint16_t text, bool pressed = false, uint8_t font = 2);
/* Truncate `text` to fit `maxW` at `font`, ending in '.' when it was cut.
 * Header and row values both need this; System Info had its own copy. */
String fitted(Ui::Renderer& tft, const String& text, int16_t maxW, uint8_t font);

void drawLabel(Ui::Renderer& tft, const Rect& r, const String& text, uint16_t color, uint8_t font = 2, Align align = Align::Left);
int16_t drawWrappedText(Ui::Renderer& tft, const String& text, const Rect& r, uint16_t color, uint8_t font = 2, Align align = Align::Left);
/* Smooth semicircular hop from x1 to x2, peaking `height` above baseY.
 * Plotted as short chords around a half ellipse -- drawing two straight lines
 * to a midpoint (as the number line game did) renders a triangle, not an arc. */
void drawHopArc(Ui::Renderer& tft, int16_t x1, int16_t x2, int16_t baseY,
                int16_t height, uint16_t color, bool arrowAtEnd = true);

void drawTriangleShape(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color, bool filled);
void drawStarShape(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color, bool filled);

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
void drawCountryImage(Ui::Renderer& tft, const void* img, int16_t x, int16_t y, uint16_t bgColor);

/** Draw centred inside `r`, at native size. Returns false if img was null. */
bool drawCountryImageCentred(Ui::Renderer& tft, const void* img, const Rect& r, uint16_t bgColor);

/**
 * Draw centred inside `r` using only the image's alpha, painted in `inkColor`.
 * Map outlines store a 16-step alpha ramp, so this recolours them at draw time.
 */
bool drawCountryImageTinted(Ui::Renderer& tft, const void* img, const Rect& r,
                            uint16_t bgColor, uint16_t inkColor);

/** Integer-scaled draw (2x, 3x...) centred in `r`, nearest-neighbour. */
bool drawCountryImageScaled(Ui::Renderer& tft, const void* img, const Rect& r,
                            uint16_t bgColor, uint8_t scale);

enum class Theme { Dark, Light };
void setTheme(Theme t);
Theme currentTheme();
}
