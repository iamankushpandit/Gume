#include "ui/LauncherLayout.h"

#include "ui/GameLayout.h"

namespace {

constexpr int16_t LAUNCHER_HEADER_H_TALL = 78;
constexpr int16_t LAUNCHER_HEADER_H_WIDE = 48;
constexpr uint8_t TILE_COLUMNS = 2;
/* The pager sits at screenH-28 and is 24 tall, so the tile band has to stop
 * clear of it. 36 leaves the 8px gap the 320x240 layout had -- and at that
 * size the grid lands back on exactly the 46px-tall tiles it was drawn with. */
constexpr int16_t FOOTER_RESERVE = 36;

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

/* Tiles fill whatever panel they are given rather than stepping a fixed pitch.
 *
 * The old numbers (145x46 on a 155px pitch in landscape, 108x96 on 116 in
 * portrait) were measured against a 320x240 panel. On the 4-inch board's
 * 480x320 they stopped two thirds of the way across and left the rest of the
 * screen empty -- the launcher still worked, it just looked like it had been
 * drawn for a smaller screen, because it had.
 *
 * Two columns in both orientations, and the rows come from pageSize(). At
 * 320x240 this lands on 148x53 against the authored 145x46, so the wide layout
 * is preserved rather than redesigned. */
Rect tileRect(uint8_t slot, Board::LayoutMode mode, int16_t screenW, int16_t screenH) {
    const int16_t headerH = headerHeight(mode);
    const uint8_t rows = static_cast<uint8_t>(pageSize(mode) / TILE_COLUMNS);
    const Rect band{8,
                    static_cast<int16_t>(headerH + 4),
                    static_cast<int16_t>(screenW - 16),
                    static_cast<int16_t>(screenH - headerH - FOOTER_RESERVE)};
    return Ui::gridCell(band, TILE_COLUMNS, rows, slot, 8);
}

uint8_t pageSize(Board::LayoutMode mode) {
    return mode == Board::LayoutMode::Vertical ? 4 : 6;
}

}  // namespace LauncherLayout
