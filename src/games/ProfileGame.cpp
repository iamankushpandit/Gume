#include "ProfileGame.h"
#include "ProfileRename.h"
#include "AppVersion.h"
#include "engine/AppRegistry.h"
#include "hal/Board.h"

namespace {
constexpr uint8_t KEY_COLS = 6;
constexpr uint8_t KEY_ROWS = 5;
const char* const KEYS[KEY_ROWS] = {
    "ABCDEF", "GHIJKL", "MNOPQR", "STUVWX", "YZ -<>"
};

/* PIN pad geometry. Four rows: 1-9 then the DEL / 0 / OK row. PIN_PAD_TOP
 * clears the title (font 2, 16px) and the row of PIN dots below it. */
constexpr uint8_t PIN_PAD_COLS = 3;
constexpr uint8_t PIN_PAD_ROWS = 4;
constexpr int16_t PIN_PAD_TOP  = 58;
constexpr int16_t PIN_DOT_Y    = 38;
constexpr int16_t PIN_DOT_R    = 7;
constexpr uint8_t PIN_LENGTH   = 4;

/* Small padlock, centred on (cx, cy): a 9x7 body with a shackle arc above it.
 * Drawn from primitives rather than added to LauncherIcons because it is a
 * row badge, not a launcher tile. */
void drawLockBadge(Ui::Renderer& tft, int16_t cx, int16_t cy, uint16_t colour) {
    const int16_t bodyW = 9, bodyH = 7;
    const int16_t bx = static_cast<int16_t>(cx - bodyW / 2);
    const int16_t by = static_cast<int16_t>(cy - 1);
    tft.fillRoundRect(bx, by, bodyW, bodyH, 2, colour);
    // Shackle: two uprights and a cap, one pixel in from each body edge.
    tft.drawFastVLine(static_cast<int16_t>(bx + 2), static_cast<int16_t>(by - 4), 4, colour);
    tft.drawFastVLine(static_cast<int16_t>(bx + bodyW - 3), static_cast<int16_t>(by - 4), 4, colour);
    tft.drawFastHLine(static_cast<int16_t>(bx + 2), static_cast<int16_t>(by - 5),
                      static_cast<int16_t>(bodyW - 4), colour);
}
}

const char* ProfileGame::title() const { return "Profiles"; }

void ProfileGame::begin(GameHost& host) {
    (void)host.requireCapability(APP_CAP_PROFILES, "open profiles");
    phase_ = Phase::Pick;
    draft_ = "";
    menuFor_ = 0;
    gameScroll_ = 0;
    markFullDirty();
}

/* One header bar, both orientations.
 *
 * This briefly needed a taller portrait bar with the copyright stacked under
 * the title, because the old product mark at font 4 was most of a 240px screen and
 * the two collided. "Braino!" is half the width and the collision is gone: the
 * title runs to about x=80 in portrait and the copyright right-aligns from
 * x=124, so a single 30px bar carries both. Rename the product to something
 * long again and this is the first thing to re-measure -- portrait is the tight
 * one, and the row pitch below depends on the bar staying 30px. */
Rect ProfileGame::headerRect(int16_t screenW, int16_t screenH) const {
    (void)screenH;
    return Rect{0, 0, screenW, 30};
}

/* Rows are packed against both ends: the header plus its two lines of prompt
 * text above, and the Add / Done buttons at screenH - 30 below. Six rows (five
 * players and the guest) have to fit between them, so the pitch here and the
 * prompt baselines in render() are one budget -- move either and re-check the
 * last row against the buttons. */
namespace {
constexpr int16_t rowsTop(bool tall)   { return tall ? 62 : 60; }
constexpr int16_t rowsPitch(bool tall) { return tall ? 38 : 25; }
constexpr int16_t rowsHeight(bool tall){ return tall ? 33 : 23; }
constexpr int16_t rowsMenuW(bool tall) { return tall ? 54 : 62; }
}   // namespace

Rect ProfileGame::slotRect(uint8_t i, int16_t screenW, int16_t screenH) const {
    const bool tall = screenH > screenW;
    return Rect{8, static_cast<int16_t>(rowsTop(tall) + i * rowsPitch(tall)),
                static_cast<int16_t>(screenW - 22 - rowsMenuW(tall)), rowsHeight(tall)};
}
Rect ProfileGame::menuRect(uint8_t i, int16_t screenW, int16_t screenH) const {
    const bool tall = screenH > screenW;
    return Rect{static_cast<int16_t>(screenW - 8 - rowsMenuW(tall)),
                static_cast<int16_t>(rowsTop(tall) + i * rowsPitch(tall)),
                rowsMenuW(tall), rowsHeight(tall)};
}
Rect ProfileGame::addRect(int16_t screenW, int16_t screenH) const {
    const int16_t w = static_cast<int16_t>((screenW - 24) / 2);
    return Rect{8, static_cast<int16_t>(screenH - 30), w, 26};
}
Rect ProfileGame::doneRect(int16_t screenW, int16_t screenH) const {
    const int16_t w = static_cast<int16_t>((screenW - 24) / 2);
    return Rect{static_cast<int16_t>(16 + w), static_cast<int16_t>(screenH - 30), w, 26};
}

Rect ProfileGame::keyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH) const {
    const bool tall = screenH > screenW;
    const int16_t margin = 8;
    const int16_t gap = 4;
    const int16_t keyW = static_cast<int16_t>((screenW - 2 * margin - (KEY_COLS - 1) * gap) / KEY_COLS);
    const int16_t keyH = tall ? 36 : 26;
    const int16_t pitch = tall ? 40 : 29;
    const int16_t y0 = tall ? 92 : 86;
    return Rect{static_cast<int16_t>(margin + col * (keyW + gap)),
                static_cast<int16_t>(y0 + row * pitch), keyW, keyH};
}
Rect ProfileGame::menuActionRect(uint8_t i, int16_t screenW, int16_t screenH) const {
    const bool tall = screenH > screenW;
    const int16_t actionW = static_cast<int16_t>(min<int16_t>(200, screenW - 40));
    const int16_t actionH = tall ? 38 : 34;
    const int16_t y0 = tall ? 82 : 64;
    const int16_t pitch = tall ? 44 : 38;
    return Rect{static_cast<int16_t>((screenW - actionW) / 2),
                static_cast<int16_t>(y0 + i * pitch), actionW, actionH};
}

Rect ProfileGame::gameCheckRect(uint8_t row, int16_t screenW) const {
    return Rect{8, static_cast<int16_t>(62 + row * 29),
                static_cast<int16_t>(screenW - 16), 27};
}
Rect ProfileGame::gamesBackRect(int16_t screenW) const {
    return Rect{static_cast<int16_t>(screenW - 72), 4, 64, 24};
}
Rect ProfileGame::gamesPrevRect(int16_t screenH) const {
    return Rect{8, static_cast<int16_t>(screenH - 30), 92, 25};
}
Rect ProfileGame::gamesNextRect(int16_t screenW, int16_t screenH) const {
    return Rect{static_cast<int16_t>(screenW - 100), static_cast<int16_t>(screenH - 30), 92, 25};
}

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
Rect ProfileGame::pinKeyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH) const {
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
Rect ProfileGame::pinDeleteRect(int16_t screenW, int16_t screenH) const {
    return pinKeyRect(3, 0, screenW, screenH);
}

Rect ProfileGame::pinConfirmRect(int16_t screenW, int16_t screenH) const {
    return pinKeyRect(3, 2, screenW, screenH);
}

/* Top-left, clear of the pad. Without this a player who taps the admin row is
 * stuck on the PIN screen with no way back to the picker. */
Rect ProfileGame::pinCancelRect(int16_t, int16_t) const {
    return Rect{6, 6, 52, 22};
}

void ProfileGame::beginPinEntry(uint8_t profile, PinPurpose purpose) {
    profileToSwitchTo_ = profile;
    pinPurpose_ = purpose;
    adminPinAttempt_ = 0;
    adminPinDigitCount_ = 0;
    phase_ = Phase::PinEntry;
    markFullDirty();
}

uint8_t ProfileGame::visibleGameRows(int16_t screenH) const {
    const int16_t usable = static_cast<int16_t>(screenH - 92);
    if (usable <= 0) return 3;
    return static_cast<uint8_t>(min<int16_t>(8, max<int16_t>(3, usable / 29)));
}

uint8_t ProfileGame::rowCount(Board& board) const {
    return static_cast<uint8_t>(board.playerCount() + 1);
}

uint8_t ProfileGame::profileForRow(Board& board, uint8_t row) const {
    return (row < board.playerCount()) ? row : Board::GUEST_INDEX;
}

void ProfileGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;
    if (!host.requireCapability(APP_CAP_PROFILES, "manage profiles")) {
        return;
    }
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());

    if (phase_ == Phase::Pick) {
        const uint8_t rows = rowCount(board);
        for (uint8_t r = 0; r < rows; ++r) {
            const uint8_t prof = profileForRow(board, r);
            if (menuRect(r, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                if (prof == Board::GUEST_INDEX) return;
                /* Edit leads to rename and to the admin's own game list, so it
                 * is as privileged as switching into the profile and is gated
                 * the same way. */
                if (board.isAdminProfile(prof)) {
                    beginPinEntry(prof, PinPurpose::OpenMenu);
                    return;
                }
                menuFor_ = prof;
                phase_ = Phase::Menu;
                markFullDirty();
                return;
            }
            if (slotRect(r, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                /* Every time, not just from a non-admin profile: "already
                 * admin" is not evidence that the person holding the device is
                 * the one who typed the PIN. */
                if (board.isAdminProfile(prof)) {
                    beginPinEntry(prof, PinPurpose::Switch);
                    return;
                }
                board.setActiveProfile(prof);
                board.beepOk();
                host.goHome();
                return;
            }
        }
        if (addRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP) &&
            board.playerCount() < Board::MAX_PLAYERS) {
            editing_ = 0xFF;
            draft_ = "";
            phase_ = Phase::Rename;
            markFullDirty();
            return;
        }
        if (doneRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.goHome();
        }
        return;
    }

    if (phase_ == Phase::Menu) {
        if (menuActionRect(0, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            editing_ = menuFor_;
            draft_ = board.profileName(menuFor_);
            phase_ = Phase::Rename;
            markFullDirty();
            return;
        }
        if (menuActionRect(1, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            gameScroll_ = 0;
            phase_ = Phase::Games;
            markFullDirty();
            return;
        }
        if (menuActionRect(2, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            /* Two separate conditions, and both matter. The admin profile
             * cannot be removed by anybody; and removing anyone else is the
             * admin's call, because it destroys that player's scores and
             * mastery data for good. Any player could previously delete a
             * sibling in two taps with no confirmation. */
            if (board.isAdminProfile(menuFor_) ||
                !board.isAdminProfile(board.activeProfile())) {
                board.beepError();
                return;
            }
            board.removePlayer(menuFor_);
            phase_ = Phase::Pick;
            markFullDirty();
            return;
        }
        if (menuActionRect(3, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::Pick;
            markFullDirty();
        }
        return;
    }

    if (phase_ == Phase::Games) {
        const uint8_t visible = visibleGameRows(H);
        if (gamesBackRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::Pick;
            markFullDirty();
            return;
        }
        if (gamesPrevRect(H).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && gameScroll_ > 0) {
            gameScroll_ = static_cast<uint8_t>(gameScroll_ >= visible ? gameScroll_ - visible : 0);
            markDirty(); return;
        }
        if (gamesNextRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP) &&
            gameScroll_ + visible < playableAppCount()) {
            gameScroll_ = static_cast<uint8_t>(gameScroll_ + visible);
            markDirty(); return;
        }
        for (uint8_t row = 0; row < visible; ++row) {
            const uint8_t gi = gameScroll_ + row;
            if (gi >= playableAppCount()) break;
            if (gameCheckRect(row, W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                /* Who may play what is the admin's decision, for every player
                 * including themselves. Anyone may read the list -- a player
                 * seeing which games are switched off is fine, and better than
                 * a launcher that is short for reasons nobody will explain --
                 * but only the admin may change it. Without this the feature
                 * enforced nothing: a player opened their own row and turned
                 * back on everything that had been hidden from them. */
                if (!board.isAdminProfile(board.activeProfile())) {
                    board.beepError();
                    return;
                }
                const AppDefinition& app = playableAppAt(gi);
                board.setGameVisibleFor(app.launcherIndex(), menuFor_,
                    !board.gameVisibleFor(app.launcherIndex(), menuFor_, app.defaultVisible()));
                markDirty(); return;
            }
        }
        return;
    }

    if (phase_ == Phase::PinEntry) {
        if (pinCancelRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::Pick;
            adminPinAttempt_ = 0;
            adminPinDigitCount_ = 0;
            markFullDirty();
            return;
        }
        if (pinConfirmRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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

    // ---- rename / create ----
    updateRename(host, touch);
}

void ProfileGame::appendPinDigit(uint8_t digit) {
    if (adminPinDigitCount_ < PIN_LENGTH) {
        adminPinAttempt_ = adminPinAttempt_ * 10 + digit;
        adminPinDigitCount_++;
        markDirty();
    }
}

void ProfileGame::deletePinDigit() {
    if (adminPinDigitCount_ > 0) {
        adminPinAttempt_ /= 10;
        adminPinDigitCount_--;
        markDirty();
    }
}

void ProfileGame::renderPinEntry(GameHost& host) {
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());

    Ui::clear(tft);

    Ui::drawButton(tft, pinCancelRect(W, H), "Back", Ui::panel(), Ui::outline(),
                   Ui::text(), false, 1);

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Enter Admin PIN", W / 2, 6, 2);

    /* Four dots, one per digit, filled from the left as digits arrive. The
     * count is tracked separately from the value because "0000" and an empty
     * field are the same number. */
    const int16_t dotPitch = 26;
    const int16_t dotsX0   = static_cast<int16_t>(W / 2 - (dotPitch * (PIN_LENGTH - 1)) / 2);
    for (uint8_t i = 0; i < PIN_LENGTH; ++i) {
        const int16_t x = static_cast<int16_t>(dotsX0 + i * dotPitch);
        const uint16_t fill = i < adminPinDigitCount_ ? Ui::success() : Ui::panel();
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

bool ProfileGame::renderChrome(GameHost& host) {
    (void)host;
    return false;   // see the header -- nothing here to repaint in isolation
}

void ProfileGame::render(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());
    const bool tall = H > W;

    if (phase_ == Phase::PinEntry) {
        renderPinEntry(host);
        return;
    }

    Ui::clear(tft);

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);

    if (phase_ == Phase::Pick) {
        const Rect header = headerRect(W, H);
        tft.fillRect(header.x, header.y, header.w, header.h, Ui::surface());

        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(ML_DATUM);
        tft.drawString(BRAINO_PRODUCT_NAME, 10, 15, 4);

        // The mark is the brand's, the copyright is the author's -- two
        // different facts, and AppVersion.h is where both are spelled.
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.setTextDatum(MR_DATUM);
        tft.drawString(BRAINO_COPYRIGHT_SHORT, W - 8, 15, 1);

        /* Both lines below sit on the background between the header and the
         * first row. They used to overlap each other by a pixel, and in
         * portrait the guest hint ran straight through the top row -- font 2 is
         * 16px tall and font 1 is 8, so the baselines have to be spaced for the
         * font, not eyeballed. Keep the last line clear of rowsTop(). */
        const int16_t promptY = 32;
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("Who is playing?", W / 2, promptY, 2);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("Guest plays without saving scores",
                       W / 2, static_cast<int16_t>(promptY + 18), 1);

        const uint8_t active = board.activeProfile();
        const uint8_t rows = rowCount(board);
        for (uint8_t r = 0; r < rows; ++r) {
            const uint8_t prof = profileForRow(board, r);
            const bool sel = (prof == active);
            const bool guest = (prof == Board::GUEST_INDEX);
            const Rect slot = slotRect(r, W, H);
            Ui::drawButton(tft, slot, board.profileName(prof),
                           sel ? Ui::rgb(36, 132, 204) : (guest ? Ui::surface() : Ui::panel()),
                           Ui::outline(), sel ? TFT_WHITE : Ui::text(), false, 2);

            /* A padlock on the admin's row. This is the only cue in the picker
             * that the row will ask for a PIN, so it has to be visible before
             * the tap, not after. Drawn at the right edge of the slot, clear of
             * the centred name. */
            if (board.isAdminProfile(prof)) {
                drawLockBadge(tft, static_cast<int16_t>(slot.x + slot.w - 14),
                              static_cast<int16_t>(slot.y + slot.h / 2),
                              sel ? TFT_WHITE : Ui::muted());
            }

            if (!guest) {
                Ui::drawButton(tft, menuRect(r, W, H), "Edit", Ui::surface(),
                               Ui::outline(), Ui::muted(), false, 2);
            }
        }

        const bool canAdd = board.playerCount() < Board::MAX_PLAYERS;
        char label[16];
        snprintf(label, sizeof(label), canAdd ? "Add Player" : "Max %u",
                 static_cast<unsigned>(Board::MAX_PLAYERS));
        Ui::drawButton(tft, addRect(W, H), label,
                       canAdd ? Ui::rgb(45, 154, 96) : Ui::surface(),
                       Ui::outline(), canAdd ? TFT_WHITE : Ui::muted(), false, 2);
        Ui::drawButton(tft, doneRect(W, H), "Done", Ui::panel(), Ui::outline(), Ui::text(), false, 2);

        tft.setTextDatum(TL_DATUM);
        return;
    }

    if (phase_ == Phase::Menu) {
        /* Two different questions, and conflating them is how Remove ended up
         * available to everyone: whether the profile being edited is the
         * admin, and whether the person doing the editing is. */
        const bool targetIsAdmin = board.isAdminProfile(menuFor_);
        const bool actorIsAdmin  = board.isAdminProfile(board.activeProfile());
        const bool canRemove     = actorIsAdmin && !targetIsAdmin;

        tft.drawString(board.profileName(menuFor_) + (targetIsAdmin ? " (Admin)" : ""),
                       W / 2, tall ? 28 : 24, 4);
        Ui::drawButton(tft, menuActionRect(0, W, H), "Rename", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        Ui::drawButton(tft, menuActionRect(1, W, H), "Games", Ui::rgb(36, 132, 204),
                       Ui::outline(), TFT_WHITE, false, 2);
        Ui::drawButton(tft, menuActionRect(2, W, H), "Remove",
                       canRemove ? Ui::rgb(178, 58, 58) : Ui::rgb(80, 80, 80),
                       Ui::outline(), canRemove ? TFT_WHITE : Ui::muted(), false, 2);
        Ui::drawButton(tft, menuActionRect(3, W, H), "Cancel", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        const char* note = targetIsAdmin ? "Admin profile cannot be removed"
                         : !actorIsAdmin ? "Only the admin can remove a player"
                                         : "Removing also clears their scores";
        tft.drawString(note, W / 2, static_cast<int16_t>(H - 20), 1);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    if (phase_ == Phase::Games) {
        /* Read-only unless the admin is the one looking. The hint line has to
         * say which it is: a checkbox that looks live and does nothing is
         * worse than one that admits it is locked. */
        const bool canEdit = board.isAdminProfile(board.activeProfile());

        tft.drawString(board.profileName(menuFor_), W / 2, 8, 4);
        Ui::drawButton(tft, gamesBackRect(W), "Back", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString(canEdit ? "Tap to show or hide from launcher"
                               : "Only the admin can change these",
                       W / 2, 30, 1);

        const uint8_t visible = visibleGameRows(H);
        for (uint8_t row = 0; row < visible; ++row) {
            const uint8_t gi = gameScroll_ + row;
            if (gi >= playableAppCount()) break;
            const AppDefinition& app = playableAppAt(gi);
            const Rect r = gameCheckRect(row, W);
            const bool on = board.gameVisibleFor(app.launcherIndex(), menuFor_,
                                                 app.defaultVisible());
            const uint16_t tick = canEdit ? Ui::success() : Ui::rgb(70, 96, 78);
            tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, Ui::surface());
            tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
            tft.fillRoundRect(r.x + 4, r.y + 6, 16, 16, 3, on ? tick : Ui::panel());
            tft.drawRoundRect(r.x + 4, r.y + 6, 16, 16, 3, Ui::outline());
            if (on) {
                tft.setTextColor(canEdit ? TFT_WHITE : Ui::muted(), tick);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("v", r.x + 12, r.y + 14, 1);
            }
            tft.setTextColor(canEdit ? Ui::text() : Ui::muted(), Ui::surface());
            tft.setTextDatum(ML_DATUM);
            tft.drawString(app.label(), r.x + 28, r.y + r.h / 2, 2);
        }

        const bool canPrev = gameScroll_ > 0;
        const bool canNext = gameScroll_ + visible < playableAppCount();
        Ui::drawPagerButton(tft, gamesPrevRect(H), "Prev", canPrev);
        Ui::drawPagerButton(tft, gamesNextRect(W, H), "Next", canNext);

        const uint8_t page  = static_cast<uint8_t>(gameScroll_ / visible + 1);
        const uint8_t pages = static_cast<uint8_t>((playableAppCount() + visible - 1) / visible);
        char pager[8];
        snprintf(pager, sizeof(pager), "%u/%u", page, pages);
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(pager, W / 2, static_cast<int16_t>(H - 18), 2);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    // ---- rename / create ----
    renderRename(host);
}
