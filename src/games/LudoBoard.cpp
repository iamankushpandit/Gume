#include "LudoGame.h"

/* Board geometry and every pixel of Ludo's drawing.
 *
 * Nothing on the board is repainted wholesale during play. A move changes two
 * places -- where a token was and where it is -- plus the yard spot of
 * anything it captured, so the renderer keeps one dirty bit per place (225
 * grid cells, 16 yard spots, the centre) and repaints only those. Every place
 * is drawn by an idempotent function that paints its whole box, which is what
 * lets one be repainted without asking what overlapped it. A full repaint
 * happens on entering the screen and on starting a game: the only times the
 * whole picture is new. */

namespace {

/* Board art. Fixed colours, like Chess's squares: a Ludo board is red, green,
 * yellow and blue in every theme, and the ground around it follows the theme. */
uint16_t red()      { return Ui::rgb(220, 48, 48); }
uint16_t green()    { return Ui::rgb(36, 160, 72); }
uint16_t yellow()   { return Ui::rgb(240, 196, 24); }
uint16_t blue()     { return Ui::rgb(32, 112, 216); }
uint16_t paper()    { return Ui::rgb(250, 250, 244); }   // track squares
uint16_t rule()     { return Ui::rgb(120, 124, 132); }   // grid lines
uint16_t ink()      { return Ui::rgb(26, 34, 48); }
uint16_t spot()     { return Ui::rgb(226, 229, 234); }   // an empty yard spot
uint16_t starInk()  { return Ui::rgb(150, 154, 162); }
uint16_t dieFace()  { return Ui::rgb(252, 252, 250); }
/* Behind a token that can move. Cyan because it is none of the four seat
 * colours, so it reads as "you can tap this" on every square it lands on. */
uint16_t hilite()   { return Ui::rgb(120, 230, 255); }

/* The track's first quarter, from Red's start square clockwise to the square
 * before Green's. The other three quarters are this, rotated. */
constexpr int8_t QUARTER[13][2] = {
    {1, 6}, {2, 6}, {3, 6}, {4, 6}, {5, 6},
    {6, 5}, {6, 4}, {6, 3}, {6, 2}, {6, 1}, {6, 0},
    {7, 0}, {8, 0},
};

/* Yards, in cell units, for seats 0..3: clockwise from the top left, which is
 * the order the track reaches their start squares. */
constexpr int8_t YARD_ORIGIN[4][2] = {{0, 0}, {9, 0}, {9, 9}, {0, 9}};

constexpr int16_t YARD_CELLS = 6;
constexpr int16_t YARD_INSET = 9;
constexpr int16_t SPOT_NEAR = 24;
constexpr int16_t SPOT_FAR = 54;
constexpr int16_t SPOT_R = 8;
constexpr int16_t YARD_TOKEN_R = 6;
constexpr int16_t TOKEN_R = 5;
constexpr int16_t MINI_R = 3;

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

/* A quarter turn clockwise about the centre of the 15x15 grid, `times` over. */
void rotate(int8_t& col, int8_t& row, uint8_t times) {
    for (uint8_t i = 0; i < times; ++i) {
        const int8_t c = col;
        col = static_cast<int8_t>(14 - row);
        row = c;
    }
}

void centreText(Ui::Renderer& tft, const char* text, int16_t x, int16_t y,
                uint16_t colour, uint8_t font) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(colour);
    tft.drawString(text, x, y, font);
    tft.setTextDatum(TL_DATUM);
}

const char* placeName(uint8_t place) {
    static const char* const NAMES[5] = {"", "1st", "2nd", "3rd", "4th"};
    return place <= 4 ? NAMES[place] : "";
}

}   // namespace

const char* LudoGame::seatName(uint8_t seat) {
    static const char* const NAMES[Ludo::SEATS] = {"Red", "Green", "Yellow", "Blue"};
    return seat < Ludo::SEATS ? NAMES[seat] : "";
}

uint16_t LudoGame::seatColour(uint8_t seat) {
    switch (seat) {
        case 0: return red();
        case 1: return green();
        case 2: return yellow();
        default: return blue();
    }
}

uint16_t LudoGame::seatText(uint8_t seat) {
    /* Text on a seat's colour: white everywhere but yellow. */
    return seat == 2 ? ink() : static_cast<uint16_t>(TFT_WHITE);
}

uint16_t LudoGame::paperColour() {
    return paper();
}

// ---- geometry ------------------------------------------------------------

LudoGame::Cell LudoGame::trackCell(uint8_t abs) {
    const uint8_t q = static_cast<uint8_t>(abs / Ludo::SEAT_OFFSET);
    const uint8_t i = static_cast<uint8_t>(abs % Ludo::SEAT_OFFSET);
    Cell c{QUARTER[i][0], QUARTER[i][1]};
    rotate(c.col, c.row, q);
    return c;
}

LudoGame::Cell LudoGame::homeCell(uint8_t seat, uint8_t step) {
    /* Red's column runs along the middle row of the left arm towards the
     * centre; the others are it, rotated onto their own arm. */
    Cell c{static_cast<int8_t>(1 + step), 7};
    rotate(c.col, c.row, seat);
    return c;
}

Rect LudoGame::cellRect(Cell c) {
    return Rect{static_cast<int16_t>(BOARD_X + c.col * CELL),
                static_cast<int16_t>(BOARD_Y + c.row * CELL), CELL, CELL};
}

Rect LudoGame::yardRect(uint8_t seat) {
    return Rect{static_cast<int16_t>(BOARD_X + YARD_ORIGIN[seat][0] * CELL),
                static_cast<int16_t>(BOARD_Y + YARD_ORIGIN[seat][1] * CELL),
                YARD_CELLS * CELL, YARD_CELLS * CELL};
}

void LudoGame::yardSpot(uint8_t seat, uint8_t token, int16_t& x, int16_t& y) {
    static_assert(SPOT_FAR + SPOT_R < YARD_CELLS * CELL - YARD_INSET + 1,
                  "a yard spot spills out of the yard's inner square");
    const Rect r = yardRect(seat);
    x = static_cast<int16_t>(r.x + ((token & 1) ? SPOT_FAR : SPOT_NEAR));
    y = static_cast<int16_t>(r.y + ((token & 2) ? SPOT_FAR : SPOT_NEAR));
}

void LudoGame::homeSpot(uint8_t seat, int16_t& x, int16_t& y) {
    /* The centre is the 3x3 block at cells 6..8. Each triangle's count sits
     * six pixels in from its outer edge, which is its widest part. */
    const int16_t x0 = static_cast<int16_t>(BOARD_X + 6 * CELL);
    const int16_t y0 = static_cast<int16_t>(BOARD_Y + 6 * CELL);
    const int16_t span = static_cast<int16_t>(3 * CELL - 1);
    const int16_t mid = static_cast<int16_t>(span / 2);
    switch (seat) {
        case 0: x = x0 + 7;        y = y0 + mid;      break;   // left
        case 1: x = x0 + mid;      y = y0 + 7;        break;   // top
        case 2: x = x0 + span - 7; y = y0 + mid;      break;   // right
        default: x = x0 + mid;     y = y0 + span - 7; break;   // bottom
    }
}

uint8_t LudoGame::placeOf(uint8_t seat, uint8_t token, uint8_t rel) {
    if (rel == Ludo::YARD) {
        return static_cast<uint8_t>(GRID * GRID + seat * Ludo::TOKENS + token);
    }
    if (rel >= Ludo::HOME) {
        return PLACE_CENTRE;
    }
    const Cell c = Ludo::onTrack(rel)
                       ? trackCell(Ludo::absolute(seat, rel))
                       : homeCell(seat, static_cast<uint8_t>(rel - Ludo::HOME_COLUMN));
    return static_cast<uint8_t>(c.row * GRID + c.col);
}

void LudoGame::tokenCentre(uint8_t seat, uint8_t token, int16_t& x, int16_t& y) const {
    const uint8_t rel = shown_[seat][token];
    if (rel == Ludo::YARD) {
        yardSpot(seat, token, x, y);
        return;
    }
    if (rel >= Ludo::HOME) {
        homeSpot(seat, x, y);
        return;
    }
    const uint8_t place = placeOf(seat, token, rel);
    const Rect r = cellRect(Cell{static_cast<int8_t>(place % GRID),
                                 static_cast<int8_t>(place / GRID)});
    x = static_cast<int16_t>(r.x + CELL / 2 + 1);
    y = static_cast<int16_t>(r.y + CELL / 2 + 1);
}

Rect LudoGame::dieRect() {
    static_assert(PANEL_X == BOARD_X + BOARD_PX + 8, "the panel must start beside the board");
    static_assert(BOARD_Y + BOARD_PX <= GAME_CANVAS_HEIGHT, "the board falls off the canvas");
    return DIE_RECT;
}

Rect LudoGame::actionRect() { return ACTION_RECT; }

// ---- tokens ----------------------------------------------------------------

void LudoGame::drawToken(Ui::Renderer& tft, int16_t cx, int16_t cy, uint8_t seat,
                         int16_t r, uint8_t count) {
    /* A shape per colour as well as the colour itself: circle, square,
     * diamond, triangle. Every shape stays inside a (2r+1) box, so erasing a
     * token is always erasing that box. */
    const uint16_t fill = seatColour(seat);
    switch (seat) {
        case 0:
            tft.fillCircle(cx, cy, r, fill);
            tft.drawCircle(cx, cy, r, ink());
            break;
        case 1:
            tft.fillRect(cx - r + 1, cy - r + 1, 2 * r - 1, 2 * r - 1, fill);
            tft.drawRect(cx - r + 1, cy - r + 1, 2 * r - 1, 2 * r - 1, ink());
            break;
        case 2:
            tft.fillTriangle(cx, cy - r, cx - r, cy, cx + r, cy, fill);
            tft.fillTriangle(cx - r, cy, cx + r, cy, cx, cy + r, fill);
            tft.drawLine(cx, cy - r, cx + r, cy, ink());
            tft.drawLine(cx + r, cy, cx, cy + r, ink());
            tft.drawLine(cx, cy + r, cx - r, cy, ink());
            tft.drawLine(cx - r, cy, cx, cy - r, ink());
            break;
        default:
            tft.fillTriangle(cx, cy - r, cx - r, cy + r, cx + r, cy + r, fill);
            tft.drawTriangle(cx, cy - r, cx - r, cy + r, cx + r, cy + r, ink());
            break;
    }
    if (count > 1) {
        char digit[2] = {static_cast<char>('0' + (count > 9 ? 9 : count)), 0};
        centreText(tft, digit, cx, cy + 1, seatText(seat), 1);
    }
}

// ---- places ------------------------------------------------------------------

void LudoGame::drawCellAt(Ui::Renderer& tft, Cell c) const {
    /* What this square is: a track square (and whose start, or a star), or a
     * square in someone's home column. 72 candidates, only asked when a cell
     * is actually being painted. */
    uint16_t base = paper();
    bool isStar = false;
    bool found = false;
    for (uint8_t abs = 0; abs < Ludo::TRACK_LEN && !found; ++abs) {
        const Cell t = trackCell(abs);
        if (t.col == c.col && t.row == c.row) {
            found = true;
            if (abs % Ludo::SEAT_OFFSET == 0) {
                base = seatColour(static_cast<uint8_t>(abs / Ludo::SEAT_OFFSET));
            } else {
                isStar = Ludo::safeSquare(abs);
            }
        }
    }
    for (uint8_t seat = 0; seat < Ludo::SEATS && !found; ++seat) {
        for (uint8_t step = 0; step < 5; ++step) {
            const Cell h = homeCell(seat, step);
            if (h.col == c.col && h.row == c.row) {
                base = seatColour(seat);
                found = true;
                break;
            }
        }
    }

    /* Who is standing here, counted per seat. */
    const uint8_t place = static_cast<uint8_t>(c.row * GRID + c.col);
    uint8_t count[Ludo::SEATS] = {0, 0, 0, 0};
    uint8_t groups = 0;
    bool lit = false;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
            const uint8_t rel = shown_[s][t];
            if (rel == Ludo::YARD || rel >= Ludo::HOME || placeOf(s, t, rel) != place) {
                continue;
            }
            if (count[s]++ == 0) {
                ++groups;
            }
            lit = lit || ((hi_ >> (s * Ludo::TOKENS + t)) & 1);
        }
    }

    const Rect r = cellRect(c);
    tft.fillRect(r.x, r.y, r.w, r.h, rule());
    tft.fillRect(r.x + 1, r.y + 1, r.w - 1, r.h - 1, lit ? hilite() : base);
    const int16_t cx = static_cast<int16_t>(r.x + CELL / 2 + 1);
    const int16_t cy = static_cast<int16_t>(r.y + CELL / 2 + 1);
    if (isStar && groups == 0) {
        Ui::drawStarShape(tft, cx, cy, 5, starInk(), true);
    }
    if (groups == 1) {
        for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
            if (count[s] != 0) {
                drawToken(tft, cx, cy, s, TOKEN_R, count[s]);
            }
        }
    } else if (groups > 1) {
        /* Colours sharing a safe square: one small token per colour, in the
         * corner of the square that colour's yard is in. */
        static constexpr int8_t CORNER[4][2] = {{-3, -3}, {3, -3}, {3, 3}, {-3, 3}};
        for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
            if (count[s] != 0) {
                drawToken(tft, cx + CORNER[s][0], cy + CORNER[s][1], s, MINI_R, 1);
            }
        }
    }
}

void LudoGame::drawYardSpot(Ui::Renderer& tft, uint8_t seat, uint8_t token) const {
    int16_t x = 0;
    int16_t y = 0;
    yardSpot(seat, token, x, y);
    const bool here = shown_[seat][token] == Ludo::YARD;
    const bool lit = here && ((hi_ >> (seat * Ludo::TOKENS + token)) & 1);
    tft.fillCircle(x, y, SPOT_R, lit ? hilite() : spot());
    tft.drawCircle(x, y, SPOT_R, seatColour(seat));
    if (here) {
        drawToken(tft, x, y, seat, YARD_TOKEN_R, 1);
    }
}

void LudoGame::drawCentre(Ui::Renderer& tft) const {
    const int16_t x0 = static_cast<int16_t>(BOARD_X + 6 * CELL);
    const int16_t y0 = static_cast<int16_t>(BOARD_Y + 6 * CELL);
    const int16_t x1 = static_cast<int16_t>(x0 + 3 * CELL - 1);
    const int16_t y1 = static_cast<int16_t>(y0 + 3 * CELL - 1);
    const int16_t cx = static_cast<int16_t>((x0 + x1) / 2);
    const int16_t cy = static_cast<int16_t>((y0 + y1) / 2);
    /* Each colour's triangle faces the home column that leads into it. */
    tft.fillTriangle(x0, y0, x0, y1, cx, cy, seatColour(0));
    tft.fillTriangle(x0, y0, x1, y0, cx, cy, seatColour(1));
    tft.fillTriangle(x1, y0, x1, y1, cx, cy, seatColour(2));
    tft.fillTriangle(x0, y1, x1, y1, cx, cy, seatColour(3));
    tft.drawLine(x0, y0, x1, y1, ink());
    tft.drawLine(x1, y0, x0, y1, ink());
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        uint8_t home = 0;
        for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
            home = static_cast<uint8_t>(home + (shown_[s][t] == Ludo::HOME ? 1 : 0));
        }
        if (home == 0) {
            continue;
        }
        int16_t x = 0;
        int16_t y = 0;
        homeSpot(s, x, y);
        char digit[2] = {static_cast<char>('0' + home), 0};
        centreText(tft, digit, x, y + 1, seatText(s), 1);
    }
}

void LudoGame::drawPlace(Ui::Renderer& tft, uint8_t place) const {
    if (place == PLACE_CENTRE) {
        drawCentre(tft);
        return;
    }
    if (place >= GRID * GRID) {
        const uint8_t i = static_cast<uint8_t>(place - GRID * GRID);
        drawYardSpot(tft, static_cast<uint8_t>(i / Ludo::TOKENS),
                     static_cast<uint8_t>(i % Ludo::TOKENS));
        return;
    }
    drawCellAt(tft, Cell{static_cast<int8_t>(place % GRID),
                         static_cast<int8_t>(place / GRID)});
}

void LudoGame::drawBoard(Ui::Renderer& tft) const {
    tft.drawRect(BOARD_X - 1, BOARD_Y - 1, BOARD_PX + 2, BOARD_PX + 2, ink());
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        const Rect y = yardRect(s);
        tft.fillRect(y.x, y.y, y.w, y.h, seatColour(s));
        tft.fillRoundRect(y.x + YARD_INSET, y.y + YARD_INSET, y.w - 2 * YARD_INSET,
                          y.h - 2 * YARD_INSET, 6, paper());
        for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
            drawYardSpot(tft, s, t);
        }
    }
    for (uint8_t abs = 0; abs < Ludo::TRACK_LEN; ++abs) {
        drawCellAt(tft, trackCell(abs));
    }
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        for (uint8_t step = 0; step < 5; ++step) {
            drawCellAt(tft, homeCell(s, step));
        }
    }
    drawCentre(tft);
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
    snprintf(line, sizeof(line), "%s%s", seatName(seat), isComputer(seat) ? " (CPU)" : "");
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(line, TURN_RECT.x + 17, TURN_RECT.y + 1, 2);
}

void LudoGame::drawDie(Ui::Renderer& tft) const {
    const Rect r = DIE_RECT;
    /* The frame is the colour of whoever the die belongs to right now, so a
     * glance at it answers "whose go is it?" without reading anything. */
    const uint16_t frame = phase_ == Phase::Over ? Ui::outline() : seatColour(state_.turn);
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
    if (centre) tft.fillCircle(cx, cy, PIP, ink());
    if (corners) {
        tft.fillCircle(cx - D, cy - D, PIP, ink());
        tft.fillCircle(cx + D, cy + D, PIP, ink());
    }
    if (others) {
        tft.fillCircle(cx + D, cy - D, PIP, ink());
        tft.fillCircle(cx - D, cy + D, PIP, ink());
    }
    if (sides) {
        tft.fillCircle(cx - D, cy, PIP, ink());
        tft.fillCircle(cx + D, cy, PIP, ink());
    }
}

void LudoGame::drawMessage(Ui::Renderer& tft) const {
    tft.fillRect(MESSAGE_RECT.x, MESSAGE_RECT.y, MESSAGE_RECT.w, MESSAGE_RECT.h, Ui::bg());
    if (message_[0] != 0) {
        Ui::drawLabel(tft, MESSAGE_RECT, message_, Ui::text(), 2, Align::Center);
    }
}

void LudoGame::drawSeats(Ui::Renderer& tft) const {
    tft.fillRect(SEATS_RECT.x, SEATS_RECT.y, SEATS_RECT.w, SEATS_RECT.h, Ui::bg());
    int16_t y = SEATS_RECT.y;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (!Ludo::playing(state_, s)) {
            continue;
        }
        drawToken(tft, SEATS_RECT.x + 7, y + SEAT_ROW_H / 2, s, TOKEN_R, 1);
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(seatName(s), SEATS_RECT.x + 17, y + 1, 2);
        const char* right = state_.place[s] != 0 ? placeName(state_.place[s])
                                                 : (isComputer(s) ? "CPU" : "");
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

// ---- the render passes ------------------------------------------------------------

uint16_t LudoGame::highlights() const {
    if (mode_ != Mode::Play || phase_ != Phase::Choose || isComputer(state_.turn)) {
        return 0;
    }
    return static_cast<uint16_t>(Ludo::movable(state_) << (state_.turn * Ludo::TOKENS));
}

void LudoGame::markPlace(uint8_t place) {
    if (place < PLACE_COUNT) {
        dirty_[place >> 3] = static_cast<uint8_t>(dirty_[place >> 3] | (1U << (place & 7)));
        anyDirty_ = true;
    }
}

void LudoGame::diffPlaces() {
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
            const uint8_t now = placeOf(s, t, shown_[s][t]);
            const uint16_t bit = static_cast<uint16_t>(1U << (s * Ludo::TOKENS + t));
            if (now != drawnPlace_[s][t]) {
                markPlace(now);
                markPlace(drawnPlace_[s][t]);
                drawnPlace_[s][t] = now;
            } else if ((hi_ & bit) != (drawnHi_ & bit)) {
                markPlace(now);
            }
        }
    }
    drawnHi_ = hi_;
}

void LudoGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    if (mode_ == Mode::Lobby) {
        lobbyStale_ = true;
        return;
    }
    hi_ = highlights();
    drawBoard(tft);
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
            drawnPlace_[s][t] = placeOf(s, t, shown_[s][t]);
        }
    }
    drawnHi_ = hi_;
    for (uint8_t& b : dirty_) {
        b = 0;
    }
    anyDirty_ = false;
    drawnFace_ = 0xFF;
    drawnTurn_ = 0xFF;
    drawnMessage_[0] = 1;   // matches no message, so the first pass draws it
    drawnMessage_[1] = 0;
    seatsStale_ = true;
    actionStale_ = true;
}

void LudoGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    if (mode_ == Mode::Lobby) {
        if (lobbyStale_) {
            renderLobby(host);
            lobbyStale_ = false;
        }
        return;
    }

    hi_ = highlights();
    diffPlaces();
    if (anyDirty_) {
        for (uint8_t p = 0; p < PLACE_COUNT; ++p) {
            if (dirty_[p >> 3] & (1U << (p & 7))) {
                drawPlace(tft, p);
            }
        }
        for (uint8_t& b : dirty_) {
            b = 0;
        }
        anyDirty_ = false;
    }

    const uint8_t turn = phase_ == Phase::Over ? 0xFE : state_.turn;
    if (face_ != drawnFace_ || turn != drawnTurn_) {
        drawDie(tft);
        drawnFace_ = face_;
    }
    if (turn != drawnTurn_) {
        drawTurn(tft);
        drawnTurn_ = turn;
    }
    if (strcmp(message_, drawnMessage_) != 0) {
        drawMessage(tft);
        snprintf(drawnMessage_, sizeof(drawnMessage_), "%s", message_);
    }
    if (seatsStale_) {
        drawSeats(tft);
        seatsStale_ = false;
    }
    const bool sure = phase_ != Phase::Over && millis() < confirmUntilMs_;
    if (actionStale_ || sure != confirmShown_) {
        drawAction(tft, sure);
        confirmShown_ = sure;
        actionStale_ = false;
    }
}
