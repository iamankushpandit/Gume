// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "LudoGame.h"

#include "ui/Stops.h"

/* The panel beside the board: whose turn it is, the die, one line of news,
 * the seats and their places, and the one button. Split from LudoBoard.cpp for
 * size. Each part is repainted on its own when what it shows changes -- see
 * LudoGame::renderDynamic().
 *
 * The lobby before a game on this console -- who sits in each seat, and how
 * hard the computer plays -- is the second half of this file, drawn and
 * answered here. (The table lobby is LudoLobby.cpp.) */

namespace {

uint16_t dieFace() { return Ui::rgb(252, 252, 250); }

/* The panel beside the board. Its x is asserted against the board in
 * LudoGame::dieRect(), where the board's own constants are in scope. */
constexpr int16_t PANEL_X = 209;
constexpr int16_t PANEL_W = GAME_CANVAS_WIDTH - PANEL_X - 6;
constexpr Rect TURN_RECT{PANEL_X, 38, PANEL_W, 18};
constexpr Rect DIE_RECT{PANEL_X + (PANEL_W - 44) / 2, 62, 44, 44};
constexpr Rect MESSAGE_RECT{PANEL_X, 112, PANEL_W, 18};
constexpr int16_t SEAT_ROW_H = 17;
constexpr Rect SEATS_RECT{PANEL_X, 132, PANEL_W, 4 * SEAT_ROW_H};
constexpr Rect ACTION_RECT{PANEL_X + 6, 204, PANEL_W - 14, 28};
static_assert(PANEL_W >= 100, "the panel must hold 'Yellow (CPU)' at font 2");
static_assert(SEATS_RECT.y + SEATS_RECT.h <= ACTION_RECT.y,
              "seat rows run into the button");
static_assert(ACTION_RECT.y + ACTION_RECT.h + Ui::BUTTON_SHADOW_DY <= GAME_CANVAS_HEIGHT,
              "the button's shadow falls off the canvas");

const char* placeName(uint8_t place) {
    static const char* const NAMES[5] = {"", "1st", "2nd", "3rd", "4th"};
    return place <= 4 ? NAMES[place] : "";
}

}   // namespace

Rect LudoGame::dieRect() {
    static_assert(PANEL_X == BOARD_X + BOARD_PX + 8, "the panel must start beside the board");
    static_assert(BOARD_Y + BOARD_PX <= GAME_CANVAS_HEIGHT, "the board falls off the canvas");
    return DIE_RECT;
}

Rect LudoGame::actionRect() { return ACTION_RECT; }

void LudoGame::tellStops() const {
    if (mode_ != Mode::Play) {
        Ui::clearBoardStops();   // the lobbies are buttons, always pressable
        return;
    }
    /* A press on the board or the die does something only when a person at
     * this console has to roll or to pick a token -- not on another console's
     * turn or a computer's, during a hop or a notice, once the game is over,
     * or while the table is paused for a quiet console. */
    const bool live = !state_.over && !ended_ &&
                      (phase_ == Phase::Roll || phase_ == Phase::Choose) &&
                      ownsSeat(state_.turn) && !isComputer(state_.turn) &&
                      !(net_ && watch_.paused());
    /* The board whole, bottom row included, and the die as a rectangle of its
     * own beside it: End game sits under the die and is in neither, so it
     * goes on taking a press whatever the board is doing. */
    Ui::setBoardStops(boardRect(), live, dieRect());
}

// ---- the panel ---------------------------------------------------------------

void LudoGame::drawTurn(Ui::Renderer& tft) const {
    tft.fillRect(TURN_RECT.x, TURN_RECT.y, TURN_RECT.w, TURN_RECT.h, Ui::bg());
    if (phase_ == Phase::Over) {
        Ui::drawLabel(tft, TURN_RECT, "Game over", Ui::text(), 2, Align::Center);
        return;
    }
    const uint8_t seat = state_.turn;
    drawToken(tft, TURN_RECT.x + 7, TURN_RECT.y + TURN_RECT.h / 2, seat, 6, 1);
    char line[16];
    /* At a table the colour is already the token beside it; what a player
     * needs is WHO -- "You", a console's name, or the computer. */
    const char* label = seatLabel(seat);
    if (label != nullptr) {
        snprintf(line, sizeof(line), "%s", label);
    } else {
        snprintf(line, sizeof(line), "%s%s", seatName(seat), isComputer(seat) ? " (CPU)" : "");
    }
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(line, TURN_RECT.x + 17, TURN_RECT.y + 1, 2);
}

void LudoGame::drawDie(Ui::Renderer& tft, bool frameOn) const {
    const Rect r = DIE_RECT;
    /* The frame is the colour of whoever the die belongs to right now, so a
     * glance at it answers "whose go is it?" without reading anything. */
    const uint16_t frame = phase_ == Phase::Over ? Ui::outline()
                           : frameOn            ? seatColour(state_.turn)
                                                : Ui::bg();
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, frame);
    /* An outline in the frame's own colour, so nothing changes on the panel:
     * a fill and an outline of one rectangle is what the d-pad's selector
     * stops at, and A on the die rolls it. */
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, frame);
    tft.fillRoundRect(r.x + 4, r.y + 4, r.w - 8, r.h - 8, 6, dieFace());
    if (face_ < 1 || face_ > 6) {
        return;   // blank until rolled
    }
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);
    const int16_t cy = static_cast<int16_t>(r.y + r.h / 2);
    constexpr int16_t D = 10;
    constexpr int16_t PIP = 4;
    const bool centre = face_ & 1;
    const bool corners = face_ >= 2;
    const bool others = face_ >= 4;
    const bool sides = face_ == 6;
    if (centre) tft.fillCircle(cx, cy, PIP, inkColour());
    if (corners) {
        tft.fillCircle(cx - D, cy - D, PIP, inkColour());
        tft.fillCircle(cx + D, cy + D, PIP, inkColour());
    }
    if (others) {
        tft.fillCircle(cx + D, cy - D, PIP, inkColour());
        tft.fillCircle(cx - D, cy + D, PIP, inkColour());
    }
    if (sides) {
        tft.fillCircle(cx - D, cy, PIP, inkColour());
        tft.fillCircle(cx + D, cy, PIP, inkColour());
    }
}

void LudoGame::drawMessage(Ui::Renderer& tft) const {
    tft.fillRect(MESSAGE_RECT.x, MESSAGE_RECT.y, MESSAGE_RECT.w, MESSAGE_RECT.h, Ui::bg());
    if (message_[0] == 0) {
        return;
    }
    if (messageSeat_ >= Ludo::SEATS) {
        Ui::drawLabel(tft, MESSAGE_RECT, message_, Ui::text(), 2, Align::Center);
        return;
    }
    /* The words, a gap, then the seat's token -- centred as one line. */
    constexpr int16_t GAP = 4;
    const int16_t textW = tft.textWidth(message_, 2);
    const int16_t tokenW = 2 * TOKEN_R + 1;
    const int16_t x0 = static_cast<int16_t>(MESSAGE_RECT.x + (MESSAGE_RECT.w - textW - GAP - tokenW) / 2);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(message_, x0, MESSAGE_RECT.y + 1, 2);
    drawToken(tft, static_cast<int16_t>(x0 + textW + GAP + TOKEN_R),
              static_cast<int16_t>(MESSAGE_RECT.y + MESSAGE_RECT.h / 2), messageSeat_, TOKEN_R, 1);
}

/* Each seat's row: the turn dot, the token, the name, and a place or "CPU" on
 * the right. The dot sits in its own column so it can be cleared without
 * touching anything else in the row. */
namespace {
constexpr int16_t DOT_DX = 4;
constexpr int16_t DOT_R = 3;
constexpr int16_t ROW_TOKEN_DX = 14;
constexpr int16_t ROW_NAME_DX = 23;
}   // namespace

void LudoGame::drawTurnDot(Ui::Renderer& tft, uint8_t seat, bool on) const {
    /* Clearing the dot must never nick the token beside it. */
    static_assert(DOT_DX + DOT_R < ROW_TOKEN_DX - TOKEN_R, "the turn dot overlaps the token");
    static_assert(ROW_TOKEN_DX + TOKEN_R < ROW_NAME_DX, "the token overlaps the name");
    int16_t y = SEATS_RECT.y;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (!Ludo::playing(state_, s)) {
            continue;
        }
        if (s == seat) {
            tft.fillCircle(SEATS_RECT.x + DOT_DX, y + SEAT_ROW_H / 2, DOT_R,
                           on ? Ui::text() : Ui::bg());
            return;
        }
        y = static_cast<int16_t>(y + SEAT_ROW_H);
    }
}

void LudoGame::drawSeats(Ui::Renderer& tft) const {
    tft.fillRect(SEATS_RECT.x, SEATS_RECT.y, SEATS_RECT.w, SEATS_RECT.h, Ui::bg());
    int16_t y = SEATS_RECT.y;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (!Ludo::playing(state_, s)) {
            continue;
        }
        drawToken(tft, SEATS_RECT.x + ROW_TOKEN_DX, y + SEAT_ROW_H / 2, s, TOKEN_R, 1);
        tft.setTextColor(Ui::text(), Ui::bg());
        const char* label = seatLabel(s);
        tft.drawString(label != nullptr ? label : seatName(s), SEATS_RECT.x + ROW_NAME_DX, y + 1, 2);
        const char* right = state_.place[s] != 0 ? placeName(state_.place[s])
                            : (label == nullptr && isComputer(s)) ? "CPU" : "";
        if (right[0] != 0) {
            tft.setTextDatum(TR_DATUM);
            tft.setTextColor(state_.place[s] != 0 ? Ui::text() : Ui::muted(), Ui::bg());
            tft.drawString(right, SEATS_RECT.x + SEATS_RECT.w - 2, y + 1, 2);
            tft.setTextDatum(TL_DATUM);
        }
        y = static_cast<int16_t>(y + SEAT_ROW_H);
    }
}

void LudoGame::drawAction(Ui::Renderer& tft, bool sure) const {
    const Rect r = ACTION_RECT;
    tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
    if (phase_ == Phase::Over) {
        Ui::drawButton(tft, r, "New game", Ui::success(), Ui::outline(), TFT_BLACK);
    } else if (sure) {
        Ui::drawButton(tft, r, "Sure?", Ui::warning(), Ui::outline(), TFT_BLACK);
    } else {
        Ui::drawButton(tft, r, "End game", Ui::panel(), Ui::outline(), Ui::text());
    }
}

// ---- the lobby ---------------------------------------------------------------
//
// Four chips, one per colour, each cycling Empty -> Player -> Computer on a
// tap; the level toggle; a hint that says why Start is greyed when it is; and
// Start. Everything here is redrawn whole on any change -- the lobby is a
// handful of fixed rectangles, each painted by an idempotent function, so
// there is nothing to be gained by tracking which one changed.

namespace {

/* Chips sit in the same corners as the yards they stand for. */
constexpr int16_t CHIP_W = 148;
constexpr int16_t CHIP_H = 44;
/* drawLabel() puts left-aligned text at the rect's top, so this is the 16px
 * line centred on the level buttons' 30px row rather than the row itself. */
constexpr Rect LEVEL_LABEL{8, 145, 100, 16};
constexpr Rect HINT_RECT{8, 176, 304, 18};
/* Start plays on this console; Nearby opens the table of consoles. Side by
 * side and the same size, because they are the two ways to begin. */
constexpr Rect START_RECT{60, 198, 120, 34};
constexpr Rect NEARBY_RECT{192, 198, 120, 34};
static_assert(START_RECT.y + START_RECT.h + Ui::BUTTON_SHADOW_DY <= GAME_CANVAS_HEIGHT,
              "Start's shadow falls off the canvas");
static_assert(START_RECT.x + START_RECT.w + Ui::BUTTON_SHADOW_DX < NEARBY_RECT.x,
              "Start and Nearby overlap");

/* How often the lobby looks at the room for an invitation or a table. */
constexpr uint32_t ROOM_REFRESH_MS = 1000;

}   // namespace

Rect LudoGame::seatChipRect(uint8_t seat) {
    /* Laid out like the yards: Red top left, Green top right, Yellow bottom
     * right, Blue bottom left -- so a young player finds a colour where the board
     * keeps it. */
    const int16_t col = (seat == 1 || seat == 2) ? 1 : 0;
    const int16_t row = seat >= 2 ? 1 : 0;
    return Rect{static_cast<int16_t>(8 + col * (CHIP_W + 8)),
                static_cast<int16_t>(38 + row * (CHIP_H + 6)), CHIP_W, CHIP_H};
}

Rect LudoGame::levelRect(uint8_t level) {
    return Rect{static_cast<int16_t>(112 + level * 100), 138, 94, 30};
}

Rect LudoGame::startRect() { return START_RECT; }
Rect LudoGame::nearbyRect() { return NEARBY_RECT; }

void LudoGame::drawSeatChip(Ui::Renderer& tft, uint8_t seat) const {
    const Rect r = seatChipRect(seat);
    const bool empty = kind_[seat] == SeatKind::Empty;
    const uint16_t fill = empty ? Ui::surface() : seatColour(seat);
    const uint16_t text = empty ? Ui::muted() : seatText(seat);
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    const int16_t tx = static_cast<int16_t>(r.x + 20);
    const int16_t ty = static_cast<int16_t>(r.y + r.h / 2);
    tft.fillCircle(tx, ty, 12, paperColour());
    drawToken(tft, tx, ty, seat, 8, 1);
    static const char* const KINDS[3] = {"Empty", "Player", "Computer"};
    tft.setTextColor(text);
    tft.drawString(seatName(seat), r.x + 40, r.y + 5, 2);
    tft.drawString(KINDS[static_cast<uint8_t>(kind_[seat])], r.x + 40, r.y + 23, 2);
}

void LudoGame::drawLevel(Ui::Renderer& tft) const {
    static const char* const LEVELS[2] = {"Easy", "Normal"};
    Ui::drawLabel(tft, LEVEL_LABEL, "Computer", Ui::text(), 2, Align::Left);
    for (uint8_t lv = 0; lv < 2; ++lv) {
        const Rect r = levelRect(lv);
        const bool on = static_cast<uint8_t>(level_) == lv;
        tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
        /* The chosen one inverted, which has contrast in every theme without
         * borrowing a colour that means something else. */
        Ui::drawButton(tft, r, LEVELS[lv], on ? Ui::text() : Ui::surface(), Ui::outline(),
                       on ? Ui::bg() : Ui::text());
    }
}

void LudoGame::drawLobbyHint(Ui::Renderer& tft) const {
    tft.fillRect(HINT_RECT.x, HINT_RECT.y, HINT_RECT.w, HINT_RECT.h, Ui::bg());
    if (lobbyNote_[0] != 0) {
        /* Why the game just vanished. Outranks everything until tapped. */
        Ui::drawLabel(tft, HINT_RECT, lobbyNote_, Ui::warning(), 2, Align::Center);
        return;
    }
    if (inviteWaiting_) {
        /* The header banner announced it and has gone; this is where it is
         * still answerable from. */
        Ui::drawLabel(tft, HINT_RECT, "Invitation waiting: tap Nearby", Ui::success(), 2,
                      Align::Center);
        return;
    }
    if (tableOpen_) {
        /* Nobody asked this console, but a table being gathered can be sat
         * at all the same, and nothing else on this screen would say so. */
        Ui::drawLabel(tft, HINT_RECT, "A table is open: tap Nearby", Ui::success(), 2,
                      Align::Center);
        return;
    }
    uint8_t seats = 0;
    bool player = false;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        seats = static_cast<uint8_t>(seats + (kind_[s] != SeatKind::Empty ? 1 : 0));
        player = player || kind_[s] == SeatKind::Player;
    }
    const char* hint = seats < 2 ? "Two seats are needed to play"
                       : !player ? "Someone has to be a Player"
                                 : "Tap a seat to change who sits there";
    Ui::drawLabel(tft, HINT_RECT, hint, canStart() ? Ui::muted() : Ui::warning(), 2,
                  Align::Center);
}

void LudoGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        drawSeatChip(tft, s);
    }
    drawLevel(tft);
    drawLobbyHint(tft);
    for (const Rect& r : {START_RECT, NEARBY_RECT}) {
        tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
    }
    Ui::drawPagerButton(tft, START_RECT, "Start", canStart());
    Ui::drawButton(tft, NEARBY_RECT, "Nearby", inviteWaiting_ ? Ui::success() : Ui::panel(),
                   Ui::outline(), inviteWaiting_ ? static_cast<uint16_t>(TFT_BLACK) : Ui::text());
}

void LudoGame::updateLobby(AppContext& host, const TouchPoint& touch) {
    /* An invitation to Ludo is announced in the header by the service; the
     * lobby says where to answer it, and says so too when somebody in the
     * room is gathering a table. Re-read on the peer cadence, not every
     * frame. */
    const uint32_t now = millis();
    if (now - peersAtMs_ >= ROOM_REFRESH_MS) {
        peersAtMs_ = now;
        NearbySeat seat;
        const bool waiting = host.nearbyInviteForUs(seat) && seat.forThisGame;
        bool open = false;
        const uint8_t n = host.nearbySeatCount();
        for (uint8_t i = 0; i < n && !open; ++i) {
            open = host.nearbySeatAt(i, seat) && seat.asking;
        }
        if (waiting != inviteWaiting_ || open != tableOpen_) {
            inviteWaiting_ = waiting;
            tableOpen_ = open;
            lobbyStale_ = true;
            markDirty();
        }
    }
    if (!touch.justPressed) {
        return;
    }
    if (lobbyNote_[0] != 0) {
        /* "A4F2 ended the game" has been seen; any tap retires it. */
        lobbyNote_[0] = 0;
        lobbyStale_ = true;
        markDirty();
    }
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (seatChipRect(s).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            /* Empty -> Player -> Computer -> Empty: one tap per step, and the
             * chip says what it is now rather than what a tap will do. */
            kind_[s] = static_cast<SeatKind>((static_cast<uint8_t>(kind_[s]) + 1) % 3);
            lobbyStale_ = true;
            host.playSound(Sound::Tap);
            saveGame(host);
            markDirty();
            return;
        }
    }
    for (uint8_t lv = 0; lv < 2; ++lv) {
        if (levelRect(lv).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            level_ = static_cast<Ludo::Level>(lv);
            lobbyStale_ = true;
            host.playSound(Sound::Tap);
            saveGame(host);
            markDirty();
            return;
        }
    }
    if (nearbyRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.playSound(Sound::Select);
        openTable(host);
        return;
    }
    if (startRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (canStart()) {
            startGame(host);
        } else {
            host.beepError();
        }
    }
}
