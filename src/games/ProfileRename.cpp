#include "ProfileRename.h"
#include "ProfileGame.h"
#include "hal/Board.h"
#include "ui/Keypad.h"

/* The keyboard itself lives in ui/Keypad -- grid maths, hit testing and
 * drawing, computed once so they cannot disagree. What stays here is the only
 * part that is about profiles: what a keystroke MEANS. OK commits a new player
 * or a rename, DEL trims the draft, everything else is a character. */

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
    const char ch = Ui::Keypad::hit(touch.x, touch.y, W, H,
                                    Ui::Keypad::FOOTER_BUTTON);
    if (ch == 0) {
        return;
    }
    if (ch == Ui::Keypad::BACKSPACE) {
        if (draft_.length() > 0) draft_.remove(draft_.length() - 1);
    } else if (ch == Ui::Keypad::ACCEPT) {
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

    Ui::Keypad::draw(tft, W, H, Ui::Keypad::FOOTER_BUTTON);
    tft.setTextDatum(TL_DATUM);
}