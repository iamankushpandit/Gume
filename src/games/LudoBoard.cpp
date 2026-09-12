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
constexpr int16_t MINI_R = 3;

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

uint16_t LudoGame::inkColour() {
    return ink();
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

// ---- the render passes ------------------------------------------------------------

uint16_t LudoGame::highlights() const {
    if (mode_ != Mode::Play || phase_ != Phase::Choose || isComputer(state_.turn) ||
        !ownsSeat(state_.turn)) {
        return 0;   // and never another console's choice
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
    if (mode_ != Mode::Play) {
        lobbyStale_ = true;
        tableStale_ = true;
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
    drawnMessageSeat_ = 0xFE;
    drawnDotSeat_ = Ludo::NO_SEAT;   // drawSeats() below paints no dot
    seatsStale_ = true;
    actionStale_ = true;
    pausePainted_ = false;   // the board was just repainted under it
    drawPause(tft);
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
    if (mode_ == Mode::Table) {
        if (tableStale_) {
            renderTable(host);
            tableStale_ = false;
        }
        return;
    }

    hi_ = highlights();
    diffPlaces();
    if (anyDirty_) {
        pausePainted_ = false;   // a place under the card
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
    const bool blink = blinkPhase();
    /* The die's frame flashes by changing colour, never size, so nothing
     * around it ever needs erasing. */
    const bool frameOn = !dieFlashing() || blink;
    if (face_ != drawnFace_ || turn != drawnTurn_ || frameOn != drawnFrameOn_) {
        drawDie(tft, frameOn);
        drawnFace_ = face_;
        drawnFrameOn_ = frameOn;
    }
    if (turn != drawnTurn_) {
        drawTurn(tft);
        drawnTurn_ = turn;
    }
    if (strcmp(message_, drawnMessage_) != 0 || messageSeat_ != drawnMessageSeat_) {
        drawMessage(tft);
        snprintf(drawnMessage_, sizeof(drawnMessage_), "%s", message_);
        drawnMessageSeat_ = messageSeat_;
    }
    if (seatsStale_) {
        drawSeats(tft);
        seatsStale_ = false;
        drawnDotSeat_ = Ludo::NO_SEAT;   // the list was repainted without it
    }
    /* The turn dot: its own few pixels, moved when the turn moves and blinked
     * by colour in between. The seat list itself is not repainted for it. */
    const uint8_t dotSeat = phase_ == Phase::Over ? Ludo::NO_SEAT : state_.turn;
    if (dotSeat != drawnDotSeat_ || (dotSeat != Ludo::NO_SEAT && blink != drawnDotOn_)) {
        if (drawnDotSeat_ != Ludo::NO_SEAT && drawnDotSeat_ != dotSeat) {
            drawTurnDot(tft, drawnDotSeat_, false);
        }
        if (dotSeat != Ludo::NO_SEAT) {
            drawTurnDot(tft, dotSeat, blink);
        }
        drawnDotSeat_ = dotSeat;
        drawnDotOn_ = blink;
    }
    const bool sure = phase_ != Phase::Over && millis() < confirmUntilMs_;
    if (actionStale_ || sure != confirmShown_) {
        drawAction(tft, sure);
        confirmShown_ = sure;
        actionStale_ = false;
    }
    drawPause(tft);
}
