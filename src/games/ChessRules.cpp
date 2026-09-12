#include "ChessGame.h"

#include <string.h>

#include "ChessInternal.h"

/* The rules of chess, and nothing about a screen. Pure: it could be compiled
 * on the host like BackgammonRules if it were ever needed to. See the class
 * comment in ChessGame.h for why "legal" is meant strictly. */

namespace {

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

}   // namespace

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

