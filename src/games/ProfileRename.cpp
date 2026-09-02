#include "ProfileRename.h"
#include "ProfileGame.h"
#include "hal/Board.h"

namespace {
constexpr uint8_t KEY_COLS = 6;
constexpr uint8_t KEY_ROWS = 5;
const char* const KEYS[KEY_ROWS] = {
    "ABCDEF", "GHIJKL", "MNOPQR", "STUVWX", "YZ -<>"
};

/* Compute a key rect with a custom y0 offset, so the rename phase can
 * shift the keyboard up to make room for the Cancel button at the bottom. */
Rect keyRectWithOffset(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH,
                       int16_t yOffset) {
    const bool tall = screenH > screenW;
    const int16_t margin = 8;
    const int16_t gap = 4;
    const int16_t keyW = static_cast<int16_t>((screenW - 2 * margin - (KEY_COLS - 1) * gap) / KEY_COLS);
    const int16_t keyH = tall ? 36 : 26;
    const int16_t pitch = tall ? 40 : 29;
    const int16_t y0 = tall ? 92 : 86;
    const int16_t adjustedY0 = static_cast<int16_t>(y0 + yOffset);
    return Rect{static_cast<int16_t>(margin + col * (keyW + gap)),
                static_cast<int16_t>(adjustedY0 + row * pitch), keyW, keyH};
}
}

/* Centred horizontally at the bottom of the screen (same row as Add / Done
 * on the picker), both orientations. */
Rect ProfileGame::renameCancelRect(int16_t screenW, int16_t screenH) const {
    return Rect{static_cast<int16_t>((screenW - 52) / 2), static_cast<int16_t>(screenH - 30), 52, 22};
}

void ProfileGame::updateRename(GameHost& host, const TouchPoint& touch) {
    Board& board = host.board();
    const int16_t W = static_cast<int16_t>(host.display().width());
    const int16_t H = static_cast<int16_t>(host.display().height());

    if (renameCancelRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        phase_ = Phase::Pick;
        draft_ = "";
        markFullDirty();
        return;
    }
    /* Shift keyboard up so Cancel (at bottom) sits above it, both visible.
     * Portrait: keyboard bottom (default) = 92 + 160 + 36 = 288.
     * Cancel top = 290.  Gap = 2px.  For 4px gap: keyboard bottom = 286.
     * yOffset = 286 - 288 = -2.
     * Landscape: keyboard bottom (default) = 86 + 116 + 26 = 228.
     * Cancel top = 210.  Overlap = 18px.  For 4px gap: keyboard bottom = 206.
     * yOffset = 206 - 228 = -22. */
    const int16_t yOffset = H > W ? -2 : -22;
    for (uint8_t r = 0; r < KEY_ROWS; ++r) {
        for (uint8_t c = 0; c < KEY_COLS; ++c) {
            if (!keyRectWithOffset(r, c, W, H, yOffset).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
            const char ch = KEYS[r][c];
            if (ch == '<') {
                if (draft_.length() > 0) draft_.remove(draft_.length() - 1);
            } else if (ch == '>') {
                if (editing_ == 0xFF) {
                    if (draft_.length() > 0) {
                        board.addPlayer(draft_);
                    } else {
                        board.beepError();
                        return;
                    }
                } else if (draft_.length() > 0) {
                    board.setProfileName(editing_, draft_);
                }
                phase_ = Phase::Pick;
                markFullDirty();
                return;
            } else if (draft_.length() < Board::PROFILE_NAME_MAX) {
                draft_ += ch;
            }
            markDirty();
            return;
        }
    }
}

void ProfileGame::renderRename(GameHost& host) {
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());

    tft.drawString(editing_ == 0xFF ? "New player" : "Name", W / 2, 8, 2);
    const int16_t fieldW = static_cast<int16_t>(min<int16_t>(240, W - 40));
    const Rect field{static_cast<int16_t>((W - fieldW) / 2), 28, fieldW, 30};
    tft.fillRoundRect(field.x, field.y, field.w, field.h, 4, Ui::surface());
    tft.drawRoundRect(field.x, field.y, field.w, field.h, 4, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(draft_.length() ? draft_.c_str() : "...",
                   W / 2, static_cast<int16_t>(field.y + field.h / 2), 4);

    /* Cancel button, centred horizontally at the bottom of the screen. */
    Ui::drawButton(tft, renameCancelRect(W, H), "Cancel", Ui::panel(), Ui::outline(),
                   Ui::text(), false, 1);

    const int16_t yOffset = H > W ? -2 : -22;
    for (uint8_t r = 0; r < KEY_ROWS; ++r) {
        for (uint8_t c = 0; c < KEY_COLS; ++c) {
            const char ch = KEYS[r][c];
            char keyLabel[2] = {ch, 0};
            const char* label = keyLabel;
            uint16_t fill = Ui::panel();
            if (ch == '<') { label = "DEL"; fill = Ui::rgb(150, 60, 60); }
            if (ch == '>') { label = "OK";  fill = Ui::rgb(45, 154, 96); }
            if (ch == ' ') { label = "_"; }
            Ui::drawButton(tft, keyRectWithOffset(r, c, W, H, yOffset), label, fill, Ui::outline(), Ui::text(), false, 2);
        }
    }
    tft.setTextDatum(TL_DATUM);
}