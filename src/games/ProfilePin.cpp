// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ProfileApp.h"
#include "hal/Board.h"

/* The admin PIN pad on the Profiles screen: its geometry, its input and its
 * drawing. Split from ProfileApp.cpp, which keeps the picker, the menu, the
 * per-player game list and the phase routing. */

namespace {
/* PIN pad geometry. Four rows: 1-9 then the DEL / 0 / OK row. PIN_PAD_TOP
 * clears the title (font 2, 16px) and the row of PIN dots below it. */
constexpr uint8_t PIN_PAD_COLS = 3;
constexpr uint8_t PIN_PAD_ROWS = 4;
constexpr int16_t PIN_PAD_TOP  = 58;
constexpr int16_t PIN_DOT_Y    = 38;
constexpr int16_t PIN_DOT_R    = 7;
constexpr uint8_t PIN_LENGTH   = 4;
}  // namespace

/* Standard PIN pad: three columns, four rows, stacked vertically and centred.
 *
 * 1 2 3
 * 4 5 6
 * 7 8 9
 * < 0 OK
 *
 * The whole pad is derived from the live width/height so it fits in both
 * orientations. The previous version hard-coded row Y values that ran past
 * 240px in landscape, so the bottom row and both action buttons were drawn
 * off the panel entirely -- there was nothing to press. Anything changed here
 * must still end above screenH. */
Rect ProfileApp::pinKeyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH) const {
    const int16_t top    = PIN_PAD_TOP;
    const int16_t bottom = static_cast<int16_t>(screenH - 6);
    const int16_t pitchY = static_cast<int16_t>((bottom - top) / PIN_PAD_ROWS);
    const int16_t keyH   = static_cast<int16_t>(pitchY - 6);

    const int16_t keyW   = static_cast<int16_t>(min<int16_t>(70, (screenW - 40) / PIN_PAD_COLS - 8));
    const int16_t pitchX = static_cast<int16_t>(keyW + 8);
    const int16_t left   = static_cast<int16_t>((screenW - (pitchX * (PIN_PAD_COLS - 1) + keyW)) / 2);

    return Rect{static_cast<int16_t>(left + col * pitchX),
                static_cast<int16_t>(top + row * pitchY), keyW, keyH};
}

/* Bottom row, outer two cells. Kept as accessors so the touch handler and the
 * renderer cannot disagree about where they are. */
Rect ProfileApp::pinDeleteRect(int16_t screenW, int16_t screenH) const {
    return pinKeyRect(3, 0, screenW, screenH);
}

Rect ProfileApp::pinConfirmRect(int16_t screenW, int16_t screenH) const {
    return pinKeyRect(3, 2, screenW, screenH);
}

/* Top-left, clear of the pad. Without this a player who taps the admin row is
 * stuck on the PIN screen with no way back to the picker. */
Rect ProfileApp::pinCancelRect(int16_t, int16_t) const {
    return Rect{6, 6, 52, 22};
}

void ProfileApp::beginPinEntry(uint8_t profile, PinPurpose purpose) {
    profileToSwitchTo_ = profile;
    pinPurpose_ = purpose;
    adminPinAttempt_ = 0;
    adminPinDigitCount_ = 0;
    phase_ = Phase::PinEntry;
    markFullDirty();
}

/* The PIN pad's input. Called from update() for every press while the pad
 * is up; the screen-wide guards (a press, the profiles capability) have
 * already passed. */
void ProfileApp::updatePinEntry(GameHost& host, const TouchPoint& touch) {
    const int16_t W = static_cast<int16_t>(host.display().width());
    const int16_t H = static_cast<int16_t>(host.display().height());

    if (pinCancelRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        phase_ = Phase::Pick;
        adminPinAttempt_ = 0;
        adminPinDigitCount_ = 0;
        markFullDirty();
        return;
    }
    if (pinConfirmRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        confirmPin(host);
        return;
    }
    if (pinDeleteRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        deletePinDigit();
        return;
    }
    /* Rows 0-2 carry 1-9; row 3 middle cell is 0. DEL and OK are the outer
     * cells of row 3 and were handled above. */
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t col = 0; col < PIN_PAD_COLS; ++col) {
            if (pinKeyRect(row, col, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                appendPinDigit(static_cast<uint8_t>(row * PIN_PAD_COLS + col + 1));
                return;
            }
        }
    }
    if (pinKeyRect(3, 1, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        appendPinDigit(0);
        return;
    }
    return;
}

/* OK, from the pad or from a keyboard's Enter. */
void ProfileApp::confirmPin(GameHost& host) {
    Board& board = host.board();
    /* Judge only a complete entry, so a wrong-length attempt cannot be
     * used to probe whether a shorter prefix is accepted. */
    if (adminPinDigitCount_ != PIN_LENGTH) {
        board.beepError();
        return;
    }
    const bool ok = (adminPinAttempt_ == board.adminPin());
    adminPinAttempt_ = 0;
    adminPinDigitCount_ = 0;

    if (!ok) {
        board.beepError();
        /* The entered digits have just been reset, so all four dots go
         * back to empty. That is a content change and nothing else on
         * the pad moves, so it is markDirty() -- and the dots erase
         * themselves, each fill covering its own predecessor exactly.
         * A rejected PIN is the moment somebody is about to type four
         * more digits, so it is the last place to want a full wipe. */
        markDirty();
        return;
    }
    board.beepOk();
    if (pinPurpose_ == PinPurpose::OpenMenu) {
        menuFor_ = profileToSwitchTo_;
        phase_ = Phase::Menu;
        markFullDirty();
    } else {
        board.setActiveProfile(profileToSwitchTo_);
        host.goHome();
    }
    return;
}

/* A digit types a digit, Delete and Enter are DEL and OK; anything else on
 * the keyboard is not on this pad and does nothing. */
void ProfileApp::typePinChar(GameHost& host, char typed) {
    if (typed >= '0' && typed <= '9') {
        appendPinDigit(static_cast<uint8_t>(typed - '0'));
    } else if (typed == '\b') {
        deletePinDigit();
    } else if (typed == '\n') {
        confirmPin(host);
    }
}

/* markDirty(), not markFullDirty(): a digit moves four dots and nothing else,
 * and the dots are the whole of renderDynamic() in this phase. Asking for a
 * full repaint here is what put a Ui::clear() on every keypress. */
void ProfileApp::appendPinDigit(uint8_t digit) {
    if (adminPinDigitCount_ < PIN_LENGTH) {
        adminPinAttempt_ = adminPinAttempt_ * 10 + digit;
        adminPinDigitCount_++;
        markDirty();
    }
}

void ProfileApp::deletePinDigit() {
    if (adminPinDigitCount_ > 0) {
        adminPinAttempt_ /= 10;
        adminPinDigitCount_--;
        markDirty();
    }
}

/* THE PAD IS DRAWN IN TWO HALVES, AND THE DIVIDING LINE IS "DOES A DIGIT
 * CHANGE IT?".
 *
 * This is the exception to the full-repaint rule stated below, and it earns
 * it: typing a digit changes four small circles and nothing else, and this
 * function used to open with Ui::clear() -- a 320x240 wipe, ~150 KB over SPI
 * and ~30 ms of visible blanking -- once per keypress, on the one screen where
 * the player is deliberately tapping four times in a row. It was the most
 * noticeable flicker in the firmware for exactly that reason.
 *
 * The heading is chrome, not content: this pad only ever asks one question. */
void ProfileApp::renderPinPadChrome(GameHost& host) {
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());

    Ui::drawButton(tft, pinCancelRect(W, H), "Back", Ui::panel(), Ui::outline(),
                   Ui::text(), false, 1);

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Enter Admin PIN", W / 2, 6, 2);

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

/* The only thing a keypress changes.
 *
 * Four dots, one per digit, filled from the left as digits arrive. The count
 * is tracked separately from the value because "0000" and an empty field are
 * the same number.
 *
 * They need no erase and must not be given one: each is a filled circle at a
 * fixed centre and radius, so the fill covers its own predecessor exactly --
 * including the backwards case, a dot going from filled to empty on DEL or on
 * a rejected PIN. Clearing a rect around them first would only add a flash. */
void ProfileApp::renderPinDots(GameHost& host) {
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());

    const int16_t dotPitch = 26;
    const int16_t dotsX0   = static_cast<int16_t>(W / 2 - (dotPitch * (PIN_LENGTH - 1)) / 2);
    for (uint8_t i = 0; i < PIN_LENGTH; ++i) {
        const int16_t x = static_cast<int16_t>(dotsX0 + i * dotPitch);
        const uint16_t fill = i < adminPinDigitCount_ ? Ui::success() : Ui::panel();
        tft.fillCircle(x, PIN_DOT_Y, PIN_DOT_R, fill);
        tft.drawCircle(x, PIN_DOT_Y, PIN_DOT_R, Ui::outline());
    }
}
