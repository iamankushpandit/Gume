#include "Keypad.h"

#include <string.h>

namespace Ui {
namespace Keypad {
namespace {

/* QWERTY, with a digit row above it and an action row below. Uppercase only:
 * a shifted lowercase layer would need a second table and a mode, and nothing
 * here has needed one -- profile names, network passwords and peer labels are
 * all things people type in capitals on a games console.
 *
 * The action row is three keys wide rather than ten, and keyRect() lays it out
 * separately for that reason. */
const char* const KEYS[ROWS] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM",
    " <>",
};

constexpr uint8_t ACTION_ROW = 4;
constexpr int16_t MARGIN = 8;

/* Portrait squeezes ten columns into 240px, so it gets the tighter gap: at 4px
 * the keys come out 18px wide, at 2px they are 20px. Two pixels of key is
 * worth more than two pixels of gap when the key is already the narrowest
 * target in the firmware. */
int16_t gapFor(bool tall) { return tall ? 2 : 4; }

int16_t keyWidth(int16_t screenW, bool tall) {
    const int16_t gap = gapFor(tall);
    return static_cast<int16_t>(
        (screenW - 2 * MARGIN - (MAX_COLS - 1) * gap) / MAX_COLS);
}

}   // namespace

uint8_t columns(uint8_t row) {
    if (row >= ROWS) {
        return 0;
    }
    return static_cast<uint8_t>(strlen(KEYS[row]));
}

char keyAt(uint8_t row, uint8_t col) {
    if (row >= ROWS || col >= columns(row)) {
        return 0;
    }
    return KEYS[row][col];
}

int16_t height(int16_t screenW, int16_t screenH) {
    const bool tall = screenH > screenW;
    const int16_t keyH = tall ? 36 : 26;
    const int16_t pitch = tall ? 40 : 29;
    return static_cast<int16_t>((ROWS - 1) * pitch + keyH);
}

int16_t topY(int16_t screenW, int16_t screenH, int16_t bottomReserve) {
    return static_cast<int16_t>(screenH - bottomReserve - height(screenW, screenH));
}

Rect keyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH,
             int16_t bottomReserve) {
    if (row >= ROWS || col >= columns(row)) {
        return Rect{};
    }
    const bool tall = screenH > screenW;
    const int16_t gap = gapFor(tall);
    const int16_t keyW = keyWidth(screenW, tall);
    const int16_t keyH = tall ? 36 : 26;
    const int16_t pitch = tall ? 40 : 29;
    const int16_t y = static_cast<int16_t>(
        topY(screenW, screenH, bottomReserve) + row * pitch);

    if (row == ACTION_ROW) {
        /* Space, DEL and OK share the full width: space takes four columns
         * because it is pressed most and is the easiest key to miss, and the
         * two actions take three each. Laid out from the same margin and gap
         * as the rows above so the block lines up with them. */
        const int16_t unit = static_cast<int16_t>(keyW + gap);
        const int16_t spaceW = static_cast<int16_t>(4 * unit - gap);
        const int16_t actionW = static_cast<int16_t>(3 * unit - gap);
        const int16_t x0 = MARGIN;
        if (col == 0) {
            return Rect{x0, y, spaceW, keyH};
        }
        const int16_t x = static_cast<int16_t>(
            x0 + 4 * unit + (col - 1) * (actionW + gap));
        return Rect{x, y, actionW, keyH};
    }

    /* Short rows are centred, which is what makes this read as QWERTY rather
     * than as a table with holes on the right. */
    const int16_t count = static_cast<int16_t>(columns(row));
    const int16_t rowW = static_cast<int16_t>(count * keyW + (count - 1) * gap);
    const int16_t x0 = static_cast<int16_t>((screenW - rowW) / 2);
    return Rect{static_cast<int16_t>(x0 + col * (keyW + gap)), y, keyW, keyH};
}

char hit(int16_t x, int16_t y, int16_t screenW, int16_t screenH,
         int16_t bottomReserve) {
    for (uint8_t r = 0; r < ROWS; ++r) {
        for (uint8_t c = 0; c < columns(r); ++c) {
            if (keyRect(r, c, screenW, screenH, bottomReserve)
                    .contains(x, y, TOUCH_HIT_SLOP)) {
                return KEYS[r][c];
            }
        }
    }
    return 0;
}

void draw(Renderer& tft, int16_t screenW, int16_t screenH,
          int16_t bottomReserve) {
    /* Font 2 on the letters, font 1 on the action words: "SPACE" at font 2
     * does not fit the space bar in portrait, and a label clipped mid-word is
     * worse than a smaller one. */
    for (uint8_t r = 0; r < ROWS; ++r) {
        for (uint8_t c = 0; c < columns(r); ++c) {
            const char ch = KEYS[r][c];
            char keyLabel[2] = {ch, 0};
            const char* label = keyLabel;
            uint16_t fill = Ui::panel();
            uint8_t font = 2;
            if (ch == BACKSPACE) { label = "DEL";   fill = Ui::rgb(150, 60, 60); font = 1; }
            if (ch == ACCEPT)    { label = "OK";    fill = Ui::rgb(45, 154, 96); font = 1; }
            if (ch == SPACE)     { label = "SPACE"; font = 1; }
            Ui::drawButton(tft, keyRect(r, c, screenW, screenH, bottomReserve), label,
                           fill, Ui::outline(), Ui::text(), false, font);
        }
    }
}

}   // namespace Keypad
}   // namespace Ui
