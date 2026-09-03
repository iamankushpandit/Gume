#include "SettingsGame.h"

#include "hal/Board.h"

/* The PIN pad, and the Admin tab that is the only way into it.
 *
 * The pad is a modal: while `pinTask_` is anything but None it owns the whole
 * screen, so there is never a half-changed PIN visible behind it. It has three
 * jobs -- enter a new PIN, confirm it, and cancel out -- and keeping the hit
 * testing in one place is what stops the renderer and the handler disagreeing
 * about which cell is which. */

namespace {
/* PIN pad geometry, matching ProfileGame's: three columns, four rows, the
 * last row being DEL / 0 / OK. */
constexpr uint8_t PIN_PAD_COLS = 3;
constexpr uint8_t PIN_PAD_ROWS = 4;
constexpr int16_t PIN_PAD_TOP  = 92;
constexpr int16_t PIN_DOT_Y    = 74;
constexpr int16_t PIN_DOT_R    = 7;
constexpr uint8_t PIN_LENGTH   = 4;
}

/* Admin tab: one action for now. Sized like a Device-tab row. */
Rect SettingsGame::changePinRect() const { return Rect{8, 58, 304, 30}; }

/* Abandons a half-finished PIN change. Without it a mis-tap on Change PIN
 * traps the admin on the pad with no way back. */
Rect SettingsGame::pinCancelRect() const { return Rect{6, 6, 52, 22}; }

/* Standard PIN pad, laid out against the live panel size so it works in both
 * orientations and, more importantly, so every row lands above the bottom
 * edge. The previous version hard-coded rows at y=220 and buttons at y=270 on
 * a 240px-tall panel: the bottom row and both actions were drawn off-screen,
 * leaving nothing to press. */
Rect SettingsGame::pinKeyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH) const {
    const int16_t top    = PIN_PAD_TOP;
    const int16_t bottom = static_cast<int16_t>(screenH - 6);
    const int16_t pitchY = static_cast<int16_t>((bottom - top) / PIN_PAD_ROWS);
    const int16_t keyH   = static_cast<int16_t>(pitchY - 5);

    const int16_t keyW   = static_cast<int16_t>(min<int16_t>(70, (screenW - 40) / PIN_PAD_COLS - 8));
    const int16_t pitchX = static_cast<int16_t>(keyW + 8);
    const int16_t left   = static_cast<int16_t>((screenW - (pitchX * (PIN_PAD_COLS - 1) + keyW)) / 2);

    return Rect{static_cast<int16_t>(left + col * pitchX),
                static_cast<int16_t>(top + row * pitchY), keyW, keyH};
}

/* Bottom row, outer cells -- accessors so the renderer and the touch handler
 * cannot disagree about where they are. */
Rect SettingsGame::pinDeleteRect(int16_t screenW, int16_t screenH) const {
    return pinKeyRect(3, 0, screenW, screenH);
}
Rect SettingsGame::pinConfirmRect(int16_t screenW, int16_t screenH) const {
    return pinKeyRect(3, 2, screenW, screenH);
}

void SettingsGame::appendPinDigit(uint8_t digit) {
    if (enteredPinDigits_ < PIN_LENGTH) {
        enteredPin_ = static_cast<uint16_t>(enteredPin_ * 10 + digit);
        enteredPinDigits_++;
        markDirty();
    }
}

void SettingsGame::deletePinDigit() {
    if (enteredPinDigits_ > 0) {
        enteredPin_ /= 10;
        enteredPinDigits_--;
        markDirty();
    }
}

/* One pad, three jobs: unlock, enter a new PIN, confirm it. Keeping the hit
 * testing in a single place is what stops the renderer and the handler
 * disagreeing about which cell is which. */
bool SettingsGame::handlePinPadTouch(GameHost& host, const TouchPoint& touch) {
    Board& board = host.board();
    const int16_t W = static_cast<int16_t>(host.display().width());
    const int16_t H = static_cast<int16_t>(host.display().height());

    if (pinConfirmRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (enteredPinDigits_ != PIN_LENGTH) {
            host.beepError();          // partial entry: nothing to judge yet
            return true;
        }
        const uint16_t value = enteredPin_;
        enteredPin_ = 0;
        enteredPinDigits_ = 0;

        switch (pinTask_) {
            case PinTask::None:
                break;      // pad is not in use
            case PinTask::SetNew:
                pendingPin_ = value;
                pinTask_ = PinTask::ConfirmNew;
                host.beepOk();
                break;
            case PinTask::ConfirmNew:
                /* Only commit when both entries agree -- a mistyped new PIN
                 * that is stored anyway locks the owner out of their own
                 * device, and there is no recovery path short of a factory
                 * reset. */
                if (value == pendingPin_) {
                    board.setAdminPin(value);
                    host.beepOk();
                } else {
                    host.beepError();
                }
                pendingPin_ = 0;
                pinTask_ = PinTask::None;
                break;
        }
        markFullDirty();
        return true;
    }

    if (pinDeleteRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        deletePinDigit();
        return true;
    }
    /* Rows 0-2 carry 1-9; row 3 middle cell is 0. DEL and OK are the outer
     * cells of row 3 and were handled above. */
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t col = 0; col < PIN_PAD_COLS; ++col) {
            if (pinKeyRect(row, col, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                appendPinDigit(static_cast<uint8_t>(row * PIN_PAD_COLS + col + 1));
                return true;
            }
        }
    }
    if (pinKeyRect(3, 1, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        appendPinDigit(0);
        return true;
    }
    return false;
}

void SettingsGame::renderPinPad(GameHost& host, const char* heading) {
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());

    Ui::drawButton(tft, pinCancelRect(), "Back", Ui::panel(), Ui::outline(),
                   Ui::text(), false, 1);

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);
    tft.drawString(heading, W / 2, 38, 2);

    /* Dots, not the digits: the PIN is masked. The previous version printed
     * it in plain text in a box, which defeats the point of having one. */
    const int16_t dotPitch = 26;
    const int16_t dotsX0   = static_cast<int16_t>(W / 2 - (dotPitch * (PIN_LENGTH - 1)) / 2);
    for (uint8_t i = 0; i < PIN_LENGTH; ++i) {
        const int16_t x = static_cast<int16_t>(dotsX0 + i * dotPitch);
        const uint16_t fill = i < enteredPinDigits_ ? Ui::success() : Ui::panel();
        tft.fillCircle(x, PIN_DOT_Y, PIN_DOT_R, fill);
        tft.drawCircle(x, PIN_DOT_Y, PIN_DOT_R, Ui::outline());
    }

    /* Rows 0-2 are 1-9; row 3 is DEL / 0 / OK. */
    for (uint8_t row = 0; row < PIN_PAD_ROWS; ++row) {
        for (uint8_t col = 0; col < PIN_PAD_COLS; ++col) {
            const Rect r = pinKeyRect(row, col, W, H);

            const char* label;
            char digitLabel[2] = {0, 0};
            uint16_t fill = Ui::panel();

            if (row < 3) {
                digitLabel[0] = static_cast<char>('1' + row * PIN_PAD_COLS + col);
                label = digitLabel;
            } else if (col == 0) {
                label = "DEL";
                fill  = Ui::rgb(150, 60, 60);
            } else if (col == 1) {
                label = "0";
            } else {
                label = "OK";
                fill  = Ui::rgb(45, 154, 96);
            }

            Ui::drawButton(tft, r, label, fill, Ui::outline(), Ui::text(), false, 2);
        }
    }
    tft.setTextDatum(TL_DATUM);
}

void SettingsGame::renderAdminTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const bool admin = isAdmin(board);

    Ui::drawButton(tft, changePinRect(), "Change admin PIN",
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(),
                   admin ? Ui::text() : Ui::muted(), false, 2);

    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString("The PIN guards the admin profile. It", 8, 100, 1);
    tft.drawString("ships as 0000 -- change it.", 8, 116, 1);

    /* Name the admin rather than restating that one exists: if the profile is
     * renamed this follows it. */
    char who[40];
    const uint8_t adminIdx = board.adminProfileIndex();
    if (adminIdx == Board::GUEST_INDEX) {
        snprintf(who, sizeof(who), "No admin profile is set");
    } else {
        snprintf(who, sizeof(who), "Admin profile: %s", board.profileName(adminIdx).c_str());
    }
    tft.drawString(who, 8, 140, 1);

    tft.setTextDatum(TC_DATUM);
    tft.drawString(admin ? "Entered twice; only saved if both match."
                         : "Only the admin can change settings.",
                   W / 2, static_cast<int16_t>(tft.height() - 20), 1);
    tft.setTextDatum(TL_DATUM);
}
