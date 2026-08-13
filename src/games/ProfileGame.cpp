#include "ProfileGame.h"

namespace {
constexpr uint8_t KEY_COLS = 6;
constexpr uint8_t KEY_ROWS = 5;
const char* const KEYS[KEY_ROWS] = {
    "ABCDEF", "GHIJKL", "MNOPQR", "STUVWX", "YZ -<>"
};
}

const char* ProfileGame::title() const { return "Profiles"; }

void ProfileGame::begin(GameHost&) {
    phase_ = Phase::Pick;
    draft_ = "";
    menuFor_ = 0;
    gameScroll_ = 0;
    markFullDirty();
}

Rect ProfileGame::slotRect(uint8_t i) const {
    return Rect{8, static_cast<int16_t>(34 + i * 29), 236, 27};
}
Rect ProfileGame::menuRect(uint8_t i) const {
    return Rect{250, static_cast<int16_t>(34 + i * 29), 62, 27};
}
Rect ProfileGame::addRect() const { return Rect{8, 210, 150, 26}; }
Rect ProfileGame::doneRect() const { return Rect{164, 210, 148, 26}; }

Rect ProfileGame::keyRect(uint8_t row, uint8_t col) const {
    return Rect{static_cast<int16_t>(14 + col * 49),
                static_cast<int16_t>(86 + row * 29), 45, 26};
}
Rect ProfileGame::menuActionRect(uint8_t i) const {
    return Rect{60, static_cast<int16_t>(64 + i * 38), 200, 34};
}

Rect ProfileGame::gameCheckRect(uint8_t row) const {
    return Rect{8, static_cast<int16_t>(62 + row * 29), 304, 27};
}
Rect ProfileGame::gamesBackRect() const { return Rect{248, 4, 64, 24}; }
Rect ProfileGame::gamesPrevRect() const { return Rect{8, 210, 92, 25}; }
Rect ProfileGame::gamesNextRect() const { return Rect{212, 210, 92, 25}; }

uint8_t ProfileGame::rowCount(Board& board) const {
    return static_cast<uint8_t>(board.kidCount() + 1);
}

uint8_t ProfileGame::profileForRow(Board& board, uint8_t row) const {
    return (row < board.kidCount()) ? row : Board::GUEST_INDEX;
}

void ProfileGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;
    Board& board = host.board();

    if (phase_ == Phase::Pick) {
        const uint8_t rows = rowCount(board);
        for (uint8_t r = 0; r < rows; ++r) {
            const uint8_t prof = profileForRow(board, r);
            if (menuRect(r).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                if (prof == Board::GUEST_INDEX) return;
                menuFor_ = prof;
                phase_ = Phase::Menu;
                markFullDirty();
                return;
            }
            if (slotRect(r).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                board.setActiveProfile(prof);
                board.beepOk();
                host.goHome();
                return;
            }
        }
        if (addRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) &&
            board.kidCount() < Board::MAX_KIDS) {
            editing_ = 0xFF;
            draft_ = "";
            phase_ = Phase::Rename;
            markFullDirty();
            return;
        }
        if (doneRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.goHome();
        }
        return;
    }

    if (phase_ == Phase::Menu) {
        if (menuActionRect(0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            editing_ = menuFor_;
            draft_ = board.profileName(menuFor_);
            phase_ = Phase::Rename;
            markFullDirty();
            return;
        }
        if (menuActionRect(1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            gameScroll_ = 0;
            phase_ = Phase::Games;
            markFullDirty();
            return;
        }
        if (menuActionRect(2).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.removeKid(menuFor_);
            phase_ = Phase::Pick;
            markFullDirty();
            return;
        }
        if (menuActionRect(3).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::Pick;
            markFullDirty();
        }
        return;
    }

    if (phase_ == Phase::Games) {
        constexpr uint8_t VISIBLE = 5;
        if (gamesBackRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::Pick;
            markFullDirty();
            return;
        }
        if (gamesPrevRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) && gameScroll_ > 0) {
            gameScroll_ = static_cast<uint8_t>(gameScroll_ >= VISIBLE ? gameScroll_ - VISIBLE : 0);
            markDirty(); return;
        }
        if (gamesNextRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) &&
            gameScroll_ + VISIBLE < GAME_CATALOG_COUNT) {
            gameScroll_ = static_cast<uint8_t>(gameScroll_ + VISIBLE);
            markDirty(); return;
        }
        for (uint8_t row = 0; row < VISIBLE; ++row) {
            const uint8_t gi = gameScroll_ + row;
            if (gi >= GAME_CATALOG_COUNT) break;
            if (gameCheckRect(row).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                board.setGameVisibleFor(gi, menuFor_,
                    !board.gameVisibleFor(gi, menuFor_));
                markDirty(); return;
            }
        }
        return;
    }

    // ---- rename / create ----
    for (uint8_t r = 0; r < KEY_ROWS; ++r) {
        for (uint8_t c = 0; c < KEY_COLS; ++c) {
            if (!keyRect(r, c).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
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
    TFT_eSPI& tft = board.display();
    Ui::clear(tft);

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);

    if (phase_ == Phase::Pick) {
        tft.drawString("Who is playing?", SCREEN_WIDTH / 2, 8, 2);

        const uint8_t active = board.activeProfile();
        const uint8_t rows = rowCount(board);
        for (uint8_t r = 0; r < rows; ++r) {
            const uint8_t prof = profileForRow(board, r);
            const bool sel = (prof == active);
            const bool guest = (prof == Board::GUEST_INDEX);
            Ui::drawButton(tft, slotRect(r), board.profileName(prof),
                           sel ? Ui::rgb(36, 132, 204) : (guest ? Ui::surface() : Ui::panel()),
                           Ui::outline(), sel ? TFT_WHITE : Ui::text(), false, 2);
            if (!guest) {
                Ui::drawButton(tft, menuRect(r), "Edit", Ui::surface(),
                               Ui::outline(), Ui::muted(), false, 2);
            }
        }

        const bool canAdd = board.kidCount() < Board::MAX_KIDS;
        Ui::drawButton(tft, addRect(),
                       canAdd ? String("Add kid") : String("Max ") + Board::MAX_KIDS,
                       canAdd ? Ui::rgb(45, 154, 96) : Ui::surface(),
                       Ui::outline(), canAdd ? TFT_WHITE : Ui::muted(), false, 2);
        Ui::drawButton(tft, doneRect(), "Done", Ui::panel(), Ui::outline(), Ui::text(), false, 2);

        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("Guest plays without saving scores", SCREEN_WIDTH / 2, 24, 1);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    if (phase_ == Phase::Menu) {
        tft.drawString(board.profileName(menuFor_), SCREEN_WIDTH / 2, 24, 4);
        Ui::drawButton(tft, menuActionRect(0), "Rename", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        Ui::drawButton(tft, menuActionRect(1), "Games", Ui::rgb(36, 132, 204),
                       Ui::outline(), TFT_WHITE, false, 2);
        Ui::drawButton(tft, menuActionRect(2), "Remove", Ui::rgb(178, 58, 58),
                       Ui::outline(), TFT_WHITE, false, 2);
        Ui::drawButton(tft, menuActionRect(3), "Cancel", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("Removing also clears their scores", SCREEN_WIDTH / 2, 220, 1);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    if (phase_ == Phase::Games) {
        tft.drawString(board.profileName(menuFor_), SCREEN_WIDTH / 2, 8, 4);
        Ui::drawButton(tft, gamesBackRect(), "Back", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("Tap to show or hide from launcher", SCREEN_WIDTH / 2, 30, 1);

        constexpr uint8_t VISIBLE = 5;
        for (uint8_t row = 0; row < VISIBLE; ++row) {
            const uint8_t gi = gameScroll_ + row;
            if (gi >= GAME_CATALOG_COUNT) break;
            const Rect r = gameCheckRect(row);
            const bool on = board.gameVisibleFor(gi, menuFor_);
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
            tft.drawString(GAME_CATALOG[gi].label, r.x + 28, r.y + r.h / 2, 2);
        }

        const bool canPrev = gameScroll_ > 0;
        const bool canNext = gameScroll_ + VISIBLE < GAME_CATALOG_COUNT;
        Ui::drawPagerButton(tft, gamesPrevRect(), "Prev", canPrev);
        Ui::drawPagerButton(tft, gamesNextRect(), "Next", canNext);

        const uint8_t page  = static_cast<uint8_t>(gameScroll_ / VISIBLE + 1);
        const uint8_t pages = static_cast<uint8_t>((GAME_CATALOG_COUNT + VISIBLE - 1) / VISIBLE);
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(String(page) + "/" + pages, SCREEN_WIDTH / 2, 222, 2);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    // ---- rename / create ----
    tft.drawString(editing_ == 0xFF ? "New kid" : "Name", SCREEN_WIDTH / 2, 8, 2);
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
