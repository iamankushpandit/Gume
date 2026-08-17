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

Rect tileRect(uint8_t slot, Board::LayoutMode mode, int16_t screenW, int16_t screenH) {
    constexpr int16_t GAP = 8;

    if (mode == Board::LayoutMode::Vertical) {
        // 2 columns x 2 rows, filling from below the header to above the footer.
        const int16_t headerH = LAUNCHER_HEADER_H_TALL;
        const int16_t footerH = 32;  // pager buttons
        const int16_t availW = static_cast<int16_t>(screenW - GAP * 3);  // 2 cols, 3 gaps
        const int16_t availH = static_cast<int16_t>(screenH - headerH - footerH - GAP * 3);
        const int16_t tileW = availW / 2;
        const int16_t tileH = availH / 2;
        const uint8_t col = slot % 2;
        const uint8_t row = slot / 2;
        return Rect{
            static_cast<int16_t>(GAP + col * (tileW + GAP)),
            static_cast<int16_t>(headerH + GAP + row * (tileH + GAP)),
            tileW, tileH
        };
    }

    // Horizontal: 2 columns x 3 rows (6 tiles), filling available space.
    const int16_t headerH = LAUNCHER_HEADER_H_WIDE;
    const int16_t footerH = 32;
    const int16_t availW = static_cast<int16_t>(screenW - GAP * 3);
    const int16_t availH = static_cast<int16_t>(screenH - headerH - footerH - GAP * 4);
    const int16_t tileW = availW / 2;
    const int16_t tileH = availH / 3;
    const uint8_t col = slot % 2;
    const uint8_t row = slot / 2;
    return Rect{
        static_cast<int16_t>(GAP + col * (tileW + GAP)),
        static_cast<int16_t>(headerH + GAP + row * (tileH + GAP)),
        tileW, tileH
    };
}

uint8_t pageSize(Board::LayoutMode mode) {
    return mode == Board::LayoutMode::Vertical ? 4 : 6;
}

}  // namespace LauncherLayout
