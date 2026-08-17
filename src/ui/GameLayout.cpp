#include "ui/GameLayout.h"

namespace Ui {

Frame frame(Renderer& tft) {
    Frame f;
    f.w = tft.width();
    f.h = tft.height();
    return f;
}

Rect Stack::next(int16_t h, int16_t pad) {
    const Rect r{static_cast<int16_t>(area_.x + pad), y_,
                 static_cast<int16_t>(area_.w - 2 * pad), h};
    y_ = static_cast<int16_t>(y_ + h + gap_);
    return r;
}

Rect Stack::nextFraction(uint8_t numerator, uint8_t denominator, int16_t pad) {
    if (denominator == 0) {
        return next(0, pad);
    }
    const int16_t left = remaining();
    const int16_t h = static_cast<int16_t>(static_cast<int32_t>(left) * numerator / denominator);
    return next(h < 0 ? 0 : h, pad);
}

void Stack::skip(int16_t h) {
    y_ = static_cast<int16_t>(y_ + h);
}

Rect Stack::rest(int16_t pad) const {
    return Rect{static_cast<int16_t>(area_.x + pad), y_,
                static_cast<int16_t>(area_.w - 2 * pad), remaining()};
}

int16_t Stack::remaining() const {
    const int16_t left = static_cast<int16_t>(area_.y + area_.h - y_);
    return left > 0 ? left : 0;
}

Rect gridCell(const Rect& band, uint8_t cols, uint8_t rows, uint8_t index, int16_t gap) {
    if (cols == 0 || rows == 0) {
        return Rect{};
    }
    const int16_t col = static_cast<int16_t>(index % cols);
    const int16_t row = static_cast<int16_t>(index / cols);

    /* Divide the gaps out first, then split what is left. Working from a
     * per-cell pitch instead would let the rounding error accumulate across
     * the row and leave the last cell short of the edge. */
    const int16_t innerW = static_cast<int16_t>(band.w - gap * (cols - 1));
    const int16_t innerH = static_cast<int16_t>(band.h - gap * (rows - 1));
    const int16_t cellW = static_cast<int16_t>(innerW / cols);
    const int16_t cellH = static_cast<int16_t>(innerH / rows);

    /* The last row and column absorb the remainder so the grid ends flush. */
    const int16_t w = (col == cols - 1)
        ? static_cast<int16_t>(innerW - cellW * (cols - 1)) : cellW;
    const int16_t h = (row == rows - 1)
        ? static_cast<int16_t>(innerH - cellH * (rows - 1)) : cellH;

    return Rect{static_cast<int16_t>(band.x + col * (cellW + gap)),
                static_cast<int16_t>(band.y + row * (cellH + gap)),
                w, h};
}

Rect squareIn(const Rect& band, int16_t maxSide) {
    int16_t side = band.w < band.h ? band.w : band.h;
    if (maxSide > 0 && side > maxSide) {
        side = maxSide;
    }
    if (side < 0) {
        side = 0;
    }
    return centreIn(band, side, side);
}

Rect centreIn(const Rect& band, int16_t w, int16_t h) {
    return Rect{static_cast<int16_t>(band.x + (band.w - w) / 2),
                static_cast<int16_t>(band.y + (band.h - h) / 2),
                w, h};
}

uint8_t answerColumns(const Frame& f, uint8_t count) {
    if (count <= 1) {
        return 1;
    }
    if (f.tall()) {
        /* 240px of width is two 110px buttons at best, and portrait has the
         * height to spare -- so stack small sets and only pair up when a
         * single column would run out of room. */
        return count <= 4 ? 1 : 2;
    }
    /* Landscape is 320 wide and only ~210 tall below the top bar; three rows
     * of buttons is already tight, so widen rather than lengthen. */
    return count <= 2 ? count : (count <= 6 ? 2 : 3);
}

}
