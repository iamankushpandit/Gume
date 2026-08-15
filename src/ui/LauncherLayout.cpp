#include "ui/LauncherLayout.h"

namespace {

constexpr int16_t LAUNCHER_HEADER_H_TALL = 78;
constexpr int16_t LAUNCHER_HEADER_H_WIDE = 48;

}

namespace LauncherLayout {

Rect topBarSettingsRect(int16_t screenW) {
    return Rect{static_cast<int16_t>(screenW - 34), 3, 26, 24};
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
    const int16_t rightLimit = static_cast<int16_t>(screenW - 114);
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
