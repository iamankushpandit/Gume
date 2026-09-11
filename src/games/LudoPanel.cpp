#include "LudoGame.h"

/* The panel beside the board: whose turn it is, the die, one line of news,
 * the seats and their places, and the one button. Split from LudoBoard.cpp for
 * size. Each part is repainted on its own when what it shows changes -- see
 * LudoGame::renderDynamic(). */

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
