#include "ChessGame.h"

#include <string.h>

#include "ChessSprites.h"
#include "engine/AppRegistry.h"

namespace {

constexpr AppMetadata CHESS_METADATA = {
    "chess",
    "Chess",
    nullptr,
    "two players",
    "Chess",
    "Two players. Tap a piece to see its moves.",
    nullptr,
    LauncherIcon::Chess,
    32,
    true,
};

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

/* Knight and king offsets as (file, rank) pairs. Written out rather than
 * derived because the board is not a torus: a knight on a1 must not wrap to
 * h-file, and offset arithmetic on a flat 64-array does exactly that. Every
 * generator below works in file/rank and tests onBoard() for the same reason. */
constexpr int8_t KNIGHT_DF[8] = {1, 2, 2, 1, -1, -2, -2, -1};
constexpr int8_t KNIGHT_DR[8] = {2, 1, -1, -2, -2, -1, 1, 2};
constexpr int8_t KING_DF[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int8_t KING_DR[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int8_t ROOK_DF[4] = {0, 1, 0, -1};
constexpr int8_t ROOK_DR[4] = {1, 0, -1, 0};
constexpr int8_t BISHOP_DF[4] = {1, 1, -1, -1};
constexpr int8_t BISHOP_DR[4] = {1, -1, -1, 1};

/* Named idx() and not the obvious sq(): Arduino already defines sq(x) as "x
 * squared", so a two-argument call to it is a preprocessor error rather than a
 * shadowing warning, and the message names a macro you never wrote. The
 * Position member stays .sq -- member access never reaches the preprocessor. */
constexpr uint8_t idx(int8_t file, int8_t rank) {
    return static_cast<uint8_t>(rank * 8 + file);
}
constexpr int8_t fileOf(uint8_t s) { return static_cast<int8_t>(s % 8); }
constexpr int8_t rankOf(uint8_t s) { return static_cast<int8_t>(s / 8); }

/* The back rank, used to set up and to spell a promotion. */
constexpr int8_t BACK_RANK[8] = {
    ChessGame::ROOK, ChessGame::KNIGHT, ChessGame::BISHOP, ChessGame::QUEEN,
    ChessGame::KING, ChessGame::BISHOP, ChessGame::KNIGHT, ChessGame::ROOK,
};

}   // namespace

const AppMetadata& chessAppMetadata() {
    return CHESS_METADATA;
}

const char* ChessGame::title() const {
    return chessAppMetadata().screenTitle != nullptr
        ? chessAppMetadata().screenTitle
        : chessAppMetadata().title;
}

// ---------------------------------------------------------------- rules

uint8_t ChessGame::kingSquare(const Position& p, bool white) {
    for (uint8_t s = 0; s < 64; ++s) {
        if (p.sq[s] != EMPTY && kind(p.sq[s]) == KING && isWhite(p.sq[s]) == white) {
            return s;
        }
    }
    return NO_SQ;   // only reachable from a corrupt position
}

/* Is `square` attacked by the given side?
 *
 * Written as "look outward from the square for an attacker" rather than
 * "generate every enemy move and see if any lands here". Both are correct; this
 * one is bounded by the eight rays and eight knight hops regardless of how many
 * pieces are on the board, and it does not need move generation -- which
 * matters because move generation needs *this*, and the other order is a
 * recursion waiting to happen.
 */
bool ChessGame::attacked(const Position& p, uint8_t square, bool bySideIsWhite) {
    const int8_t f = fileOf(square);
    const int8_t r = rankOf(square);

    // Pawns. They capture forwards, so look backwards from the square.
    const int8_t pawnDir = bySideIsWhite ? -1 : 1;
    for (int8_t df = -1; df <= 1; df += 2) {
        const int8_t nf = static_cast<int8_t>(f + df);
        const int8_t nr = static_cast<int8_t>(r + pawnDir);
        if (!onBoard(nf, nr)) continue;
        const int8_t piece = p.sq[idx(nf, nr)];
        if (piece != EMPTY && isWhite(piece) == bySideIsWhite && kind(piece) == PAWN) {
            return true;
        }
    }

    for (uint8_t i = 0; i < 8; ++i) {
        const int8_t nf = static_cast<int8_t>(f + KNIGHT_DF[i]);
        const int8_t nr = static_cast<int8_t>(r + KNIGHT_DR[i]);
        if (!onBoard(nf, nr)) continue;
        const int8_t piece = p.sq[idx(nf, nr)];
        if (piece != EMPTY && isWhite(piece) == bySideIsWhite && kind(piece) == KNIGHT) {
            return true;
        }
    }

    for (uint8_t i = 0; i < 8; ++i) {
        const int8_t nf = static_cast<int8_t>(f + KING_DF[i]);
        const int8_t nr = static_cast<int8_t>(r + KING_DR[i]);
        if (!onBoard(nf, nr)) continue;
        const int8_t piece = p.sq[idx(nf, nr)];
        if (piece != EMPTY && isWhite(piece) == bySideIsWhite && kind(piece) == KING) {
            return true;
        }
    }

    // Sliders: rook/queen along ranks and files, bishop/queen along diagonals.
    for (uint8_t dir = 0; dir < 4; ++dir) {
        for (int8_t step = 1; step < 8; ++step) {
            const int8_t nf = static_cast<int8_t>(f + ROOK_DF[dir] * step);
            const int8_t nr = static_cast<int8_t>(r + ROOK_DR[dir] * step);
            if (!onBoard(nf, nr)) break;
            const int8_t piece = p.sq[idx(nf, nr)];
            if (piece == EMPTY) continue;
            if (isWhite(piece) == bySideIsWhite &&
                (kind(piece) == ROOK || kind(piece) == QUEEN)) {
                return true;
            }
            break;   // any other piece blocks the ray
        }
    }
    for (uint8_t dir = 0; dir < 4; ++dir) {
        for (int8_t step = 1; step < 8; ++step) {
            const int8_t nf = static_cast<int8_t>(f + BISHOP_DF[dir] * step);
            const int8_t nr = static_cast<int8_t>(r + BISHOP_DR[dir] * step);
            if (!onBoard(nf, nr)) break;
            const int8_t piece = p.sq[idx(nf, nr)];
            if (piece == EMPTY) continue;
            if (isWhite(piece) == bySideIsWhite &&
                (kind(piece) == BISHOP || kind(piece) == QUEEN)) {
                return true;
            }
            break;
        }
    }
    return false;
}

uint8_t ChessGame::pseudoMoves(const Position& p, uint8_t from, uint8_t* out) {
    const int8_t piece = p.sq[from];
    if (piece == EMPTY || isWhite(piece) != p.whiteToMove) return 0;

    const bool white = isWhite(piece);
    const int8_t f = fileOf(from);
    const int8_t r = rankOf(from);
    uint8_t n = 0;

    auto add = [&](int8_t nf, int8_t nr) {
        if (!onBoard(nf, nr)) return;
        const int8_t target = p.sq[idx(nf, nr)];
        if (target != EMPTY && isWhite(target) == white) return;
        if (n < MAX_MOVES) out[n++] = idx(nf, nr);
    };

    switch (kind(piece)) {
        case PAWN: {
            const int8_t dir = white ? 1 : -1;
            const int8_t start = white ? 1 : 6;
            // Forward one, and two from the start -- neither may capture.
            if (onBoard(f, r + dir) && p.sq[idx(f, r + dir)] == EMPTY) {
                if (n < MAX_MOVES) out[n++] = idx(f, static_cast<int8_t>(r + dir));
                if (r == start && p.sq[idx(f, r + dir * 2)] == EMPTY) {
                    if (n < MAX_MOVES) out[n++] = idx(f, static_cast<int8_t>(r + dir * 2));
                }
            }
            // Diagonal captures, including en passant.
            for (int8_t df = -1; df <= 1; df += 2) {
                const int8_t nf = static_cast<int8_t>(f + df);
                const int8_t nr = static_cast<int8_t>(r + dir);
                if (!onBoard(nf, nr)) continue;
                const uint8_t to = idx(nf, nr);
                const int8_t target = p.sq[to];
                const bool capture = target != EMPTY && isWhite(target) != white;
                if ((capture || to == p.epSquare) && n < MAX_MOVES) out[n++] = to;
            }
            break;
        }
        case KNIGHT:
            for (uint8_t i = 0; i < 8; ++i) {
                add(static_cast<int8_t>(f + KNIGHT_DF[i]),
                    static_cast<int8_t>(r + KNIGHT_DR[i]));
            }
            break;
        case KING:
            for (uint8_t i = 0; i < 8; ++i) {
                add(static_cast<int8_t>(f + KING_DF[i]),
                    static_cast<int8_t>(r + KING_DR[i]));
            }
            /* Castling. Three conditions and all of them matter: the right
             * survives, the squares between are empty, and the king may not
             * start in check, pass through an attacked square, or land on one.
             * The last is why this asks attacked() three times rather than
             * leaving it to the legality filter -- that filter only checks
             * where the king ENDS UP, and a king may not castle out of or
             * through check even when the destination is safe. */
            if (kind(piece) == KING) {
                const bool kingSide = white ? p.castle[0] : p.castle[2];
                const bool queenSide = white ? p.castle[1] : p.castle[3];
                const int8_t homeRank = white ? 0 : 7;
                if (r == homeRank && f == 4 && !attacked(p, from, !white)) {
                    if (kingSide && p.sq[idx(5, homeRank)] == EMPTY &&
                        p.sq[idx(6, homeRank)] == EMPTY &&
                        !attacked(p, idx(5, homeRank), !white) &&
                        !attacked(p, idx(6, homeRank), !white)) {
                        if (n < MAX_MOVES) out[n++] = idx(6, homeRank);
                    }
                    if (queenSide && p.sq[idx(3, homeRank)] == EMPTY &&
                        p.sq[idx(2, homeRank)] == EMPTY &&
                        p.sq[idx(1, homeRank)] == EMPTY &&
                        !attacked(p, idx(3, homeRank), !white) &&
                        !attacked(p, idx(2, homeRank), !white)) {
                        if (n < MAX_MOVES) out[n++] = idx(2, homeRank);
                    }
                }
            }
            break;
        default: {
            // Sliders. Bishop and rook are the queen with one set of rays.
            const bool diag = kind(piece) == BISHOP || kind(piece) == QUEEN;
            const bool straight = kind(piece) == ROOK || kind(piece) == QUEEN;
            for (uint8_t dir = 0; dir < 4 && straight; ++dir) {
                for (int8_t step = 1; step < 8; ++step) {
                    const int8_t nf = static_cast<int8_t>(f + ROOK_DF[dir] * step);
                    const int8_t nr = static_cast<int8_t>(r + ROOK_DR[dir] * step);
                    if (!onBoard(nf, nr)) break;
                    const int8_t target = p.sq[idx(nf, nr)];
                    if (target == EMPTY) { add(nf, nr); continue; }
                    add(nf, nr);
                    break;
                }
            }
            for (uint8_t dir = 0; dir < 4 && diag; ++dir) {
                for (int8_t step = 1; step < 8; ++step) {
                    const int8_t nf = static_cast<int8_t>(f + BISHOP_DF[dir] * step);
                    const int8_t nr = static_cast<int8_t>(r + BISHOP_DR[dir] * step);
                    if (!onBoard(nf, nr)) break;
                    const int8_t target = p.sq[idx(nf, nr)];
                    if (target == EMPTY) { add(nf, nr); continue; }
                    add(nf, nr);
                    break;
                }
            }
            break;
        }
    }
    return n;
}

int8_t ChessGame::applyMove(Position& p, uint8_t from, uint8_t to) {
    const int8_t piece = p.sq[from];
    const bool white = isWhite(piece);
    const int8_t k = kind(piece);
    int8_t captured = p.sq[to];

    /* En passant capture removes a pawn that is NOT on the destination square,
     * which is the one move in chess where that is true -- so it is also the
     * one move where the captured piece has to be read from somewhere other
     * than `to`, which is the reason this function reports the capture at all
     * rather than leaving the caller to work it out. */
    if (k == PAWN && to == p.epSquare && p.sq[to] == EMPTY) {
        const uint8_t victim = idx(fileOf(to), rankOf(from));
        captured = p.sq[victim];
        p.sq[victim] = EMPTY;
    }

    // Castling moves the rook too; the king's two-square step identifies it.
    if (k == KING && fileOf(from) == 4) {
        const int8_t homeRank = rankOf(from);
        if (fileOf(to) == 6) {
            p.sq[idx(5, homeRank)] = p.sq[idx(7, homeRank)];
            p.sq[idx(7, homeRank)] = EMPTY;
        } else if (fileOf(to) == 2) {
            p.sq[idx(3, homeRank)] = p.sq[idx(0, homeRank)];
            p.sq[idx(0, homeRank)] = EMPTY;
        }
    }

    p.sq[to] = piece;
    p.sq[from] = EMPTY;

    /* Promotion is automatic and always a queen. Under-promotion exists and is
     * occasionally the only winning move, but a chooser is a modal dialog on a
     * screen with no room for one, and this is a game two children play. */
    if (k == PAWN && (rankOf(to) == 7 || rankOf(to) == 0)) {
        p.sq[to] = static_cast<int8_t>(white ? QUEEN : -QUEEN);
    }

    /* A double push offers en passant for exactly one move; anything else
     * withdraws it. Clearing this unconditionally first is what stops a stale
     * offer surviving into a later position. */
    p.epSquare = NO_SQ;
    if (k == PAWN && (rankOf(to) - rankOf(from) == 2 || rankOf(from) - rankOf(to) == 2)) {
        p.epSquare = idx(fileOf(from), static_cast<int8_t>((rankOf(from) + rankOf(to)) / 2));
    }

    /* Castling rights are lost by the king moving, by the rook moving, or by
     * the rook being captured on its home square -- that last one is easy to
     * forget and shows up as a castle through a rook that is not there. */
    if (k == KING) {
        if (white) { p.castle[0] = p.castle[1] = false; }
        else       { p.castle[2] = p.castle[3] = false; }
    }
    const uint8_t H1 = idx(7, 0), A1 = idx(0, 0), H8 = idx(7, 7), A8 = idx(0, 7);
    if (from == H1 || to == H1) p.castle[0] = false;
    if (from == A1 || to == A1) p.castle[1] = false;
    if (from == H8 || to == H8) p.castle[2] = false;
    if (from == A8 || to == A8) p.castle[3] = false;

    /* The fifty-move clock. Reset by a capture or a pawn move -- the two
     * things that cannot be undone -- and counted in plies, so the rule is a
     * hundred of these rather than fifty. Saturates instead of wrapping: at
     * 255 the game has been drawn for a long time and rolling over to zero
     * would quietly restart the count. */
    if (k == PAWN || captured != EMPTY) {
        p.halfmove = 0;
    } else if (p.halfmove < 255) {
        ++p.halfmove;
    }

    p.whiteToMove = !p.whiteToMove;
    return captured;
}

uint8_t ChessGame::legalMoves(const Position& p, uint8_t from, uint8_t* out) {
    uint8_t pseudo[MAX_MOVES];
    const uint8_t count = pseudoMoves(p, from, pseudo);
    const bool white = p.whiteToMove;
    uint8_t n = 0;
    for (uint8_t i = 0; i < count; ++i) {
        /* Make the move on a copy and ask whether our own king is attacked.
         * One test, and pins, discovered checks and the duty to answer a check
         * all come out of it -- none of them needs a rule of its own. */
        Position trial = p;
        applyMove(trial, from, pseudo[i]);
        if (!attacked(trial, kingSquare(trial, white), !white)) out[n++] = pseudo[i];
    }
    return n;
}

/* Can anybody still mate?
 *
 * The FIDE dead-position cases that are decidable by counting, and no attempt
 * at the ones that are not. A pawn, rook or queen anywhere on the board means
 * a mate is constructible, so the position is alive however lost it looks --
 * being unable to WIN is not the same as being unable to MATE, and only the
 * second one ends the game.
 *
 * Two bishops of the same colour on same-shaded squares is the one case that
 * needs more than a count: neither can ever attack the other's colour of
 * square, so between them they can never cover a king's escape. Bishops on
 * opposite shades can mate, so that case is alive.
 *
 * Deliberately not "can the side to move force a win". That is a search, and
 * this game has no engine -- see the class comment. */
bool ChessGame::deadPosition(const Position& p) {
    uint8_t minors[2] = {0, 0};       // [0] white, [1] black
    uint8_t bishops[2] = {0, 0};
    int8_t bishopShade[2] = {-1, -1}; // square colour of a lone bishop

    for (uint8_t sqr = 0; sqr < 64; ++sqr) {
        const int8_t piece = p.sq[sqr];
        if (piece == EMPTY) continue;
        const int8_t k = kind(piece);
        if (k == PAWN || k == ROOK || k == QUEEN) return false;
        if (k == KING) continue;
        const uint8_t side = isWhite(piece) ? 0 : 1;
        ++minors[side];
        if (k == BISHOP) {
            ++bishops[side];
            bishopShade[side] = static_cast<int8_t>((fileOf(sqr) + rankOf(sqr)) & 1);
        }
    }

    // King against king, and king plus one minor against a bare king.
    if (minors[0] == 0 && minors[1] == 0) return true;
    if (minors[0] <= 1 && minors[1] == 0) return true;
    if (minors[1] <= 1 && minors[0] == 0) return true;

    // One bishop each, both on the same shade of square.
    if (minors[0] == 1 && minors[1] == 1 && bishops[0] == 1 && bishops[1] == 1) {
        return bishopShade[0] == bishopShade[1];
    }
    return false;
}

bool ChessGame::hasAnyLegalMove(const Position& p) {
    uint8_t buf[MAX_MOVES];
    for (uint8_t s = 0; s < 64; ++s) {
        if (p.sq[s] == EMPTY || isWhite(p.sq[s]) != p.whiteToMove) continue;
        if (legalMoves(p, s, buf) > 0) return true;
    }
    return false;
}

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

// ---------------------------------------------------------------- screen

/* A fresh position in whatever mode is already running. The board, not the
 * session: a remote game resets to the starting position with the same
 * opponent, and only the lobby changes who is playing whom. */
void ChessGame::newGame() {
    memset(pos_.sq, EMPTY, sizeof(pos_.sq));
    for (int8_t f = 0; f < 8; ++f) {
        pos_.sq[idx(f, 0)] = BACK_RANK[f];
        pos_.sq[idx(f, 1)] = PAWN;
        pos_.sq[idx(f, 6)] = -PAWN;
        pos_.sq[idx(f, 7)] = static_cast<int8_t>(-BACK_RANK[f]);
    }
    pos_.whiteToMove = true;
    for (bool& c : pos_.castle) c = true;
    pos_.epSquare = NO_SQ;
    pos_.halfmove = 0;

    selected_ = NO_SQ;
    targetCount_ = 0;
    dirtyCount_ = 0;
    status_ = Status::Playing;
    statusStale_ = true;
    takenCount_[0] = takenCount_[1] = 0;
    endedByUs_ = false;
    confirmUntilMs_ = 0;
    ourPly_ = 0;
    theirPly_ = 0;
    panelStale_ = true;
    markFullDirty();
}

void ChessGame::begin(AppContext& host) {
    newGame();
    mode_ = Mode::Lobby;
    seatCount_ = 0;
    seatsAtMs_ = 0;
    opponent_[0] = 0;
    opponentName_[0] = 0;
    session_ = 0;

    /* A game left part-finished comes back. Anything else -- no save, a save
     * from an older layout, a game that had already ended -- falls through to
     * the lobby, which is also what a first visit gets. */
    restoreGame(host);
    markFullDirty();
}

void ChessGame::saveGame(AppContext& host) const {
    Saved out{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    memcpy(out.sq, pos_.sq, sizeof(out.sq));
    out.whiteToMove = pos_.whiteToMove ? 1 : 0;
    for (uint8_t i = 0; i < 4; ++i) out.castle[i] = pos_.castle[i] ? 1 : 0;
    out.epSquare = pos_.epSquare;
    out.halfmove = pos_.halfmove;
    out.status = static_cast<uint8_t>(status_);
    out.mode = static_cast<uint8_t>(mode_);
    out.remoteIsWhite = remoteIsWhite_ ? 1 : 0;
    out.endedByUs = endedByUs_ ? 1 : 0;
    memcpy(out.opponent, opponent_, sizeof(out.opponent));
    memcpy(out.opponentName, opponentName_, sizeof(out.opponentName));
    out.session = session_;
    out.ourPly = ourPly_;
    out.theirPly = theirPly_;
    out.ourFrom = ourFrom_;
    out.ourTo = ourTo_;
    out.takenCount[0] = takenCount_[0];
    out.takenCount[1] = takenCount_[1];
    memcpy(out.taken, taken_, sizeof(out.taken));
    host.saveBlob("game", &out, sizeof(out));
}

bool ChessGame::restoreGame(AppContext& host) {
    Saved in{};
    host.loadBlob("game", &in, sizeof(in));
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) return false;

    /* Only a game that is still going is worth coming back to. A finished one
     * is restored as nothing, so the screen opens at the lobby rather than at
     * a checkmate somebody already read. */
    const Mode m = static_cast<Mode>(in.mode);
    if (m != Mode::Local && m != Mode::Remote) return false;
    /* Only an unfinished game is worth coming back to. Every terminal status
     * is listed rather than tested for "not Playing", so adding a new way for
     * a game to end forces a decision here instead of silently restoring a
     * finished board. */
    const Status st = static_cast<Status>(in.status);
    if (st == Status::Checkmate || st == Status::Stalemate ||
        st == Status::DrawMaterial || st == Status::DrawFifty ||
        st == Status::Ended) {
        return false;
    }
    if (in.takenCount[0] > MAX_TAKEN || in.takenCount[1] > MAX_TAKEN) {
        return false;
    }

    memcpy(pos_.sq, in.sq, sizeof(pos_.sq));
    pos_.whiteToMove = in.whiteToMove != 0;
    for (uint8_t i = 0; i < 4; ++i) pos_.castle[i] = in.castle[i] != 0;
    pos_.epSquare = in.epSquare;
    pos_.halfmove = in.halfmove;

    /* Both kings, or it is not a chess position. This is the only validation
     * worth doing: the blob is our own NVS rather than anything that came off
     * the air, so the realistic failure is a layout change that slipped past
     * the length and version checks, not an attack. Restoring a board with no
     * king would divide by zero in kingSquare()'s callers' assumptions and
     * would be far harder to diagnose than starting over. */
    if (kingSquare(pos_, true) == NO_SQ || kingSquare(pos_, false) == NO_SQ) {
        return false;
    }

    mode_ = m;
    remoteIsWhite_ = in.remoteIsWhite != 0;
    endedByUs_ = in.endedByUs != 0;
    memcpy(opponent_, in.opponent, sizeof(opponent_));
    opponent_[sizeof(opponent_) - 1] = 0;
    memcpy(opponentName_, in.opponentName, sizeof(opponentName_));
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = in.session;
    ourPly_ = in.ourPly;
    theirPly_ = in.theirPly;
    ourFrom_ = in.ourFrom;
    ourTo_ = in.ourTo;
    takenCount_[0] = in.takenCount[0];
    takenCount_[1] = in.takenCount[1];
    memcpy(taken_, in.taken, sizeof(taken_));

    /* Recomputed rather than restored. Check and mate are functions of the
     * position, so storing them would be storing a fact twice and inviting the
     * two copies to disagree -- and refreshStatus() is cheap enough to run
     * once on the way in. */
    refreshStatus();
    return true;
}

/* end() rather than begin() is where the radio is handed back. A move is a
 * state that stays on the air until it is replaced, so leaving the screen
 * without clearing it would leave this console advertising a game it is no
 * longer playing -- and the score field it displaces would stay missing.
 *
 * The board is written here as well as after each move. After each move is
 * what survives a flat battery; here is what survives everything else, and
 * costs one NVS write on a screen change rather than one per frame. */
void ChessGame::end(AppContext& host) {
    if (mode_ == Mode::Local || mode_ == Mode::Remote) saveGame(host);
    host.nearbyStop();
}

void ChessGame::recordCapture(int8_t piece) {
    if (piece == EMPTY) return;
    const uint8_t side = isWhite(piece) ? 0 : 1;
    if (takenCount_[side] < MAX_TAKEN) {
        taken_[side][takenCount_[side]++] = piece;
        panelStale_ = true;
    }
}

/* Stop a game nobody is going to finish.
 *
 * This is a result, not an escape hatch: it is stored like one, the status
 * line says so, and the button afterwards offers a new game. Two children
 * abandon games constantly -- one of them loses interest, or the bell goes --
 * and before this the only exit was to leave the screen, which now brings the
 * same stuck position straight back.
 *
 * Over the radio it rides the move field with `from` equal to `to`. That is
 * never a legal chess move, so it cannot be confused with one, and it costs no
 * bytes on a payload that is already exactly 31. */
void ChessGame::declareEnd(AppContext& host, bool byUs) {
    status_ = Status::Ended;
    endedByUs_ = byUs;
    confirmUntilMs_ = 0;
    if (mode_ == Mode::Remote && byUs) {
        ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
        /* Published through the service's own call rather than by writing a
         * reserved square pair, so what "ended" looks like on the wire stays
         * one fact in one place. ourFrom_/ourTo_ are left alone: the frame
         * loop republishes them, and it must not republish this as a move. */
        host.nearbyEnd(session_, ourPly_, theirPly_);
    }
    for (uint8_t t = 0; t < targetCount_; ++t) markSquare(targets_[t]);
    markSquare(selected_);
    selected_ = NO_SQ;
    targetCount_ = 0;
    statusStale_ = true;
    panelStale_ = true;
    saveGame(host);
    markDirty();
}

void ChessGame::startLocal() {
    mode_ = Mode::Local;
    opponent_[0] = 0;
    newGame();
}

void ChessGame::startRemote(const NearbySeat& seat, uint8_t session,
                            bool weAreWhite) {
    strncpy(opponent_, seat.deviceId, sizeof(opponent_) - 1);
    opponent_[sizeof(opponent_) - 1] = 0;
    /* The label is copied once, here, rather than resolved on every frame:
     * it is display text and the peer table is behind a lock. It is saved with
     * the game for the same reason -- coming back to "A4F2 is thinking" after
     * naming that console RAVI would look like the name had not taken. */
    strncpy(opponentName_, seat.name, sizeof(opponentName_) - 1);
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = static_cast<uint8_t>(session & 0x3F);
    remoteIsWhite_ = weAreWhite;
    mode_ = Mode::Remote;
    newGame();
}

bool ChessGame::ourTurn() const {
    if (mode_ != Mode::Remote) return true;
    return pos_.whiteToMove == remoteIsWhite_;
}

void ChessGame::markSquare(uint8_t square) {
    if (square == NO_SQ) return;
    for (uint8_t i = 0; i < dirtyCount_; ++i) {
        if (dirtySq_[i] == square) return;
    }
    if (dirtyCount_ < sizeof(dirtySq_)) dirtySq_[dirtyCount_++] = square;
}

void ChessGame::refreshStatus() {
    const bool inCheck =
        attacked(pos_, kingSquare(pos_, pos_.whiteToMove), !pos_.whiteToMove);
    /* Order matters. Mate ends the game even in a position that is otherwise
     * dead -- you cannot be mated by pieces that cannot mate, so the two never
     * actually collide, but stating the precedence means nobody has to work
     * that out again. Having no legal move is asked first for the same reason:
     * stalemate is a draw arrived at by the rules of movement, not by counting
     * material or moves. */
    if (!hasAnyLegalMove(pos_)) {
        status_ = inCheck ? Status::Checkmate : Status::Stalemate;
    } else if (deadPosition(pos_)) {
        status_ = Status::DrawMaterial;
    } else if (pos_.halfmove >= 100) {
        status_ = Status::DrawFifty;
    } else {
        status_ = inCheck ? Status::Check : Status::Playing;
    }
    statusStale_ = true;
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

void ChessGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    Ui::drawButton(tft, lobbyRowRect(host, 0), "Pass and play",
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);

    char label[32];
    uint8_t row = 1;
    for (uint8_t i = 0; i < seatCount_ && row < 5; ++i, ++row) {
        /* A peer offering us a game reads differently from one merely
         * present, and the wording has to say which -- "Play A4F2" and
         * "A4F2 invites you" are different offers. */
        /* The owner's own label for that console when they have given it
         * one. Naming a peer is the whole point of being able to name one, so
         * it has to change what every screen calls it, not just the Nearby
         * app's list. */
        const char* who = seats_[i].name[0] != 0 ? seats_[i].name
                                                 : seats_[i].deviceId;
        if (seats_[i].inviting) {
            snprintf(label, sizeof(label), "%s invites you", who);
        } else {
            snprintf(label, sizeof(label), "Play %s", who);
        }
        Ui::drawButton(tft, lobbyRowRect(host, row), label,
                       seats_[i].inviting ? Ui::success() : Ui::panel(),
                       Ui::outline(), Ui::text(), false, 2);
    }

    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    /* The empty case names who can fix it. Switching the radio on is an
     * admin job, so a player who reads "turn Beacon on" and cannot find the
     * switch has been sent to a door they have no key for. Playing, once it is
     * on, needs no admin at all. */
    const char* note = seatCount_ > 0
        ? "Moves travel by Bluetooth. Anyone near hears them."
        : "Nobody nearby. An adult can switch Beacon and Nearby on.";
    tft.drawString(note, static_cast<int16_t>(tft.width() / 2),
                   static_cast<int16_t>(tft.height() - 6), 1);
    tft.setTextDatum(TL_DATUM);
}

void ChessGame::updateLobby(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (now - seatsAtMs_ > 1000) {
        seatsAtMs_ = now;
        NearbySeat fresh[6];
        uint8_t count = 0;
        const uint8_t n = host.nearbySeatCount();
        for (uint8_t i = 0; i < n && count < 6; ++i) {
            NearbySeat seat;
            if (host.nearbySeatAt(i, seat)) fresh[count++] = seat;
        }

        /* Compare the rows, not just how many there are. A peer that starts
         * inviting us does not change the count -- it changes what its row
         * says, from "Play A4F2" to "A4F2 invites you" -- so a count-only test
         * left an invitation sitting on the air with nothing on screen to
         * accept it, until some unrelated console wandered in or out of
         * range. */
        bool changed = count != seatCount_;
        for (uint8_t i = 0; !changed && i < count; ++i) {
            changed = fresh[i].inviting != seats_[i].inviting ||
                      strcmp(fresh[i].deviceId, seats_[i].deviceId) != 0;
        }
        for (uint8_t i = 0; i < count; ++i) seats_[i] = fresh[i];
        seatCount_ = count;
        if (changed) markFullDirty();
    }

    if (!touch.justPressed) return;

    if (lobbyRowRect(host, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.playSound(Sound::Select);
        startLocal();
        return;
    }
    for (uint8_t i = 0; i < seatCount_; ++i) {
        if (!lobbyRowRect(host, static_cast<uint8_t>(i + 1))
                 .contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        if (seats_[i].inviting) {
            /* Accepting: they chose the session, and which of us moves first
             * came with the invitation -- the service flipped for it. We
             * answer by publishing ply 0 in their session, which is the whole
             * handshake; there is no second round trip to go missing. */
            host.playSound(Sound::Select);
            startRemote(seats_[i], seats_[i].session, seats_[i].weMoveFirst);
        } else {
            /* Inviting: our session id, derived from the clock rather than a
             * counter so two consoles inviting each other at once are unlikely
             * to pick the same number. Which of us is White is NOT ours to
             * decide -- nearbyInvite() tosses for it and tells us. */
            const uint8_t session = static_cast<uint8_t>((millis() >> 3) & 0x3F);
            bool weMoveFirst = false;
            if (host.nearbyInvite(seats_[i].deviceId, session, weMoveFirst)) {
                host.playSound(Sound::Select);
                startRemote(seats_[i], session, weMoveFirst);
                /* Back to Waiting: startRemote() sets up the game, but nobody
                 * has accepted yet and the board must not take a move until
                 * somebody has. */
                mode_ = Mode::Waiting;
                markFullDirty();
            } else {
                host.beepError();
            }
        }
        return;
    }
}

/* Take the opponent's move, if there is one we should act on.
 *
 * Four tests, and every one of them is load-bearing:
 *
 *   from this peer      -- another console's game must not leak into ours
 *   in this session     -- nor a previous game between the same two consoles
 *   the ply we expect   -- an advertisement repeats, so the same move arrives
 *                          many times; acting once is what makes it a move
 *                          rather than a stutter
 *   legal here          -- and this is the one that matters for safety. A
 *                          move is only applied if it is legal in OUR
 *                          position, so a confused or hostile advertiser
 *                          cannot force the board into a state that is not
 *                          reachable by playing chess.
 */
void ChessGame::pollOpponent(AppContext& host) {
    if (mode_ != Mode::Remote && mode_ != Mode::Waiting) return;
    if (opponent_[0] == 0) return;

    NearbyTurn turn;
    if (!host.nearbyTurnFrom(opponent_, session_, turn)) return;

    if (mode_ == Mode::Waiting) {
        /* They answered. Their first advertisement in our session is the
         * acceptance -- no separate message, because a separate message could
         * be the one that goes missing. Nothing is re-derived here: the seat,
         * the session and the side were all settled when the invitation went
         * out, and this only promotes the mode. */
        mode_ = Mode::Remote;
        markFullDirty();
    }

    const uint8_t expected = static_cast<uint8_t>((theirPly_ + 1) & 0x7F);
    if (turn.ply != expected) return;

    /* They stopped the game. This is tested BEFORE the whose-turn check on
     * purpose: a player gives up when they are stuck, which is usually while
     * they are waiting for us, and a declaration that only arrived on their
     * own turn would be one that mostly never arrived. The reserved encoding
     * belongs to the nearby service, not here -- this only reads the flag. */
    if (turn.ended) {
        theirPly_ = turn.ply;
        declareEnd(host, false);
        host.playSound(Sound::GameOver);
        return;
    }

    if (ourTurn()) return;              // not their move to make

    uint8_t legal[MAX_MOVES];
    const uint8_t n = legalMoves(pos_, turn.from, legal);
    bool ok = false;
    for (uint8_t i = 0; i < n; ++i) {
        if (legal[i] == turn.to) { ok = true; break; }
    }
    if (!ok) return;

    for (int8_t f = 0; f < 8; ++f) markSquare(idx(f, rankOf(turn.from)));
    markSquare(turn.from);
    markSquare(turn.to);
    recordCapture(applyMove(pos_, turn.from, turn.to));
    theirPly_ = turn.ply;
    selected_ = NO_SQ;
    targetCount_ = 0;
    refreshStatus();
    saveGame(host);
    host.playSound(gameOver()                 ? Sound::GameOver
                   : status_ == Status::Check ? Sound::Reveal
                                              : Sound::Tap);
    markDirty();
}

void ChessGame::update(AppContext& host, const TouchPoint& touch) {
    if (mode_ == Mode::Lobby) {
        updateLobby(host, touch);
        return;
    }

    pollOpponent(host);

    /* Republish every frame. Unchanged values do not touch the radio, and a
     * move must stay on the air until it is replaced: the opponent may be
     * anywhere in its scan cycle, or may only just have come back into range.
     *
     * From ply 0, which is not a move but a presence: it is how an accepted
     * invitation is answered. The inviter sits in Waiting until it hears
     * ANYTHING from us in its session, and the acceptor plays Black -- so if
     * this only published once we had moved, neither side could ever start.
     * The receiver's expected-ply test discards ply 0 as a move, so saying it
     * costs nothing and means the handshake needs no second message, which is
     * the message that would have gone missing. */
    if (mode_ == Mode::Remote && status_ != Status::Ended) {
        host.nearbyPublish(session_, ourPly_, ourFrom_, ourTo_, theirPly_);
    }

    /* The confirm window closes on its own, so a half-pressed End game does
     * not lie in wait to be completed by an unrelated tap minutes later. */
    if (confirmUntilMs_ != 0 && millis() > confirmUntilMs_) {
        confirmUntilMs_ = 0;
        panelStale_ = true;
        markDirty();
    }

    if (!touch.justPressed) return;

    const bool over = gameOver();

    /* The one button. New game once the game is over, End game while it is
     * running -- and End game asks twice, because it is the only control here
     * that destroys something. Tested before the board, so a button drawn
     * over the panel is never also a tap on a square. */
    if (actionRect(host).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (over) {
            /* Always back to the lobby, whichever way the game was being
             * played. Two reasons, and the second one is the important one.
             *
             * A new remote game needs a new session and a fresh invitation, so
             * pretending the old session can be reused would be wrong. And now
             * that an unfinished game is restored on the way in, the lobby is
             * no longer somewhere you arrive by leaving and coming back -- so
             * if a finished LOCAL game started another local game directly,
             * there would be no route from pass-and-play to playing a peer at
             * all, short of ending a game you did not want to end. Persistence
             * quietly took that route away; this is where it comes back. */
            host.playSound(Sound::Select);
            if (mode_ == Mode::Remote) host.nearbyStop();
            mode_ = Mode::Lobby;
            opponent_[0] = 0;
            opponentName_[0] = 0;
            session_ = 0;
            seatCount_ = 0;
            seatsAtMs_ = 0;
            newGame();
            saveGame(host);
        } else if (confirmUntilMs_ != 0) {
            declareEnd(host, true);
            host.playSound(Sound::GameOver);
        } else {
            confirmUntilMs_ = millis() + CONFIRM_MS;
            host.playSound(Sound::Tap);
            panelStale_ = true;
            markDirty();
        }
        return;
    }

    if (over) return;
    /* In a remote game the board is read-only while it is their turn. The
     * legality filter would catch an out-of-turn move anyway -- it generates
     * for the side to move -- but stopping it here means the pieces simply do
     * not respond, which reads as "not your turn" rather than as a bug. */
    if (mode_ == Mode::Remote && !ourTurn()) return;

    const uint8_t hit = squareAt(host, touch.x, touch.y);
    if (hit == NO_SQ) return;

    // A tap on one of the marked squares plays the move.
    for (uint8_t i = 0; i < targetCount_; ++i) {
        if (targets_[i] != hit) continue;
        const uint8_t from = selected_;
        markSquare(from);
        markSquare(hit);
        /* Castling and en passant move or remove a piece on a square the
         * player never touched, so those have to be repainted too. Marking
         * the whole home rank and the captured pawn's square is cheaper than
         * working out which case applied. */
        for (int8_t f = 0; f < 8; ++f) markSquare(idx(f, rankOf(from)));
        if (pos_.epSquare != NO_SQ) {
            markSquare(idx(fileOf(pos_.epSquare), rankOf(from)));
        }
        for (uint8_t t = 0; t < targetCount_; ++t) markSquare(targets_[t]);

        recordCapture(applyMove(pos_, from, hit));
        if (mode_ == Mode::Remote) {
            ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
            ourFrom_ = from;
            ourTo_ = hit;
            host.nearbyPublish(session_, ourPly_, ourFrom_, ourTo_, theirPly_);
        }
        selected_ = NO_SQ;
        targetCount_ = 0;
        refreshStatus();
        /* Written after every move, not just on the way out. That is what
         * survives a battery going flat mid-game; end() is what survives
         * everything else. A move is not a hot path -- one NVS write per move
         * is nothing against the thirty-odd a whole game costs. */
        saveGame(host);
        /* A draw is an ending and sounds like one, but it is not a win.
         * On a board with no speaker this changes nothing, which is why the
         * status line has to carry the same news in words. */
        host.playSound(status_ == Status::Checkmate ? Sound::Victory
                       : gameOver()                 ? Sound::GameOver
                       : status_ == Status::Check   ? Sound::Reveal
                                                    : Sound::Tap);
        markDirty();
        return;
    }

    // Otherwise it is a selection: only ever of a piece belonging to the mover.
    for (uint8_t t = 0; t < targetCount_; ++t) markSquare(targets_[t]);
    markSquare(selected_);
    selected_ = NO_SQ;
    targetCount_ = 0;

    const int8_t piece = pos_.sq[hit];
    if (piece != EMPTY && isWhite(piece) == pos_.whiteToMove) {
        selected_ = hit;
        targetCount_ = legalMoves(pos_, hit, targets_);
        markSquare(hit);
        for (uint8_t t = 0; t < targetCount_; ++t) markSquare(targets_[t]);
        host.playSound(Sound::Tap);
    }
    markDirty();
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
