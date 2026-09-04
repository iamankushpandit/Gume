#include "ui/LauncherLayout.h"

#include "BoardConfig.h"

namespace {

constexpr int16_t LAUNCHER_HEADER_H_TALL = 78;
constexpr int16_t LAUNCHER_HEADER_H_WIDE = 48;

}

namespace LauncherLayout {

Rect topBarSettingsRect(int16_t screenW) {
    return Rect{static_cast<int16_t>(screenW - 34), 3, 26, 24};
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
    const Rect lock = topBarLockRect(screenW);
    return static_cast<int16_t>(lock.x + lock.w + 4);
}

/* Both layouts put the padlock in the status cluster, at badge size, centred
 * on the same row as the sync, Wi-Fi and battery glyphs. It sat beside
 * "Braino!" first, on the far side of the header's hairline, where it read as
 * part of the product name rather than as one of the device's indicators. */
Rect lockRect(Board::LayoutMode mode, int16_t screenW) {
    if (mode == Board::LayoutMode::Vertical) {
        /* Portrait badge row (centred on y=60), left of the gear. The badges
         * before it end near x=155 in the widest state ("100" while charging,
         * plus the beacon rune), and the gear starts at screenW-32. */
        return Rect{static_cast<int16_t>(screenW - 64), 51, 18, 18};
    }
    /* Landscape: the left-hand end of the badge row, inside the hairline.
     * That row had about 4px spare, so the hairline moved out again -- from
     * screenW-116 to screenW-138 -- and profileRect()'s right limit moved with
     * it, exactly as it did when the battery badge grew. The name truncates
     * gracefully; the badge row does not. */
    return Rect{static_cast<int16_t>(screenW - 136), 25, 18, 18};
}

int16_t headerHeight(Board::LayoutMode mode) {
    return mode == Board::LayoutMode::Vertical ? LAUNCHER_HEADER_H_TALL
                                               : LAUNCHER_HEADER_H_WIDE;
}

Rect gearRect(Board::LayoutMode mode, int16_t screenW) {
    if (mode == Board::LayoutMode::Vertical) {
        return Rect{static_cast<int16_t>(screenW - 32), 48, 26, 24};
    }
    return Rect{static_cast<int16_t>(screenW - 30), 11, 26, 26};
}

Rect profileRect(Board::LayoutMode mode, int16_t screenW) {
    if (mode == Board::LayoutMode::Vertical) {
        return Rect{8, 34, static_cast<int16_t>(min<int16_t>(112, screenW - 46)), 20};
    }
    const int16_t x = 124;
    /* Stops short of the header's status hairline at lW-138. That hairline has
     * moved out twice now -- 6px when the battery badge grew to carry its
     * percentage, then 22px again for the Lock badge -- and the profile name
     * gives up those pixels both times, because it truncates gracefully and
     * the badge row does not. */
    const int16_t rightLimit = static_cast<int16_t>(screenW - 142);
    const int16_t w = static_cast<int16_t>(min<int16_t>(86, max<int16_t>(52, rightLimit - x)));
    return Rect{x, 30, w, 18};
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
    const uint8_t rows = tall ? 2 : 3;

    // Two columns either way: three gaps across, one more than the row count down.
    const int16_t tileW = static_cast<int16_t>((screenW - GAP * 3) / 2);
    const int16_t tileH = static_cast<int16_t>(
        (screenH - headerH - FOOTER_H - GAP * (rows + 1)) / rows);

    const uint8_t col = slot % 2;
    const uint8_t row = slot / 2;
    return Rect{static_cast<int16_t>(GAP + col * (tileW + GAP)),
                static_cast<int16_t>(headerH + GAP + row * (tileH + GAP)),
                tileW, tileH};
}

uint8_t pageSize(Board::LayoutMode mode) {
    return mode == Board::LayoutMode::Vertical ? 4 : 6;
}

}  // namespace LauncherLayout
