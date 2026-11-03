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

/* THE LAUNCHER HEADER, both orientations. The mark and the copyright are
 * fixed; everything else is placed here, so the drawing and every hit test
 * read the same rectangles.
 *
 * The status row is ordered by how often each thing is there: Wi-Fi and the
 * battery at the right-hand end, then the speaker and the padlock, and the
 * Bluetooth rune -- drawn only while the beacon is on -- at the far left of
 * the group, where its absence leaves a gap nobody notices instead of moving
 * the rest. The sync check sits with the clock it describes, as it does in
 * the top bar; on its own it means nothing.
 *
 * The gear is the top bar's: Ui::CONTROL_H, 8px in from the right edge. It was
 * 26px here and 18px in every app, so the same control looked like two.
 *
 * And it is kept away from the padlock and the speaker. The runtime takes
 * their taps before the launcher sees the gear's, so with the speaker under
 * the gear, a tap on the gear's lower edge would have muted the console. Wi-Fi
 * and the battery sit between them, nearly 40px of glyphs nobody taps.
 *
 * The padlock's x leaves room for the widest battery badge ("100", 28px) and
 * Wi-Fi after the speaker; a narrower badge only widens the gap. */
namespace {
constexpr int16_t CONTROL_FROM_RIGHT = 110;   // padlock's left edge, from screenW
constexpr int16_t WIDE_TOP_Y = 6;             // the top row, centred on y=15
constexpr int16_t WIDE_BOTTOM_Y = 25;         // the badge row, centred on y=34
constexpr int16_t TALL_NAME_Y = 32;           // the name row, centred on y=41
constexpr int16_t TALL_BADGE_Y = 51;          // the badge row, centred on y=60
}   // namespace

Rect lockRect(Board::LayoutMode mode, int16_t screenW) {
    const bool tall = mode == Board::LayoutMode::Vertical;
    return Rect{static_cast<int16_t>(screenW - CONTROL_FROM_RIGHT), tall ? TALL_BADGE_Y : WIDE_BOTTOM_Y,
                Ui::CONTROL_H, Ui::CONTROL_H};
}

/* Mute, on the launcher's own header, which draws no top bar: beside the
 * padlock, as it is in the top bar, in both orientations. */
Rect speakerRect(Board::LayoutMode mode, int16_t screenW) {
    const Rect lock = lockRect(mode, screenW);
    return Rect{static_cast<int16_t>(lock.x + lock.w + 4), lock.y, Ui::CONTROL_H, Ui::CONTROL_H};
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

Rect gearRect(Board::LayoutMode mode, int16_t screenW) {
    const bool tall = mode == Board::LayoutMode::Vertical;
    return Rect{static_cast<int16_t>(screenW - 8 - Ui::CONTROL_H), tall ? TALL_NAME_Y : WIDE_TOP_Y,
                Ui::CONTROL_H, Ui::CONTROL_H};
}

/* The player's name: plain text, and the touch target that opens the picker.
 *
 * LANDSCAPE it is on the top row beside the mark, not after the copyright.
 * After the copyright it began at x=132 and every control added to the badge
 * row took pixels from it, until its right limit was screenW-164 -- 24px on a
 * 320px panel -- and a floor of 52 then pushed it past that limit, over the
 * hairline and under the padlock. Beside the mark it runs to the clock: 91px
 * at 320, which holds a full PROFILE_NAME_MAX name at font 2 in all but the
 * widest letters, and the launcher drops to font 1 (6px a character, 54px for
 * nine) when it does not. The clock sits off the gear and the sync check and
 * is at most 48px at font 2 ("12:59P"), so its left edge is never further left
 * than screenW-99; 7px of air before that.
 *
 * PORTRAIT it has the second row to itself up to the gear. */
Rect profileRect(Board::LayoutMode mode, int16_t screenW) {
    const Rect gear = gearRect(mode, screenW);
    if (mode == Board::LayoutMode::Vertical) {
        return Rect{8, static_cast<int16_t>(TALL_NAME_Y - 1),
                    static_cast<int16_t>(gear.x - 8 - 8), 20};
    }
    const int16_t x = static_cast<int16_t>(10 + Ui::logoWidth(Ui::Logo::Word) + 10);
    const int16_t rightLimit = static_cast<int16_t>(screenW - 106);
    return Rect{x, static_cast<int16_t>(WIDE_TOP_Y - 1), static_cast<int16_t>(rightLimit - x), 20};
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
    constexpr int16_t GAP = 8;
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
