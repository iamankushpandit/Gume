#include "ProfileGame.h"
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

Rect ProfileGame::headerRect(int16_t screenW) const {
    return Rect{0, 0, screenW, 30};
}

Rect ProfileGame::slotRect(uint8_t i, int16_t screenW, int16_t screenH) const {
    const bool tall = screenH > screenW;
    const int16_t menuW = tall ? 54 : 62;
    const int16_t rowH = tall ? 33 : 23;
    const int16_t pitch = tall ? 38 : 25;
    const int16_t y0 = tall ? 62 : 58;
    return Rect{8, static_cast<int16_t>(y0 + i * pitch),
                static_cast<int16_t>(screenW - 22 - menuW), rowH};
}
Rect ProfileGame::menuRect(uint8_t i, int16_t screenW, int16_t screenH) const {
    const bool tall = screenH > screenW;
    const int16_t menuW = tall ? 54 : 62;
    const int16_t rowH = tall ? 33 : 23;
    const int16_t pitch = tall ? 38 : 25;
    const int16_t y0 = tall ? 62 : 58;
    return Rect{static_cast<int16_t>(screenW - 8 - menuW),
                static_cast<int16_t>(y0 + i * pitch), menuW, rowH};
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
            board.removeKid(menuFor_);
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
                const AppDefinition& app = playableAppAt(gi);
                board.setGameVisibleFor(app.launcherIndex(), menuFor_,
                    !board.gameVisibleFor(app.launcherIndex(), menuFor_, app.defaultVisible()));
                markDirty(); return;
            }
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

void ProfileGame::render(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());
    const bool tall = H > W;
    Ui::clear(tft);

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);

    if (phase_ == Phase::Pick) {
        const Rect header = headerRect(W);
        tft.fillRect(header.x, header.y, header.w, header.h, Ui::surface());

        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(ML_DATUM);
        tft.drawString("GoodTime Kids!", 10, 15, 4);

        if (tall) {
            tft.setTextColor(Ui::muted(), Ui::bg());
            tft.setTextDatum(TL_DATUM);
            tft.drawString("(C) GoodTime Micro", 10, 32, 1);
            tft.setTextColor(Ui::text(), Ui::bg());
            tft.setTextDatum(TC_DATUM);
            tft.drawString("Who is playing?", W / 2, 48, 2);
            tft.setTextColor(Ui::muted(), Ui::bg());
            tft.setTextDatum(TC_DATUM);
            tft.drawString("Guest plays without saving scores", W / 2, 62, 1);
        } else {
            tft.setTextColor(Ui::muted(), Ui::surface());
            tft.setTextDatum(MR_DATUM);
            tft.drawString("(C) GoodTime Micro", W - 8, 15, 1);
            tft.setTextColor(Ui::text(), Ui::bg());
            tft.setTextDatum(TC_DATUM);
            tft.drawString("Who is playing?", W / 2, 34, 2);
            tft.setTextColor(Ui::muted(), Ui::bg());
            tft.setTextDatum(TC_DATUM);
            tft.drawString("Guest plays without saving scores", W / 2, 49, 1);
        }

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
        tft.drawString(board.profileName(menuFor_), W / 2, tall ? 28 : 24, 4);
        Ui::drawButton(tft, menuActionRect(0, W, H), "Rename", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        Ui::drawButton(tft, menuActionRect(1, W, H), "Games", Ui::rgb(36, 132, 204),
                       Ui::outline(), TFT_WHITE, false, 2);
        Ui::drawButton(tft, menuActionRect(2, W, H), "Remove", Ui::rgb(178, 58, 58),
                       Ui::outline(), TFT_WHITE, false, 2);
        Ui::drawButton(tft, menuActionRect(3, W, H), "Cancel", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("Removing also clears their scores", W / 2, static_cast<int16_t>(H - 20), 1);
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
