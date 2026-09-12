#include "SeaBattleGame.h"

#include <string.h>

/* Sea Battle geometry and drawing, against the fixed 320x240 canvas. */

namespace {

constexpr int16_t TOP_BAR_H = 30;
constexpr int16_t MARGIN = 3;
constexpr int16_t GAP = 3;
constexpr int16_t ACTION_H = 26;
constexpr int16_t STATUS_H = 24;
constexpr int16_t TALLY_H = 12;
constexpr int16_t LABEL_H = 11;

/* Sea, and the two things that can be on it. Fixed rather than themed, like
 * the chess board's squares: these are the colours of a thing being depicted,
 * not of the interface around it, and a hit has to read as red in all nine
 * themes. */
uint16_t seaColour() { return Ui::rgb(30, 62, 104); }
uint16_t seaGrid() { return Ui::rgb(58, 96, 142); }
uint16_t missColour() { return Ui::rgb(150, 168, 190); }
uint16_t hitColour() { return Ui::rgb(214, 66, 54); }
uint16_t shipColour() { return Ui::rgb(120, 128, 140); }

}   // namespace

// --------------------------------------------------------------- layout
//
// Every rect below is derived from the board so the panel cannot overlap it.
// The canvas is a fixed 320x240 in landscape -- see the class comment for why
// this game does not follow the owner's orientation.

Rect SeaBattleGame::boardRect() {
    const int16_t top = TOP_BAR_H + MARGIN;
    int16_t side = static_cast<int16_t>(GAME_CANVAS_HEIGHT - top - MARGIN);
    side = static_cast<int16_t>((side / GRID) * GRID);
    return Rect{MARGIN, top, side, side};
}

Rect SeaBattleGame::cellRect(uint8_t cell) {
    const Rect b = boardRect();
    const int16_t s = static_cast<int16_t>(b.w / GRID);
    return Rect{static_cast<int16_t>(b.x + colOf(cell) * s),
                static_cast<int16_t>(b.y + rowOf(cell) * s), s, s};
}

Rect SeaBattleGame::panelRect() {
    const Rect b = boardRect();
    const int16_t x = static_cast<int16_t>(b.x + b.w + GAP);
    return Rect{x, b.y, static_cast<int16_t>(GAME_CANVAS_WIDTH - x - MARGIN), b.h};
}

/* Our own sea, small, in the column beside the board. Square and centred: a
 * grid that is not square reads as a different game rather than as the same
 * one seen from further away. */
Rect SeaBattleGame::miniRect() {
    const Rect p = panelRect();
    int16_t s = p.w;
    s = static_cast<int16_t>((s / GRID) * GRID);
    return Rect{static_cast<int16_t>(p.x + (p.w - s) / 2),
                static_cast<int16_t>(p.y + LABEL_H), s, s};
}

Rect SeaBattleGame::miniCellRect(uint8_t cell) {
    const Rect m = miniRect();
    const int16_t s = static_cast<int16_t>(m.w / GRID);
    return Rect{static_cast<int16_t>(m.x + colOf(cell) * s),
                static_cast<int16_t>(m.y + rowOf(cell) * s), s, s};
}

Rect SeaBattleGame::statusRect() {
    const Rect p = panelRect();
    const Rect m = miniRect();
    return Rect{p.x, static_cast<int16_t>(m.y + m.h + GAP + 2), p.w, STATUS_H};
}

Rect SeaBattleGame::tallyRect() {
    const Rect s = statusRect();
    return Rect{s.x, static_cast<int16_t>(s.y + s.h), s.w, TALLY_H};
}

Rect SeaBattleGame::actionRect() {
    const Rect p = panelRect();
    return Rect{p.x, static_cast<int16_t>(p.y + p.h - ACTION_H), p.w, ACTION_H};
}

/* Ready sits above Shuffle while placing, and nothing is drawn there at any
 * other time -- so the two never need to share a row and neither has to shrink
 * to make room for a button that is usually absent. */
Rect SeaBattleGame::readyRect() {
    const Rect a = actionRect();
    return Rect{a.x, static_cast<int16_t>(a.y - ACTION_H - GAP), a.w, ACTION_H};
}

uint8_t SeaBattleGame::cellAt(int16_t x, int16_t y) {
    const Rect b = boardRect();
    if (!b.contains(x, y, 0)) return NO_CELL;
    const int16_t s = static_cast<int16_t>(b.w / GRID);
    const int16_t col = static_cast<int16_t>((x - b.x) / s);
    const int16_t row = static_cast<int16_t>((y - b.y) / s);
    if (col < 0 || col >= GRID || row < 0 || row >= GRID) return NO_CELL;
    return cellOf(static_cast<uint8_t>(col), static_cast<uint8_t>(row));
}


// -------------------------------------------------------------- the lobby

Rect SeaBattleGame::lobbyRowRect(uint8_t row) const {
    const int16_t top = TOP_BAR_H + 8;
    const int16_t h = 30;
    return Rect{10, static_cast<int16_t>(top + row * (h + 6)),
                static_cast<int16_t>(GAME_CANVAS_WIDTH - 20), h};
}

// ---------------------------------------------------------------- render

/* One cell of the enemy sea: what we have fired at and what we found.
 *
 * An unknown square is plain sea. That is the whole game -- the grid is a
 * record of guesses, not a map -- so there is deliberately nothing here that
 * reads the enemy fleet. Getting that wrong would not look like a bug, it
 * would look like a game that is no fun. */
void SeaBattleGame::drawCell(AppContext& host, uint8_t cell) const {
    Ui::Renderer& tft = host.display();
    const Rect r = cellRect(cell);
    const Side& us = side_[viewer()];

    if (phase_ == Phase::Placing) {
        // Our own sea, full size, while the fleet is being laid out.
        const bool ship = us.shipAt[cell] != NO_SHIP;
        tft.fillRect(r.x, r.y, r.w, r.h, ship ? shipColour() : seaColour());
        tft.drawRect(r.x, r.y, r.w, r.h, seaGrid());
        return;
    }

    tft.fillRect(r.x, r.y, r.w, r.h, seaColour());
    tft.drawRect(r.x, r.y, r.w, r.h, seaGrid());
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);
    const int16_t cy = static_cast<int16_t>(r.y + r.h / 2);
    switch (us.shot[cell]) {
        case Mark::Miss:
            tft.fillCircle(cx, cy, static_cast<int16_t>(r.w / 6), missColour());
            break;
        case Mark::Hit:
            tft.fillCircle(cx, cy, static_cast<int16_t>(r.w / 3), hitColour());
            break;
        case Mark::Pending:
            // Fired, answer not back yet: a ring, so it reads as "waiting".
            tft.drawCircle(cx, cy, static_cast<int16_t>(r.w / 3), Ui::warning());
            break;
        case Mark::Unknown:
            break;
    }
}

void SeaBattleGame::drawBoard(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    for (uint8_t c = 0; c < CELLS; ++c) drawCell(host, c);
    const Rect b = boardRect();
    tft.drawRect(b.x, b.y, b.w, b.h, Ui::outline());
}

/* Our own sea, small. Ships we still have, ships that have been hit, and the
 * water they have wasted a shot on. Always visible, because being shot at is
 * half of what is happening and a screen you have to switch to is a screen
 * nobody switches to. */
void SeaBattleGame::drawMini(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const Rect m = miniRect();
    const Side& us = side_[viewer()];

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString("Your sea", m.x, static_cast<int16_t>(m.y - LABEL_H), 1);

    for (uint8_t c = 0; c < CELLS; ++c) {
        const Rect r = miniCellRect(c);
        const bool ship = us.shipAt[c] != NO_SHIP;
        uint16_t fill = seaColour();
        if (ship && us.incoming[c]) {
            fill = hitColour();
        } else if (ship) {
            fill = shipColour();
        } else if (us.incoming[c]) {
            fill = missColour();
        }
        tft.fillRect(r.x, r.y, r.w, r.h, fill);
    }
    tft.drawRect(m.x, m.y, m.w, m.h, Ui::outline());
}

void SeaBattleGame::drawStatus(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const Rect r = statusRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());

    char top[24];
    char bot[24];
    top[0] = 0;
    bot[0] = 0;
    uint16_t colour = Ui::text();

    if (mode_ == Mode::Waiting) {
        snprintf(top, sizeof(top), "Asking %s", opponentLabel());
        snprintf(bot, sizeof(bot), "waiting...");
        colour = Ui::muted();
    } else if (mode_ == Mode::Remote && watch_.paused() && !gameOver()) {
        /* Still here after Keep waiting has taken the card away. */
        snprintf(top, sizeof(top), "%s", opponentLabel());
        snprintf(bot, sizeof(bot),
                 watch_.state() == NearbyWatch::State::Gone ? "out of range" : "gone quiet");
        colour = Ui::warning();
    } else if (won_) {
        snprintf(top, sizeof(top), "You win!");
        snprintf(bot, sizeof(bot), "fleet sunk");
        colour = Ui::success();
    } else if (lost_) {
        snprintf(top, sizeof(top), "Fleet lost");
        snprintf(bot, sizeof(bot), "%s wins", opponentLabel());
        colour = Ui::warning();
    } else if (ended_) {
        snprintf(top, sizeof(top), "Game ended");
        snprintf(bot, sizeof(bot),
                 mode_ == Mode::Remote
                     ? (endedByUs_ ? "you stopped it" : "they stopped it")
                     : "no winner");
        colour = Ui::muted();
    } else if (phase_ == Phase::Placing) {
        snprintf(top, sizeof(top), "Your fleet");
        snprintf(bot, sizeof(bot), "shuffle or ready");
    } else if (mode_ == Mode::Remote && pendingCell_ != NO_CELL) {
        snprintf(top, sizeof(top), "Fired");
        snprintf(bot, sizeof(bot), "waiting...");
        colour = Ui::muted();
    } else if (mode_ == Mode::Remote && !ourTurn()) {
        snprintf(top, sizeof(top), "%s", opponentLabel());
        snprintf(bot, sizeof(bot), "is aiming");
        colour = Ui::muted();
    } else {
        /* What just happened beats whose turn it is: a child who has just
         * scored a hit wants to be told so, and the turn is obvious from the
         * board being tappable. */
        switch (lastShot_) {
            case Shot::Sunk:
                snprintf(top, sizeof(top), "Sunk!");
                snprintf(bot, sizeof(bot), "fire again");
                colour = Ui::success();
                break;
            case Shot::Hit:
                snprintf(top, sizeof(top), "Hit!");
                snprintf(bot, sizeof(bot), "fire again");
                colour = Ui::success();
                break;
            case Shot::Miss:
                snprintf(top, sizeof(top), "Miss");
                snprintf(bot, sizeof(bot), "fire again");
                break;
            default:
                /* One branch, one call. It used to pick the format string with
                 * a ternary and pass no argument at all, then overwrite the
                 * result on the next line for the local case -- so every local
                 * status draw ran an snprintf whose "%u" had nothing to read,
                 * which is undefined behaviour that happened to be harmless
                 * because the answer was thrown away. `-Wformat=` had been
                 * saying so on every board. */
                if (mode_ == Mode::Remote) {
                    snprintf(top, sizeof(top), "Your shot");
                } else {
                    snprintf(top, sizeof(top), "Player %u",
                             static_cast<unsigned>(turnPlayer_ + 1));
                }
                snprintf(bot, sizeof(bot), "tap the sea");
                break;
        }
    }

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(colour, Ui::bg());
    tft.drawString(top, r.x, r.y, 2);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(bot, r.x, static_cast<int16_t>(r.y + 14), 1);
}

void SeaBattleGame::drawTally(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const Rect r = tallyRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    if (phase_ == Phase::Placing) return;

    char line[24];
    snprintf(line, sizeof(line), "Hits %u/%u  lost %u",
             static_cast<unsigned>(side_[viewer()].hitsScored),
             static_cast<unsigned>(FLEET_CELLS),
             static_cast<unsigned>(side_[viewer()].hitsTaken));
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(line, r.x, r.y, 1);
}

void SeaBattleGame::drawButtons(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    if (phase_ == Phase::Placing) {
        Ui::drawButton(tft, readyRect(), "Ready", Ui::success(), Ui::outline(),
                       Ui::bg(), false, 2);
        Ui::drawButton(tft, actionRect(), "Shuffle", Ui::panel(), Ui::outline(),
                       Ui::text(), false, 2);
        return;
    }
    const bool over = gameOver();
    const bool confirming = !over && confirmUntilMs_ != 0;
    Ui::drawButton(tft, actionRect(),
                   over ? "New game" : (confirming ? "Sure?" : "End game"),
                   confirming ? Ui::warning() : Ui::panel(), Ui::outline(),
                   confirming ? Ui::bg() : Ui::text(), false, 2);
}

/* The curtain. Nothing of either sea on it, which is the entire point: this
 * screen exists so that one player can hand the console to the other without
 * handing over the answers. */
void SeaBattleGame::renderPass(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    char line[32];
    snprintf(line, sizeof(line), "Pass to Player %u",
             static_cast<unsigned>(turnPlayer_ + 1));

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(line, GAME_CANVAS_WIDTH / 2, 108, 4);

    const char* sub = !placed_[turnPlayer_] ? "then place your fleet"
                      : lastShot_ == Shot::Sunk ? "a ship went down!"
                      : lastShot_ == Shot::Hit  ? "that was a hit"
                      : lastShot_ == Shot::Miss ? "that was a miss"
                                                : "then take your shot";
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(sub, GAME_CANVAS_WIDTH / 2, 140, 2);
    tft.drawString("Tap when ready", GAME_CANVAS_WIDTH / 2, 180, 2);
    tft.setTextDatum(TL_DATUM);
}

void SeaBattleGame::renderStatic(AppContext& host) {
    if (mode_ == Mode::Lobby) {
        renderLobby(host);
        return;
    }
    if (phase_ == Phase::Passing) {
        renderPass(host);
        dirtyCount_ = 0;
        return;
    }
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    drawBoard(host);
    drawMini(host);
    drawStatus(host);
    drawTally(host);
    drawButtons(host);
    dirtyCount_ = 0;
    fullPaint_ = false;
    panelStale_ = false;
    pausePainted_ = false;   // the sea was just repainted under it
    drawPause(host);
}

void SeaBattleGame::renderDynamic(AppContext& host) {
    if (mode_ == Mode::Lobby) {
        renderLobby(host);
        return;
    }
    if (phase_ == Phase::Passing || fullPaint_) {
        renderStatic(host);
        return;
    }
    if (dirtyCount_ != 0) pausePainted_ = false;   // a cell under the card
    for (uint8_t i = 0; i < dirtyCount_; ++i) drawCell(host, dirty_[i]);
    dirtyCount_ = 0;
    if (panelStale_) {
        drawMini(host);
        drawStatus(host);
        drawTally(host);
        drawButtons(host);
        panelStale_ = false;
    }
    drawPause(host);
}
