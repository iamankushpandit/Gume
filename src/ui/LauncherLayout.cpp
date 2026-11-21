// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ui/LauncherLayout.h"

#include "BoardConfig.h"
#include "hal/BleBeacon.h"

namespace {

constexpr int16_t LAUNCHER_HEADER_H_TALL = 78;
constexpr int16_t LAUNCHER_HEADER_H_WIDE = 48;

}

namespace LauncherLayout {

Rect topBarSettingsRect(int16_t screenW) {
    /* Ui::CONTROL_H, like the padlock and the speaker. It was 26x24 -- the
     * one oversized glyph on a bar of 18px ones -- and the eight pixels that
     * gave back are most of what the speaker cost. */
    return Rect{static_cast<int16_t>(screenW - 8 - Ui::CONTROL_H),
                static_cast<int16_t>((TOP_BAR_HEIGHT - Ui::CONTROL_H) / 2),
                Ui::CONTROL_H, Ui::CONTROL_H};
}

/* Mute sits immediately right of the padlock, at the same size. Both are
 * derived from topBarLockRect so the three cannot drift apart, and the
 * title's left edge is derived from THIS rather than from the padlock. */
Rect topBarSpeakerRect(int16_t screenW) {
    const Rect lock = topBarLockRect(screenW);
    return Rect{static_cast<int16_t>(lock.x + lock.w + 6), lock.y,
                Ui::CONTROL_H, Ui::CONTROL_H};
}

Rect topBarHomeRect() {
    return Rect{0, 0, 32, TOP_BAR_HEIGHT};
}

Rect topBarLockRect(int16_t screenW) {
    /* Sized like a status badge rather than like the gear: it belongs to the
     * same family as the battery and Wi-Fi glyphs, and a gear-sized padlock
     * read as a control twice as important as anything else on the bar.
     *
     * At 320px it starts at 40, where home's 8px of touch slop ends, so the
     * two overlap in slop only and never in painted pixels. That was as far
     * apart as they could be on a bar with no spare room -- and it is too
     * close: Home and Lock do opposite things, and reaching for Home is the
     * commonest thing anyone does up here. Landing on the padlock instead
     * blanks the screen and demands a hold to get back, which is a harsh
     * price for a few pixels of aim.
     *
     * A wider panel has room the 320px bar never did, so spend some of it on
     * separating them. The offset is a quarter of the extra width, capped, so
     * 320px is unchanged and 480px opens a clear gap between Home's slop and
     * the padlock's. */
    const int16_t extra = static_cast<int16_t>((screenW - GAME_CANVAS_WIDTH) / 4);
    const int16_t offset = extra < 0 ? 0 : (extra > 30 ? 30 : extra);
    return Rect{static_cast<int16_t>(40 + offset), 6, 18, 18};
}

/* Derived from the padlock rather than stated, so the title cannot creep back
 * over it when the lock moves. At 320px this is 40 + 18 + 4 = 62, which is
 * exactly where the title has always started. */
int16_t topBarTitleLeft(int16_t screenW) {
    const Rect speaker = topBarSpeakerRect(screenW);
    return static_cast<int16_t>(speaker.x + speaker.w + 4);
}

/* THE LAUNCHER HEADER'S STATUS GROUP, both orientations -- see the header
 * for the whole rule. The mark and the copyright are fixed; everything the
 * header reports is placed here, packed from the right edge out of only what
 * is there, so the drawing and every hit test read the same numbers.
 *
 * Top row: the clock and its sync check, which belong together and stay
 * together, then the gear for the admin. Without the gear they close up to
 * the edge.
 *
 * Badge row, from the right: the battery (a slot as wide as its widest
 * reading, "100" on the charger, and only on a board that can sense one),
 * Wi-Fi, then the two controls -- speaker and padlock -- and the Bluetooth
 * rune outermost while the beacon is on. The beacon never moves the controls:
 * the rune is outside them.
 *
 * While the gear is drawn, the controls stay clear of its corner. The runtime
 * takes padlock and speaker presses before the launcher sees the gear's, all
 * with TOUCH_HIT_SLOP, so the speaker's reach ends two slops short of the
 * gear's left edge: on a board whose battery and Wi-Fi already fill that span
 * it costs nothing, and on one with no battery it is the one gap in the row,
 * there so a press on the gear can never mute the console. With no gear there
 * is nothing to keep clear of, and the row closes up. The controls move only
 * when the gear comes or goes -- a change of player, which repaints the whole
 * header -- never while anyone is pressing them. */
namespace {
constexpr int16_t EDGE = 8;                   // in from the right edge, like the gear everywhere
constexpr int16_t GAP = 6;                    // between badges
constexpr int16_t WIFI_HALF = 8;              // the fan is 16px across
constexpr int16_t BATTERY_SLOT = 36;          // the widest badge: "100" with the bolt
constexpr int16_t SYNC_HALF = Ui::BADGE_H / 2 + 1;
/* The clock grows leftwards from clockRight(); the hairline is placed clear
 * of the widest time font 2 draws ("12:59P"), so a minute never moves it. */
constexpr int16_t CLOCK_RESERVE = 50;
constexpr int16_t WIDE_TOP_Y = 6;             // the top row, centred on y=15
constexpr int16_t WIDE_BOTTOM_Y = 25;         // the badge row, centred on y=34
constexpr int16_t TALL_NAME_Y = 32;           // the name row, centred on y=41
constexpr int16_t TALL_BADGE_Y = 51;          // the badge row, centred on y=60

bool tallMode(Board::LayoutMode mode) { return mode == Board::LayoutMode::Vertical; }
int16_t topY(Board::LayoutMode mode) { return tallMode(mode) ? TALL_NAME_Y : WIDE_TOP_Y; }
int16_t badgeY(Board::LayoutMode mode) { return tallMode(mode) ? TALL_BADGE_Y : WIDE_BOTTOM_Y; }

/* The badge row's right-hand passive glyphs, then where the controls end. */
int16_t batterySlotLeft(int16_t screenW) {
    return static_cast<int16_t>(screenW - EDGE - BATTERY_SLOT);
}
int16_t wifiCentre(int16_t screenW) {
    const int16_t right = BOARD.hasBatterySense()
        ? static_cast<int16_t>(batterySlotLeft(screenW) - GAP)
        : static_cast<int16_t>(screenW - EDGE);
    return static_cast<int16_t>(right - WIFI_HALF);
}
int16_t controlsRight(int16_t screenW) {
    const int16_t afterWifi = static_cast<int16_t>(wifiCentre(screenW) - WIFI_HALF - GAP);
    if (!Ui::settingsShown()) return afterWifi;   // no gear: nothing to keep clear of
    const int16_t gearX = static_cast<int16_t>(screenW - EDGE - Ui::CONTROL_H);
    const int16_t clearOfGear = static_cast<int16_t>(gearX - 2 * TOUCH_HIT_SLOP);
    return afterWifi < clearOfGear ? afterWifi : clearOfGear;
}
}   // namespace

int16_t statusTopCy(Board::LayoutMode mode) {
    return static_cast<int16_t>(topY(mode) + Ui::CONTROL_H / 2);
}

int16_t statusBadgeCy(Board::LayoutMode mode) {
    return static_cast<int16_t>(badgeY(mode) + Ui::CONTROL_H / 2);
}

Rect gearRect(Board::LayoutMode mode, int16_t screenW) {
    return Rect{static_cast<int16_t>(screenW - EDGE - Ui::CONTROL_H), topY(mode),
                Ui::CONTROL_H, Ui::CONTROL_H};
}

int16_t syncCx(Board::LayoutMode mode, int16_t screenW) {
    const int16_t right = Ui::settingsShown()
        ? static_cast<int16_t>(gearRect(mode, screenW).x - GAP)
        : static_cast<int16_t>(screenW - EDGE);
    return static_cast<int16_t>(right - SYNC_HALF);
}

int16_t clockRight(Board::LayoutMode mode, int16_t screenW) {
    return static_cast<int16_t>(syncCx(mode, screenW) - SYNC_HALF - 5);
}

/* Mute, on the launcher's own header, which draws no top bar: at the right of
 * the two controls, the padlock before it, as in the top bar. */
Rect speakerRect(Board::LayoutMode mode, int16_t screenW) {
    return Rect{static_cast<int16_t>(controlsRight(screenW) - Ui::CONTROL_H), badgeY(mode),
                Ui::CONTROL_H, Ui::CONTROL_H};
}

Rect lockRect(Board::LayoutMode mode, int16_t screenW) {
    const int16_t right = Board::hasSound()
        ? static_cast<int16_t>(speakerRect(mode, screenW).x - 4)
        : controlsRight(screenW);
    return Rect{static_cast<int16_t>(right - Ui::CONTROL_H), badgeY(mode), Ui::CONTROL_H,
                Ui::CONTROL_H};
}

int16_t wifiCx(Board::LayoutMode, int16_t screenW) { return wifiCentre(screenW); }

int16_t batteryLeft(Board::LayoutMode, int16_t screenW) { return batterySlotLeft(screenW); }

/* The rune's 9px box, a badge gap before the padlock. */
int16_t bleCx(Board::LayoutMode mode, int16_t screenW) {
    return static_cast<int16_t>(lockRect(mode, screenW).x - GAP - 5);
}

/* Speaker against the right edge, 8px in like the gear everywhere else, and
 * the padlock 4px before it. Vertically on the prompt row (32..48 at font 2):
 * the header above holds the mark and the copyright and has no room for two
 * more glyphs at 240px, and the guest hint underneath starts at y=50. The
 * prompt is centred, so at 240px it ends near x=175 and the padlock starts at
 * 192. The Edit buttons below begin at y=60 or 62, 11px under the speaker. */
Rect pickerSpeakerRect(int16_t screenW) {
    return Rect{static_cast<int16_t>(screenW - 8 - Ui::CONTROL_H), 31, Ui::CONTROL_H, Ui::CONTROL_H};
}

Rect pickerLockRect(int16_t screenW) {
    const Rect speaker = pickerSpeakerRect(screenW);
    return Rect{static_cast<int16_t>(speaker.x - 4 - Ui::CONTROL_H), speaker.y, Ui::CONTROL_H,
                Ui::CONTROL_H};
}

int16_t headerHeight(Board::LayoutMode mode) {
    return mode == Board::LayoutMode::Vertical ? LAUNCHER_HEADER_H_TALL
                                               : LAUNCHER_HEADER_H_WIDE;
}

/* The player's name: plain text, and the touch target that opens the picker.
 * It runs from its start up to the hairline, 4px short of it -- in landscape
 * beside the mark on the top row, in portrait on the second row from the left
 * edge. The hairline is the status group's left edge, so the name gets back
 * whatever the group does not need: a player who is not the admin has no gear,
 * and a board with no battery has no battery badge.
 *
 * LANDSCAPE it is beside the mark, not after the copyright: after it, every
 * control added to the badge row took pixels from the name, until it was 24px
 * on a 320px panel. Beside the mark it holds a full PROFILE_NAME_MAX name at
 * font 1 (54px for nine characters) and a shorter one at font 2, which the
 * launcher uses when it fits. */
Rect profileRect(Board::LayoutMode mode, int16_t screenW) {
    const int16_t rightLimit = static_cast<int16_t>(headerDividerX(mode, screenW) - 4);
    const int16_t x = tallMode(mode)
        ? static_cast<int16_t>(8)
        : static_cast<int16_t>(10 + Ui::logoWidth(Ui::Logo::Word) + 10);
    return Rect{x, static_cast<int16_t>(topY(mode) - 1), static_cast<int16_t>(rightLimit - x), 20};
}

/* The hairline that sets the mark, the name and the copyright apart from
 * everything the header reports, in both orientations. Its x is the status
 * group's own left edge, a gap before whichever reaches further left: the
 * clock (at its widest) on the top row, or the badge row's outermost glyph --
 * the rune while the beacon is on, else the padlock. */
int16_t headerDividerX(Board::LayoutMode mode, int16_t screenW) {
    const int16_t topLeft = static_cast<int16_t>(clockRight(mode, screenW) - CLOCK_RESERVE);
    const int16_t badgeLeft = BleBeacon::active()
        ? static_cast<int16_t>(bleCx(mode, screenW) - 5)
        : lockRect(mode, screenW).x;
    return static_cast<int16_t>((topLeft < badgeLeft ? topLeft : badgeLeft) - GAP);
}

/* From the top of the status group's top row to the foot of its badge row. */
int16_t headerDividerTop(Board::LayoutMode mode) { return topY(mode); }

int16_t headerDividerBottom(Board::LayoutMode mode) {
    return static_cast<int16_t>(badgeY(mode) + Ui::CONTROL_H);
}

Rect tileRect(uint8_t slot, Board::LayoutMode mode, int16_t screenW, int16_t screenH) {
    /* Divide what is actually there rather than stepping by a fixed pitch. The
     * old form -- 10 + col * 155, 145 wide -- was a correct description of a
     * 320px panel and a wrong one of anything else: on 480px it drew the same
     * 300px of tiles and left 170px empty on the right, next to a header that
     * had already adapted. The launcher is the first thing anyone sees, so it
     * was also the most obvious way for a bigger board to look broken.
     *
     * Bigger tiles are the point on a larger panel, not a side effect: this
     * board exists to give players who need one a plainer, easier screen, and
     * a target that grows with the glass is easier to hit as well as to read. */
    constexpr int16_t GAP = TILE_GAP;
    constexpr int16_t FOOTER_H = 32;  // the pager row

    const bool tall = mode == Board::LayoutMode::Vertical;
    const int16_t headerH = tall ? LAUNCHER_HEADER_H_TALL : LAUNCHER_HEADER_H_WIDE;
    const Grid g = grid(mode, screenW, screenH);

    // One more gap than there are tiles, in each direction.
    const int16_t tileW = static_cast<int16_t>((screenW - GAP * (g.cols + 1)) / g.cols);
    const int16_t tileH = static_cast<int16_t>(
        (screenH - headerH - FOOTER_H - GAP * (g.rows + 1)) / g.rows);

    const uint8_t col = slot % g.cols;
    const uint8_t row = slot / g.cols;
    return Rect{static_cast<int16_t>(GAP + col * (tileW + GAP)),
                static_cast<int16_t>(headerH + GAP + row * (tileH + GAP)),
                tileW, tileH};
}

namespace {
constexpr Grid GRID_WIDE{2, 3};
constexpr Grid GRID_TALL{2, 2};
constexpr Grid GRID_TALL_DENSE{3, 3};
static_assert(GRID_WIDE.cols * GRID_WIDE.rows <= MAX_PAGE_SIZE &&
              GRID_TALL.cols * GRID_TALL.rows <= MAX_PAGE_SIZE &&
              GRID_TALL_DENSE.cols * GRID_TALL_DENSE.rows <= MAX_PAGE_SIZE,
              "a launcher grid holds more tiles than MAX_PAGE_SIZE");
}  // namespace

Grid grid(Board::LayoutMode mode, int16_t screenW, int16_t screenH) {
    if (mode != Board::LayoutMode::Vertical) {
        return GRID_WIDE;
    }
    const int16_t shortSide = screenW < screenH ? screenW : screenH;
    return shortSide >= DENSE_PORTRAIT_MIN_W ? GRID_TALL_DENSE : GRID_TALL;
}

uint8_t pageSize(Board::LayoutMode mode, int16_t screenW, int16_t screenH) {
    const Grid g = grid(mode, screenW, screenH);
    return static_cast<uint8_t>(g.cols * g.rows);
}

uint8_t tileFillIndex(uint8_t slot, const Grid& g) {
    if (g.cols % 3 != 0) {
        return static_cast<uint8_t>(slot % 3);
    }
    return static_cast<uint8_t>((slot % g.cols + slot / g.cols) % 3);
}

}  // namespace LauncherLayout
