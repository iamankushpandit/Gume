#include "ui/LauncherLayout.h"

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

Rect topBarLockRect() {
    /* Starts at 40 so that home's 8px of touch slop -- which reaches exactly
     * 40 -- stops where the padlock starts drawing. The two targets overlap in
     * slop only, never in pixels either of them paints. Sized like a status
     * badge rather than like the gear: it belongs to the same family as the
     * battery and Wi-Fi glyphs, and a gear-sized padlock read as a control
     * twice as important as anything else on the bar. */
    return Rect{40, 6, 18, 18};
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

Rect tileRect(uint8_t slot, Board::LayoutMode mode) {
    if (mode == Board::LayoutMode::Vertical) {
        const uint8_t col = slot % 2;
        const uint8_t row = slot / 2;
        return Rect{static_cast<int16_t>(8 + col * 116),
                    static_cast<int16_t>(LAUNCHER_HEADER_H_TALL + 8 + row * 104),
                    108, 96};
    }

    const uint8_t col = slot % 2;
    const uint8_t row = slot / 2;
    return Rect{static_cast<int16_t>(10 + col * 155), static_cast<int16_t>(52 + row * 53), 145, 46};
}

uint8_t pageSize(Board::LayoutMode mode) {
    return mode == Board::LayoutMode::Vertical ? 4 : 6;
}

}  // namespace LauncherLayout
