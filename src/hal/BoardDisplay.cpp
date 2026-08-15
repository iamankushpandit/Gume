#include "Board.h"

#include "ui/Ui.h"

namespace {
uint16_t read16(File& file) {
    uint16_t result = file.read();
    result |= static_cast<uint16_t>(file.read()) << 8;
    return result;
}

uint32_t read32(File& file) {
    uint32_t result = file.read();
    result |= static_cast<uint32_t>(file.read()) << 8;
    result |= static_cast<uint32_t>(file.read()) << 16;
    result |= static_cast<uint32_t>(file.read()) << 24;
    return result;
}
}

TFT_eSPI& Board::display() {
    return tft_;
}

void Board::setDisplayRotation(uint8_t rotation) {
    displayRotation_ = rotation;
    tft_.setRotation(rotation);
    lastTouch_ = TouchPoint{};
}

uint8_t Board::displayRotation() const {
    return displayRotation_;
}

bool Board::drawBmp(const char* path, int16_t x, int16_t y, int16_t maxW, int16_t maxH) {
    if (!sdMounted_) {
        return false;
    }
    File bmp = SD.open(path, FILE_READ);
    if (!bmp) {
        return false;
    }

    if (read16(bmp) != 0x4D42) {
        bmp.close();
        return false;
    }
    (void)read32(bmp);
    (void)read32(bmp);
    const uint32_t imageOffset = read32(bmp);
    const uint32_t headerSize = read32(bmp);
    if (headerSize < 40) {
        bmp.close();
        return false;
    }

    int32_t bmpW = static_cast<int32_t>(read32(bmp));
    int32_t bmpH = static_cast<int32_t>(read32(bmp));
    if (read16(bmp) != 1) {
        bmp.close();
        return false;
    }
    const uint16_t depth = read16(bmp);
    const uint32_t compression = read32(bmp);
    if ((depth != 24 && depth != 16) || compression != 0 || bmpW <= 0 || bmpH == 0) {
        bmp.close();
        return false;
    }

    const bool flip = bmpH > 0;
    if (bmpH < 0) {
        bmpH = -bmpH;
    }
    const int16_t drawW = min<int32_t>(bmpW, maxW);
    const int16_t drawH = min<int32_t>(bmpH, maxH);
    const uint32_t rowSize = ((static_cast<uint32_t>(bmpW) * depth + 31) / 32) * 4;

    for (int16_t row = 0; row < drawH; ++row) {
        const uint32_t sourceRow = flip ? (bmpH - 1 - row) : row;
        bmp.seek(imageOffset + sourceRow * rowSize);
        for (int16_t col = 0; col < drawW; ++col) {
            uint16_t color = TFT_BLACK;
            if (depth == 24) {
                const uint8_t b = bmp.read();
                const uint8_t g = bmp.read();
                const uint8_t r = bmp.read();
                color = Ui::rgb(r, g, b);
            } else {
                color = read16(bmp);
            }
            const int16_t px = x + col;
            const int16_t py = y + row;
            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                tft_.drawPixel(px, py, color);
            }
        }
    }

    bmp.close();
    return true;
}
