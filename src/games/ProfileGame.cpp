#include "ProfileGame.h"

namespace {
// 6 columns x 5 rows covers A-Z, space and backspace with room to spare.
constexpr uint8_t KEY_COLS = 6;
constexpr uint8_t KEY_ROWS = 5;
const char* const KEYS[KEY_ROWS] = {
    "ABCDEF", "GHIJKL", "MNOPQR", "STUVWX", "YZ -<>"
};
}

const char* ProfileGame::title() const { return "Who is playing?"; }

void ProfileGame::begin(GameHost&) {
    phase_ = Phase::Pick;
    draft_ = "";
    markFullDirty();
}

Rect ProfileGame::slotRect(uint8_t i) const {
    return Rect{16, static_cast<int16_t>(52 + i * 56), 220, 46};
}

Rect ProfileGame::renameRect(uint8_t i) const {
    return Rect{244, static_cast<int16_t>(52 + i * 56), 60, 46};
}

Rect ProfileGame::keyRect(uint8_t row, uint8_t col) const {
    return Rect{static_cast<int16_t>(14 + col * 49),
                static_cast<int16_t>(86 + row * 29), 45, 26};
}

void ProfileGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;
    Board& board = host.board();

    if (phase_ == Phase::Pick) {
        for (uint8_t i = 0; i < Board::PROFILE_COUNT; ++i) {
            if (renameRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                editing_ = i;
                draft_ = board.profileName(i);
                phase_ = Phase::Rename;
                markFullDirty();
                return;
            }
            if (slotRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                board.setActiveProfile(i);
                host.board().beepOk();
                host.goHome();
                return;
            }
        }
        return;
    }

    // ---- rename ----
    for (uint8_t r = 0; r < KEY_ROWS; ++r) {
        for (uint8_t c = 0; c < KEY_COLS; ++c) {
            if (!keyRect(r, c).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
            const char ch = KEYS[r][c];
            if (ch == '<') {                       // backspace
                if (draft_.length() > 0) draft_.remove(draft_.length() - 1);
            } else if (ch == '>') {                // done
                if (draft_.length() > 0) board.setProfileName(editing_, draft_);
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

void ProfileGame::render(GameHost& host) {
    Board& board = host.board();
    TFT_eSPI& tft = board.display();
    Ui::clear(tft);

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);

    if (phase_ == Phase::Pick) {
        tft.drawString("Who is playing?", SCREEN_WIDTH / 2, 14, 4);
        const uint8_t active = board.activeProfile();
        for (uint8_t i = 0; i < Board::PROFILE_COUNT; ++i) {
            const Rect r = slotRect(i);
            const bool sel = (i == active);
            Ui::drawButton(tft, r, board.profileName(i),
                           sel ? Ui::rgb(36, 132, 204) : Ui::panel(),
                           Ui::outline(), sel ? TFT_WHITE : Ui::text(), false, 4);
            Ui::drawButton(tft, renameRect(i), "Edit", Ui::surface(),
                           Ui::outline(), Ui::muted(), false, 2);
        }
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("Each name keeps its own scores", SCREEN_WIDTH / 2, 224, 1);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    // ---- rename ----
    tft.drawString("Name", SCREEN_WIDTH / 2, 8, 2);
    const Rect field{40, 28, 240, 30};
    tft.fillRoundRect(field.x, field.y, field.w, field.h, 4, Ui::surface());
    tft.drawRoundRect(field.x, field.y, field.w, field.h, 4, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(draft_.length() ? draft_ : String("..."),
                   SCREEN_WIDTH / 2, static_cast<int16_t>(field.y + field.h / 2), 4);

    for (uint8_t r = 0; r < KEY_ROWS; ++r) {
        for (uint8_t c = 0; c < KEY_COLS; ++c) {
            const char ch = KEYS[r][c];
            String label(ch);
            uint16_t fill = Ui::panel();
            if (ch == '<') { label = "DEL"; fill = Ui::rgb(150, 60, 60); }
            if (ch == '>') { label = "OK";  fill = Ui::rgb(45, 154, 96); }
            if (ch == ' ') { label = "_"; }
            Ui::drawButton(tft, keyRect(r, c), label, fill, Ui::outline(), Ui::text(), false, 2);
        }
    }
    tft.setTextDatum(TL_DATUM);
}
