#include "ProfileGame.h"
#include "AppVersion.h"
#include "engine/AppRegistry.h"
#include "hal/Board.h"

namespace {
constexpr uint8_t KEY_COLS = 6;
constexpr uint8_t KEY_ROWS = 5;
const char* const KEYS[KEY_ROWS] = {
    "ABCDEF", "GHIJKL", "MNOPQR", "STUVWX", "YZ -<>"
};
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
 * the title, because "GoodTime Kids!" at font 4 was most of a 240px screen and
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
 * children and the guest) have to fit between them, so the pitch here and the
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

Rect ProfileGame::pinKeyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH) const {
    const int16_t keyW = 50;
    const int16_t keyH = 40;
    const int16_t keySpacingX = 60;
    const int16_t keySpacingY = 50;
    const int16_t keypadStartX = screenW / 2 - (keySpacingX * 3) / 2 - keyW / 2;
    const int16_t keypadStartY = 70;
    return Rect{static_cast<int16_t>(keypadStartX + col * keySpacingX),
                static_cast<int16_t>(keypadStartY + row * keySpacingY), keyW, keyH};
}

Rect ProfileGame::pinDeleteRect(int16_t screenW, int16_t screenH) const {
    const int16_t keyW = 50;
    const int16_t keyH = 40;
    const int16_t buttonY = 270;
    return Rect{static_cast<int16_t>(screenW / 2 - 110), buttonY, keyW, keyH};
}

Rect ProfileGame::pinConfirmRect(int16_t screenW, int16_t screenH) const {
    const int16_t keyW = 50;
    const int16_t keyH = 40;
    const int16_t buttonY = 270;
    return Rect{static_cast<int16_t>(screenW / 2 + 60), buttonY, keyW, keyH};
}

uint8_t ProfileGame::visibleGameRows(int16_t screenH) const {
    const int16_t usable = static_cast<int16_t>(screenH - 92);
    if (usable <= 0) return 3;
    return static_cast<uint8_t>(min<int16_t>(8, max<int16_t>(3, usable / 29)));
}

uint8_t ProfileGame::rowCount(Board& board) const {
    return static_cast<uint8_t>(board.kidCount() + 1);
}

uint8_t ProfileGame::profileForRow(Board& board, uint8_t row) const {
    return (row < board.kidCount()) ? row : Board::GUEST_INDEX;
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
                menuFor_ = prof;
                phase_ = Phase::Menu;
                markFullDirty();
                return;
            }
            if (slotRect(r, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                // Require PIN every time admin profile is accessed
                if (board.isAdminProfile(prof)) {
                    profileToSwitchTo_ = prof;
                    adminPinAttempt_ = 0;
                    phase_ = Phase::PinEntry;
                    markFullDirty();
                    return;
                }
                board.setActiveProfile(prof);
                board.beepOk();
                host.goHome();
                return;
            }
        }
        if (addRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP) &&
            board.kidCount() < Board::MAX_KIDS) {
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
            if (!board.isAdminProfile(menuFor_)) {
                board.removeKid(menuFor_);
                phase_ = Phase::Pick;
                markFullDirty();
            } else {
                board.beepError();
            }
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
                const AppDefinition& app = playableAppAt(gi);
                board.setGameVisibleFor(app.launcherIndex(), menuFor_,
                    !board.gameVisibleFor(app.launcherIndex(), menuFor_, app.defaultVisible()));
                markDirty(); return;
            }
        }
        return;
    }

    if (phase_ == Phase::PinEntry) {
        if (pinConfirmRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (adminPinAttempt_ == board.adminPin()) {
                board.setActiveProfile(profileToSwitchTo_);
                board.beepOk();
                host.goHome();
            } else {
                board.beepError();
                adminPinAttempt_ = 0;
                markDirty();
            }
            return;
        }
        if (pinDeleteRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            deletePinDigit();
            return;
        }
        // Digits 1-9 (3x3 grid)
        for (uint8_t row = 0; row < 3; ++row) {
            for (uint8_t col = 0; col < 3; ++col) {
                if (pinKeyRect(row, col, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                    const uint8_t digit = static_cast<uint8_t>(row * 3 + col + 1);
                    appendPinDigit(digit);
                    return;
                }
            }
        }
        // Digit 0 (row 3, col 1)
        if (pinKeyRect(3, 1, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            appendPinDigit(0);
            return;
        }
        return;
    }

    // ---- rename / create ----
    for (uint8_t r = 0; r < KEY_ROWS; ++r) {
        for (uint8_t c = 0; c < KEY_COLS; ++c) {
            if (!keyRect(r, c, W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
            const char ch = KEYS[r][c];
            if (ch == '<') {
                if (draft_.length() > 0) draft_.remove(draft_.length() - 1);
            } else if (ch == '>') {
                if (editing_ == 0xFF) {
                    board.addKid(draft_);
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

void ProfileGame::appendPinDigit(uint8_t digit) {
    if (adminPinAttempt_ < 9999) {
        adminPinAttempt_ = adminPinAttempt_ * 10 + digit;
        markDirty();
    }
}

void ProfileGame::deletePinDigit() {
    if (adminPinAttempt_ > 0) {
        adminPinAttempt_ /= 10;
        markDirty();
    }
}

void ProfileGame::renderPinEntry(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());
    const bool tall = H > W;

    Ui::clear(tft);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);

    const int16_t titleY = tall ? 20 : 12;
    tft.drawString("Enter Admin PIN", W / 2, titleY, 2);

    // PIN display dots
    const int16_t pinY = tall ? 50 : 40;
    const int16_t dotSpacing = 40;
    const int16_t dotsStartX = W / 2 - (dotSpacing * 3) / 2 - 10;

    uint8_t digitsEntered = 0;
    if (adminPinAttempt_ >= 1000) digitsEntered = 4;
    else if (adminPinAttempt_ >= 100) digitsEntered = 3;
    else if (adminPinAttempt_ >= 10) digitsEntered = 2;
    else if (adminPinAttempt_ > 0) digitsEntered = 1;

    tft.setTextDatum(MC_DATUM);
    for (uint8_t i = 0; i < 4; ++i) {
        const int16_t x = dotsStartX + i * dotSpacing;
        const uint16_t fill = i < digitsEntered ? Ui::rgb(70, 140, 70) : Ui::rgb(100, 100, 100);
        tft.fillCircle(x, pinY, 8, fill);
        tft.drawCircle(x, pinY, 8, Ui::outline());
    }

    // Numeric keypad
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t col = 0; col < 3; ++col) {
            const uint8_t digit = row * 3 + col + 1;
            char label[2] = {static_cast<char>('0' + digit), 0};
            const Rect keyRect = pinKeyRect(row, col, W, H);
            tft.fillRoundRect(keyRect.x, keyRect.y, keyRect.w, keyRect.h, 4, Ui::panel());
            tft.drawRoundRect(keyRect.x, keyRect.y, keyRect.w, keyRect.h, 4, Ui::outline());
            tft.setTextColor(Ui::text(), Ui::panel());
            tft.setTextDatum(MC_DATUM);
            tft.drawString(label, keyRect.x + keyRect.w / 2, keyRect.y + keyRect.h / 2, 2);
        }
    }

    // 0 button (centered)
    const Rect key0Rect = pinKeyRect(3, 1, W, H);
    tft.fillRoundRect(key0Rect.x, key0Rect.y, key0Rect.w, key0Rect.h, 4, Ui::panel());
    tft.drawRoundRect(key0Rect.x, key0Rect.y, key0Rect.w, key0Rect.h, 4, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("0", key0Rect.x + key0Rect.w / 2, key0Rect.y + key0Rect.h / 2, 2);

    // Delete button
    const Rect deleteRect = pinDeleteRect(W, H);
    tft.fillRoundRect(deleteRect.x, deleteRect.y, deleteRect.w, deleteRect.h, 4, Ui::rgb(150, 60, 60));
    tft.drawRoundRect(deleteRect.x, deleteRect.y, deleteRect.w, deleteRect.h, 4, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::rgb(150, 60, 60));
    tft.setTextDatum(MC_DATUM);
    tft.drawString("DEL", deleteRect.x + deleteRect.w / 2, deleteRect.y + deleteRect.h / 2, 1);

    // Confirm button
    const Rect confirmRect = pinConfirmRect(W, H);
    tft.fillRoundRect(confirmRect.x, confirmRect.y, confirmRect.w, confirmRect.h, 4, Ui::rgb(45, 154, 96));
    tft.drawRoundRect(confirmRect.x, confirmRect.y, confirmRect.w, confirmRect.h, 4, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::rgb(45, 154, 96));
    tft.setTextDatum(MC_DATUM);
    tft.drawString("OK", confirmRect.x + confirmRect.w / 2, confirmRect.y + confirmRect.h / 2, 1);

    tft.setTextDatum(TL_DATUM);
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

        // The product renamed, the owner did not: the copyright stays theirs.
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
            Ui::drawButton(tft, slotRect(r, W, H), board.profileName(prof),
                           sel ? Ui::rgb(36, 132, 204) : (guest ? Ui::surface() : Ui::panel()),
                           Ui::outline(), sel ? TFT_WHITE : Ui::text(), false, 2);
            if (!guest) {
                Ui::drawButton(tft, menuRect(r, W, H), "Edit", Ui::surface(),
                               Ui::outline(), Ui::muted(), false, 2);
            }
        }

        const bool canAdd = board.kidCount() < Board::MAX_KIDS;
        char label[16];
        snprintf(label, sizeof(label), canAdd ? "Add Player" : "Max %u",
                 static_cast<unsigned>(Board::MAX_KIDS));
        Ui::drawButton(tft, addRect(W, H), label,
                       canAdd ? Ui::rgb(45, 154, 96) : Ui::surface(),
                       Ui::outline(), canAdd ? TFT_WHITE : Ui::muted(), false, 2);
        Ui::drawButton(tft, doneRect(W, H), "Done", Ui::panel(), Ui::outline(), Ui::text(), false, 2);

        tft.setTextDatum(TL_DATUM);
        return;
    }

    if (phase_ == Phase::Menu) {
        const bool isAdmin = board.isAdminProfile(menuFor_);
        tft.drawString(board.profileName(menuFor_) + (isAdmin ? " (Admin)" : ""), W / 2, tall ? 28 : 24, 4);
        Ui::drawButton(tft, menuActionRect(0, W, H), "Rename", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        Ui::drawButton(tft, menuActionRect(1, W, H), "Games", Ui::rgb(36, 132, 204),
                       Ui::outline(), TFT_WHITE, false, 2);
        Ui::drawButton(tft, menuActionRect(2, W, H), "Remove",
                       isAdmin ? Ui::rgb(80, 80, 80) : Ui::rgb(178, 58, 58),
                       Ui::outline(), isAdmin ? Ui::muted() : TFT_WHITE, false, 2);
        Ui::drawButton(tft, menuActionRect(3, W, H), "Cancel", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        if (isAdmin) {
            tft.drawString("Admin profile cannot be removed", W / 2, static_cast<int16_t>(H - 20), 1);
        } else {
            tft.drawString("Removing also clears their scores", W / 2, static_cast<int16_t>(H - 20), 1);
        }
        tft.setTextDatum(TL_DATUM);
        return;
    }

    if (phase_ == Phase::Games) {
        tft.drawString(board.profileName(menuFor_), W / 2, 8, 4);
        Ui::drawButton(tft, gamesBackRect(W), "Back", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("Tap to show or hide from launcher", W / 2, 30, 1);

        const uint8_t visible = visibleGameRows(H);
        for (uint8_t row = 0; row < visible; ++row) {
            const uint8_t gi = gameScroll_ + row;
            if (gi >= playableAppCount()) break;
            const AppDefinition& app = playableAppAt(gi);
            const Rect r = gameCheckRect(row, W);
            const bool on = board.gameVisibleFor(app.launcherIndex(), menuFor_,
                                                 app.defaultVisible());
            tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, Ui::surface());
            tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
            tft.fillRoundRect(r.x + 4, r.y + 6, 16, 16, 3, on ? Ui::success() : Ui::panel());
            tft.drawRoundRect(r.x + 4, r.y + 6, 16, 16, 3, Ui::outline());
            if (on) {
                tft.setTextColor(TFT_WHITE, Ui::success());
                tft.setTextDatum(MC_DATUM);
                tft.drawString("v", r.x + 12, r.y + 14, 1);
            }
            tft.setTextColor(Ui::text(), Ui::surface());
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
    tft.drawString(editing_ == 0xFF ? "New player" : "Name", W / 2, 8, 2);
    const int16_t fieldW = static_cast<int16_t>(min<int16_t>(240, W - 40));
    const Rect field{static_cast<int16_t>((W - fieldW) / 2), 28, fieldW, 30};
    tft.fillRoundRect(field.x, field.y, field.w, field.h, 4, Ui::surface());
    tft.drawRoundRect(field.x, field.y, field.w, field.h, 4, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(draft_.length() ? draft_.c_str() : "...",
                   W / 2, static_cast<int16_t>(field.y + field.h / 2), 4);

    for (uint8_t r = 0; r < KEY_ROWS; ++r) {
        for (uint8_t c = 0; c < KEY_COLS; ++c) {
            const char ch = KEYS[r][c];
            char keyLabel[2] = {ch, 0};
            const char* label = keyLabel;
            uint16_t fill = Ui::panel();
            if (ch == '<') { label = "DEL"; fill = Ui::rgb(150, 60, 60); }
            if (ch == '>') { label = "OK";  fill = Ui::rgb(45, 154, 96); }
            if (ch == ' ') { label = "_"; }
            Ui::drawButton(tft, keyRect(r, c, W, H), label, fill, Ui::outline(), Ui::text(), false, 2);
        }
    }
    tft.setTextDatum(TL_DATUM);
}
