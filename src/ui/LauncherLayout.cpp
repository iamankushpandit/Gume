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
     * slop only, never in pixels either of them paints. */
    return Rect{40, 5, 18, 20};
}

Rect lockRect(Board::LayoutMode mode, int16_t screenW) {
    if (mode == Board::LayoutMode::Vertical) {
        /* On the badge row, left of the gear. The badges before it end near
         * x=155 in the widest state ("100" while charging, plus the beacon
         * rune), and the gear starts at screenW-32. */
        return Rect{static_cast<int16_t>(screenW - 70), 48, 24, 24};
    }
    /* Wide: the air between "Braino!" -- which ends near x=80 at font 4 -- and
     * the profile name at x=124. The badge row below is untouched; it is
     * packed to the pixel and had nothing to give. */
    return Rect{92, 8, 24, 24};
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
    /* Stops short of the header's status hairline at lW-116. That hairline
     * moved out by 6px when the battery badge grew to carry its percentage;
     * the profile name gives up those pixels because it truncates gracefully
     * and the badge row does not. */
    const int16_t rightLimit = static_cast<int16_t>(screenW - 120);
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
