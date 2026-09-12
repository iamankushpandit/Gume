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
/* The fill of a primary action -- Settings' Network button, Wi-Fi's Scan and
 * JOIN. Pair it with onFill(accent()) for the label rather than assuming
 * white: on the themes whose accent is light, white on it cannot be read. */
uint16_t accent();
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

/* THE PRODUCT MARK, centred on (cx, cy) and painted in `colour`.
 *
 * One-bit silhouettes generated from the artwork by tools/gen_logo_mask.py.
 * Only the ink is painted -- the background is left alone -- so a caller that
 * wants the mark in a new colour simply draws it again in the same place,
 * which is what the screen saver does on every paddle hit.
 *
 * THE PRODUCT NAME IS NOT DRAWN AS TEXT ANYWHERE BUT THE LAUNCHER. It is the
 * mark, and a font-4 approximation of it is not; every other screen that used
 * to spell "Braino!" in the UI font now draws `Logo::Word` or
 * `Logo::WordSmall`, which are the real letterforms at the size that text was.
 * The launcher keeps its text because its header is laid out to the pixel
 * around a measured string, and that is a separate change.
 *
 * Sizes are fixed, because the masks are: ask logoWidth()/logoHeight() for the
 * variant you are drawing and lay out around them rather than assuming. */
enum class Logo : uint8_t {
    Badge,       // the whole artwork: brain over wordmark, for the saver
    Word,        // the wordmark alone, at the height a font-4 heading was
    WordSmall,   // the wordmark alone, at the height a font-2 row was
    BadgeMid,    // the whole artwork at two thirds: the lock screen
    Icon,        // the brain alone, beside a header row's wordmark
    IconBig,     // the brain alone, for a page corner
};

/* `cx` is where the mark should LOOK centred. That is not the middle of its
 * bitmap: the trade mark sign hangs off the right-hand end, so a mark centred
 * by its image sits visibly left of centre. To place one against a left edge,
 * pass `x + logoCentre(which)`. */
void drawLogo(Ui::Renderer& tft, int16_t cx, int16_t cy, uint16_t colour,
              Logo which = Logo::Badge);
int16_t logoWidth(Logo which = Logo::Badge);
int16_t logoHeight(Logo which = Logo::Badge);
int16_t logoCentre(Logo which = Logo::Badge);

/* Small badge shown beside the clock: a tick when the time came from NTP, a
 * warning dot when it is still the free-running build-time estimate. Drawn at
 * (cx, cy) as a centre point; about 12px across. */
void drawSyncBadge(Ui::Renderer& tft, int16_t cx, int16_t cy, bool synced, uint16_t bg);

/* Wi-Fi state beside the clock: signal arcs when associated, greyed with a red
 * slash when not. Centred on (cx, cy), about 16px across. */
void drawWifiBadge(Ui::Renderer& tft, int16_t cx, int16_t cy, uint16_t bg);

/* Battery beside Wi-Fi: a battery shell holding the percentage as numerals,
 * over a bordered two-pixel level gauge along its inside bottom. That is all
 * it says. There is no charging bolt and no charging state: the board has no
 * charge-status line, the verdict was inferred from the cell voltage, and a
 * guess shown as a fact is worse than a plain percentage.
 *
 * At or below Board::BATTERY_LOW_PERCENT the *shell and the digits* go red.
 * The outline changing colour is what makes "charge me" visible across the
 * room, and it only works while it is rare, so every other level leaves the
 * shell neutral and lets the gauge carry green/amber.
 *
 * THE BADGE IS VARIABLE WIDTH -- it grows with the digits, widest at "100".
 * Lay out from batteryBadgeWidth() instead of assuming a size; `cx` is the
 * centre of the whole badge, terminal nub included. */
void drawBatteryBadge(Ui::Renderer& tft, int16_t cx, int16_t cy, int8_t percent, uint16_t bg);

/** Width the badge will occupy at this percentage. Lay headers out from the right. */
int16_t batteryBadgeWidth(Ui::Renderer& tft, int8_t percent);

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

/* The display themes.
 *
 * Order is storage: the value is persisted as a uint8_t and Board::ThemeMode
 * mirrors it one for one, so entries may be APPENDED but never reordered or
 * removed -- a device that has been switched to Paper and then downgraded must
 * not come back up in a different theme. Count is the cycle length and must
 * stay last.
 *
 * Dark and Light are the originals. The rest were chosen for a 2.8-inch panel
 * behind a resistive overlay, read by a child, not ported from editor palettes:
 * the overlay diffuses and slightly greys everything, and RGB565 gives 5-6-5
 * bits, so the low-contrast pairings those themes are admired for turn to mud
 * here. See PALETTES in Ui.cpp. */
enum class Theme : uint8_t {
    Dark = 0,
    Light = 1,
    Midnight = 2,
    Dusk = 3,
    Paper = 4,
    HighContrast = 5,
    /* Three period looks. Named for what they evoke rather than for the
     * products they evoke: the Windows 98 and System 7 desktops and the Game
     * Boy screen are Microsoft's, Apple's and Nintendo's trade dress, and
     * NOTICE.md keeps this project on the right side of that line. Anyone who
     * recognises them will recognise them. */
    Classic = 6,        // System 7: grey desktop, white paper, black hairlines
    Silver = 7,         // Windows 98: teal ground, silver panels, navy bar
    Pocket = 8,         // the original handheld: four shades of green
    Count = 9,
};

/* drawButton() paints a drop shadow offset from the button rect by this much,
 * at the SAME width and height -- so it protrudes past the rect's right and
 * bottom edges. Anything erasing a button has to cover the rect plus this, and
 * these exist so that erase can be derived rather than typed. Getting it wrong
 * leaves an L-shaped sliver of shadow behind, which is what the launcher did
 * on a short last page. */
constexpr int16_t BUTTON_SHADOW_DX = 2;
constexpr int16_t BUTTON_SHADOW_DY = 3;

/** Text and glyphs drawn on the top bar. A palette role, not a constant. */
uint16_t barText();

/* THE INK THAT CAN BE READ ON `fill`: black or white, whichever contrasts
 * more. Use it wherever text or a glyph goes on a colour the THEME chose --
 * a launcher tile, the success badge -- rather than picking one and hoping.
 *
 * The launcher's tile labels were a fixed white over a palette fill, which is
 * how Classic came to draw white on a light grey tile at 1.3:1 and Pocket
 * white on pale green at 2.6:1. Neither is readable, and neither was visible
 * in a mock-up of the Dark theme.
 *
 * `onFillSoft` is the same decision, backed off towards the fill, for the
 * second line of a tile -- the subtitle that used to be a fixed near-white. It
 * stays the readable side of the fill, so it dims without vanishing. */
uint16_t onFill(uint16_t fill);
uint16_t onFillSoft(uint16_t fill);
/* The three launcher tile fills, cycled by slot. They are palette entries
 * because a theme built from four shades of green cannot survive three bright
 * RGB tiles on its first screen. */
uint16_t tileFill(uint8_t index);
/* Corner radius for buttons and tiles. 6 everywhere except the period themes,
 * which are square -- a rounded Windows 98 button is not a Windows 98 button. */
uint8_t cornerRadius();

void setTheme(Theme t);
Theme currentTheme();
/** Display name for the Settings label. Never null. */
const char* themeName(Theme t);
}
