#include "ChessGame.h"

#include <string.h>

#include "ChessInternal.h"
#include "ChessSprites.h"

/* Chess geometry and drawing. Every rectangle is measured from the live
 * panel; every repaint is of a square that changed or a panel part that is
 * stale. See ChessGame.h. */

namespace {

constexpr int16_t TOP_BAR_H = 30;
constexpr int16_t MARGIN = 3;

/* The panel beside (or below) the board: captured pieces, the status lines and
 * the End/New game button.
 *
 * In landscape the board is square and the panel is everything the board does
 * not need, which on a 320x240 console is a column about 110px wide that this
 * screen used to leave as two empty gutters. That was the whole of the
 * complaint -- a 176px board floating in the middle of a 320px panel with
 * nothing either side of it. Sizing the board from the full height instead
 * takes it to 200px, and the captured pieces and the button move into the
 * space that buys.
 *
 * In portrait the board is already as wide as the screen, so the panel becomes
 * a band underneath and this is how tall it is. Every other measurement below
 * is derived from one of these two facts. */
constexpr int16_t PANEL_PORTRAIT_H = 72;
constexpr int16_t ACTION_H = 26;
constexpr int16_t GAP = 3;
/* Two lines of font 1 with a little air. The status has to fit a 110px column
 * in landscape, which is why it is two short lines rather than one sentence:
 * "Checkmate - Black wins" is 130px at font 2 and would be silently chopped
 * mid-word by the driver, with no ellipsis to show it had happened. */
constexpr int16_t STATUS_H = 24;

}   // namespace

// ---------------------------------------------------------------- layout

bool ChessGame::sidePanel(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    return tft.width() > tft.height();
}

/* Square, and sized from whichever axis the panel is not eating.
 *
 * Landscape: the full height, with the panel taking the width left over.
 * Portrait: the full width, with the panel taking a band underneath.
 * Either way the board gets the larger of the two dimensions it could have
 * had, which is the point -- see PANEL_PORTRAIT_H. */
Rect ChessGame::boardRect(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const int16_t w = static_cast<int16_t>(tft.width());
    const int16_t h = static_cast<int16_t>(tft.height());
    const int16_t top = static_cast<int16_t>(TOP_BAR_H + MARGIN);

    int16_t side;
    if (sidePanel(host)) {
        side = static_cast<int16_t>(h - top - MARGIN);
    } else {
        const int16_t availH =
            static_cast<int16_t>(h - top - PANEL_PORTRAIT_H - MARGIN * 2);
        const int16_t availW = static_cast<int16_t>(w - MARGIN * 2);
        side = (availW < availH ? availW : availH);
    }
    /* A multiple of 8, so every square is the same size and the grid has no
     * one-pixel-wider column where the division left a remainder. */
    side = static_cast<int16_t>((side / 8) * 8);
    const int16_t x = sidePanel(host)
                          ? MARGIN
                          : static_cast<int16_t>((w - side) / 2);
    return Rect{x, top, side, side};
}

/* Derived from the board rather than stated, so the two cannot overlap however
 * the panel is sized. CLAUDE.md's rule about clear rectangles applies with
 * particular force here: this panel is repainted whole, next to a board that
 * is repainted a square at a time, and a panel rect a few pixels too wide
 * would eat a file of the board on every capture. */
Rect ChessGame::panelRect(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const int16_t w = static_cast<int16_t>(tft.width());
    const int16_t h = static_cast<int16_t>(tft.height());
    const Rect b = boardRect(host);
    if (sidePanel(host)) {
        const int16_t x = static_cast<int16_t>(b.x + b.w + GAP);
        return Rect{x, b.y, static_cast<int16_t>(w - x - MARGIN), b.h};
    }
    const int16_t y = static_cast<int16_t>(b.y + b.h + GAP);
    return Rect{MARGIN, y, static_cast<int16_t>(w - MARGIN * 2),
                static_cast<int16_t>(h - y - MARGIN)};
}

/* The panel, top to bottom: White's losses, Black's losses, then the controls.
 * In landscape the controls are two stacked rows; in portrait there is only
 * room for one, so the status and the button sit side by side on it. */
Rect ChessGame::takenRect(AppContext& host, uint8_t side) const {
    const Rect p = panelRect(host);
    const int16_t controls = sidePanel(host)
        ? static_cast<int16_t>(STATUS_H + GAP + ACTION_H)
        : ACTION_H;
    const int16_t body = static_cast<int16_t>(p.h - controls - GAP);
    const int16_t each = static_cast<int16_t>((body - GAP) / 2);
    return Rect{p.x, static_cast<int16_t>(p.y + side * (each + GAP)), p.w, each};
}

Rect ChessGame::statusRect(AppContext& host) const {
    const Rect p = panelRect(host);
    if (sidePanel(host)) {
        return Rect{p.x,
                    static_cast<int16_t>(p.y + p.h - ACTION_H - GAP - STATUS_H),
                    p.w, STATUS_H};
    }
    /* Portrait: the button is pinned right and the status takes the rest, so
     * the two are derived from one edge and cannot overlap. */
    const Rect a = actionRect(host);
    return Rect{p.x, static_cast<int16_t>(p.y + p.h - ACTION_H),
                static_cast<int16_t>(a.x - GAP - p.x), ACTION_H};
}

Rect ChessGame::actionRect(AppContext& host) const {
    const Rect p = panelRect(host);
    const int16_t y = static_cast<int16_t>(p.y + p.h - ACTION_H);
    if (sidePanel(host)) return Rect{p.x, y, p.w, ACTION_H};
    const int16_t bw = 84;
    return Rect{static_cast<int16_t>(p.x + p.w - bw), y, bw, ACTION_H};
}

Rect ChessGame::squareRect(AppContext& host, uint8_t square) const {
    const Rect b = boardRect(host);
    const int16_t cell = static_cast<int16_t>(b.w / 8);
    /* Rank 0 is white's home and is drawn at the BOTTOM, the way a board is
     * set up in front of you. The board does not flip between turns: two
     * players sharing one device is confusing enough without the pieces
     * changing ends. */
    const int16_t col = fileOf(square);
    const int16_t row = static_cast<int16_t>(7 - rankOf(square));
    return Rect{static_cast<int16_t>(b.x + col * cell),
                static_cast<int16_t>(b.y + row * cell), cell, cell};
}

uint8_t ChessGame::squareAt(AppContext& host, int16_t x, int16_t y) const {
    const Rect b = boardRect(host);
    if (!b.contains(x, y, 0)) return NO_SQ;
    const int16_t cell = static_cast<int16_t>(b.w / 8);
    const int16_t col = static_cast<int16_t>((x - b.x) / cell);
    const int16_t row = static_cast<int16_t>((y - b.y) / cell);
    if (col < 0 || col > 7 || row < 0 || row > 7) return NO_SQ;
    return idx(static_cast<int8_t>(col), static_cast<int8_t>(7 - row));
}


/* The lobby. Two ways to play and a list of consoles in the room.
 *
 * Laid out against the live panel like everything else here, and the rows are
 * generous -- this is a menu a child taps once, not a board they play on, so
 * there is no reason to make the targets small. */
Rect ChessGame::lobbyRowRect(AppContext& host, uint8_t row) const {
    Ui::Renderer& tft = host.display();
    const int16_t w = static_cast<int16_t>(tft.width());
    const int16_t top = static_cast<int16_t>(TOP_BAR_H + 8);
    const int16_t h = 30;
    return Rect{10, static_cast<int16_t>(top + row * (h + 6)),
                static_cast<int16_t>(w - 20), h};
}


/* Blit a piece silhouette, scaled to the square.
 *
 * Shapes, not letters. A letter is a literacy test: the youngest players this
 * console is for cannot read one, and at 26px on a resistive panel neither can
 * anyone whose sight is not sharp. The silhouettes come from
 * tools/gen_chess_sprites.py, which rasterises the traced sheet and writes a
 * preview so the shapes can be looked at rather than reasoned about.
 *
 * Nearest-neighbour, iterating over DESTINATION pixels rather than source
 * ones. The board is usually smaller than the 32px mask -- 26px squares in
 * landscape on the 2.8-inch -- so walking the source would skip destination
 * pixels and leave the shape full of holes. Walking the destination cannot.
 *
 * Runs, not pixels: a row is emitted as horizontal spans, so a piece costs
 * about `cell` line calls instead of `cell * cell` pixel calls. On a full
 * board repaint that is the difference between comfortable and a visible
 * stutter.
 *
 * Drawn three times: the rim colour offset one pixel each diagonal, then the
 * fill on top. That rim is what keeps a white piece legible on a light square
 * and a black one on a dark square, in all nine themes, without needing a
 * per-theme piece colour. */
void ChessGame::drawPiece(AppContext& host, const Rect& r, int8_t piece) const {
    if (piece == EMPTY) return;
    Ui::Renderer& tft = host.display();
    const bool white = isWhite(piece);
    const uint32_t* mask = ChessSprites::MASK[kind(piece) - 1];

    const uint16_t fill = white ? Ui::rgb(250, 250, 246) : Ui::rgb(20, 20, 26);
    const uint16_t rim  = white ? Ui::rgb(20, 20, 26) : Ui::rgb(250, 250, 246);

    const int16_t cell = r.w < r.h ? r.w : r.h;
    if (cell <= 0) return;
    const int16_t x0 = static_cast<int16_t>(r.x + (r.w - cell) / 2);
    const int16_t y0 = static_cast<int16_t>(r.y + (r.h - cell) / 2);

    auto blit = [&](int16_t dx, int16_t dy, uint16_t colour) {
        for (int16_t py = 0; py < cell; ++py) {
            const uint8_t sy = static_cast<uint8_t>(
                (static_cast<int32_t>(py) * ChessSprites::SIZE) / cell);
            const uint32_t row = mask[sy];
            if (row == 0) continue;
            int16_t runStart = -1;
            for (int16_t px = 0; px <= cell; ++px) {
                bool on = false;
                if (px < cell) {
                    const uint8_t sx = static_cast<uint8_t>(
                        (static_cast<int32_t>(px) * ChessSprites::SIZE) / cell);
                    on = (row >> (31 - sx)) & 1u;
                }
                if (on && runStart < 0) {
                    runStart = px;
                } else if (!on && runStart >= 0) {
                    tft.drawFastHLine(x0 + dx + runStart, y0 + dy + py,
                                      px - runStart, colour);
                    runStart = -1;
                }
            }
        }
    };

    blit(1, 1, rim);
    blit(-1, -1, rim);
    blit(0, 0, fill);
}

void ChessGame::drawSquare(AppContext& host, uint8_t square) const {
    Ui::Renderer& tft = host.display();
    const Rect r = squareRect(host, square);
    const bool light = ((fileOf(square) + rankOf(square)) % 2) != 0;

    bool isTarget = false;
    for (uint8_t i = 0; i < targetCount_; ++i) {
        if (targets_[i] == square) { isTarget = true; break; }
    }

    uint16_t fill = light ? Ui::rgb(222, 210, 180) : Ui::rgb(120, 96, 72);
    if (square == selected_) fill = Ui::success();
    tft.fillRect(r.x, r.y, r.w, r.h, fill);
    drawPiece(host, r, pos_.sq[square]);

    /* A legal destination is marked with a ring rather than a fill, so the
     * piece standing on a capturable square is still visible underneath. A
     * child needs to see what they are taking. */
    if (isTarget) {
        const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);
        const int16_t cy = static_cast<int16_t>(r.y + r.h / 2);
        tft.drawCircle(cx, cy, static_cast<int16_t>(r.w / 2 - 2), Ui::success());
        tft.drawCircle(cx, cy, static_cast<int16_t>(r.w / 2 - 3), Ui::success());
    }
}

/* Two short lines rather than one sentence, and that is a measurement, not a
 * preference. In landscape the status now lives in a column about 110px wide;
 * "Checkmate - Black wins" is well past that at font 2, and TFT_eSPI does not
 * ellipsize -- drawChar simply stops once x reaches the viewport edge, so the
 * line would be cut mid-word with nothing to show it had been. Splitting the
 * headline from the detail fits both orientations and reads better anyway. */
void ChessGame::drawStatus(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const Rect r = statusRect(host);
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());

    char top[24];
    char bot[24];
    top[0] = 0;
    bot[0] = 0;
    uint16_t colour = Ui::text();
    const char* side = pos_.whiteToMove ? "White" : "Black";

    if (mode_ == Mode::Waiting) {
        snprintf(top, sizeof(top), "Asking %s", opponentLabel());
        snprintf(bot, sizeof(bot), "waiting...");
        colour = Ui::muted();
    } else {
        switch (status_) {
            case Status::Checkmate:
                /* The side to move is the one that is mated, so the winner is
                 * the other one -- stating the loser here would be a small
                 * cruelty and a large confusion. */
                snprintf(top, sizeof(top), "Checkmate");
                snprintf(bot, sizeof(bot), "%s wins",
                         pos_.whiteToMove ? "Black" : "White");
                colour = Ui::warning();
                break;
            /* Every draw says WHY, in words a child can act on. "Draw" on
             * its own teaches nothing; "too few pieces" is the whole lesson of
             * the endgame they have just reached, and it is the difference
             * between the console looking broken and the console teaching. */
            case Status::Stalemate:
                snprintf(top, sizeof(top), "Draw");
                snprintf(bot, sizeof(bot), "stalemate");
                break;
            case Status::DrawMaterial:
                snprintf(top, sizeof(top), "Draw");
                snprintf(bot, sizeof(bot), "too few pieces");
                break;
            case Status::DrawFifty:
                snprintf(top, sizeof(top), "Draw");
                snprintf(bot, sizeof(bot), "50 moves, no take");
                break;
            case Status::Ended:
                /* NOT called a draw. A game somebody walked away from and a
                 * game the rules drew are different things, and a console that
                 * blurred them would be teaching the wrong lesson in the other
                 * direction from the one this release fixed. */
                snprintf(top, sizeof(top), "Game ended");
                snprintf(bot, sizeof(bot),
                         mode_ == Mode::Remote
                             ? (endedByUs_ ? "you stopped it" : "they stopped it")
                             : "no winner");
                colour = Ui::muted();
                break;
            case Status::Check:
                snprintf(top, sizeof(top), "Check!");
                snprintf(bot, sizeof(bot), "%s to move", side);
                colour = Ui::warning();
                break;
            default:
                if (mode_ == Mode::Remote && !ourTurn()) {
                    /* Second person, because in a remote game "White to move"
                     * does not tell a child whether to pick a piece up. */
                    snprintf(top, sizeof(top), "%s", opponentLabel());
                    snprintf(bot, sizeof(bot), "is thinking");
                    colour = Ui::muted();
                } else if (mode_ == Mode::Remote) {
                    snprintf(top, sizeof(top), "Your move");
                    snprintf(bot, sizeof(bot), "(%s)",
                             remoteIsWhite_ ? "White" : "Black");
                } else {
                    snprintf(top, sizeof(top), "%s", side);
                    snprintf(bot, sizeof(bot), "to move");
                }
                break;
        }
    }

    /* Font from the space actually available, not from the orientation: the
     * 4-inch panel's landscape column is 190px and can carry font 2, the
     * 2.8-inch's is 110px and cannot. */
    const uint8_t font = r.w >= 150 ? 2 : 1;
    const int16_t lineH = font == 2 ? 16 : 10;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(colour, Ui::bg());
    tft.drawString(top, r.x, r.y, font);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(bot, r.x, static_cast<int16_t>(r.y + lineH), font);
}

/* The pieces one colour has lost, oldest first.
 *
 * Shapes at whatever size fits, for the same reason the board uses shapes: a
 * letter is a literacy test. The cell is chosen so that all fifteen a side can
 * lose fit in the strip -- shrinking until they do rather than picking a
 * number, because the strip is 110x60 in landscape on the 2.8-inch and 234x20
 * in portrait, and no single constant is right for both. */
void ChessGame::drawTaken(AppContext& host, uint8_t side) const {
    Ui::Renderer& tft = host.display();
    const Rect r = takenRect(host, side);
    if (r.w <= 0 || r.h <= 0) return;
    /* Each strip is backed by the shade its pieces are NOT, so White's losses
     * sit on the board's dark square colour and Black's on the light one.
     *
     * Both strips used the theme panel colour, and on a photographed 4-inch
     * panel the two were very hard to tell apart -- which defeats the point of
     * showing them separately at all. Borrowing the board's own two square
     * colours also says, without a label, that these pieces came off that
     * board. The pieces carry a contrasting rim of their own, so they stay
     * legible on either. */
    const uint16_t back = side == 0 ? Ui::rgb(120, 96, 72)
                                    : Ui::rgb(222, 210, 180);
    tft.fillRect(r.x, r.y, r.w, r.h, back);

    int16_t cell = 20;
    while (cell > 10 && (r.w / cell) * (r.h / cell) < MAX_TAKEN) {
        cell = static_cast<int16_t>(cell - 2);
    }
    const int16_t cols = static_cast<int16_t>(r.w / cell);
    const int16_t rows = static_cast<int16_t>(r.h / cell);
    if (cols <= 0 || rows <= 0) return;

    const uint8_t capacity = static_cast<uint8_t>(cols * rows);
    const uint8_t n = takenCount_[side];
    uint8_t shown = n < capacity ? n : capacity;
    /* If even the shrunk cell cannot hold them all, give the last slot to a
     * count rather than silently dropping pieces off the end. */
    const bool overflow = n > capacity;
    if (overflow) shown = static_cast<uint8_t>(capacity - 1);

    for (uint8_t i = 0; i < shown; ++i) {
        const Rect c{static_cast<int16_t>(r.x + (i % cols) * cell),
                     static_cast<int16_t>(r.y + (i / cols) * cell), cell, cell};
        drawPiece(host, c, taken_[side][i]);
    }
    if (overflow) {
        char more[8];
        snprintf(more, sizeof(more), "+%u", static_cast<unsigned>(n - shown));
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(side == 0 ? Ui::rgb(240, 236, 228) : Ui::rgb(60, 50, 40),
                         back);
        tft.drawString(more,
                       static_cast<int16_t>(r.x + (shown % cols) * cell + cell / 2),
                       static_cast<int16_t>(r.y + (shown / cols) * cell + cell / 2),
                       1);
        tft.setTextDatum(TL_DATUM);
    }
}

void ChessGame::drawAction(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const Rect r = actionRect(host);
    const bool over = gameOver();
    const bool confirming = !over && confirmUntilMs_ != 0;
    /* Warning colour only while it is asking. A destructive control that looks
     * alarming all the time stops meaning anything by the second game. */
    Ui::drawButton(tft, r, over ? "New game" : (confirming ? "Sure?" : "End game"),
                   confirming ? Ui::warning() : Ui::panel(), Ui::outline(),
                   confirming ? Ui::bg() : Ui::text(), false, 2);
}

void ChessGame::renderStatic(AppContext& host) {
    if (mode_ == Mode::Lobby) {
        renderLobby(host);
        return;
    }
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    for (uint8_t s = 0; s < 64; ++s) drawSquare(host, s);
    const Rect b = boardRect(host);
    tft.drawRect(b.x, b.y, b.w, b.h, Ui::outline());
    drawTaken(host, 0);
    drawTaken(host, 1);
    drawStatus(host);
    drawAction(host);
    dirtyCount_ = 0;
    statusStale_ = false;
    panelStale_ = false;
}

void ChessGame::renderDynamic(AppContext& host) {
    if (mode_ == Mode::Lobby) {
        renderLobby(host);
        return;
    }
    /* Only the squares that changed. A whole board is 64 fills and up to 32
     * pieces; a move touches a handful, and at 26px a square the difference is
     * the whole frame budget. */
    for (uint8_t i = 0; i < dirtyCount_; ++i) drawSquare(host, dirtySq_[i]);
    dirtyCount_ = 0;
    /* The panel is repainted whole, unlike the board. It is a fraction of the
     * area and it changes rarely -- a capture, or the button relabelling
     * itself -- so working out which strip moved would cost more to maintain
     * than it saves, and every clear rectangle here is one more chance to take
     * a bite out of the board next to it. */
    if (panelStale_) {
        drawTaken(host, 0);
        drawTaken(host, 1);
        drawAction(host);
        panelStale_ = false;
    }
    if (statusStale_) {
        drawStatus(host);
        statusStale_ = false;
    }
}
