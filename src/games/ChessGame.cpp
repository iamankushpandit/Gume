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
constexpr int16_t STATUS_H = 22;
constexpr int16_t MARGIN = 3;

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

void ChessGame::applyMove(Position& p, uint8_t from, uint8_t to) {
    const int8_t piece = p.sq[from];
    const bool white = isWhite(piece);
    const int8_t k = kind(piece);

    /* En passant capture removes a pawn that is NOT on the destination square,
     * which is the one move in chess where that is true. */
    if (k == PAWN && to == p.epSquare && p.sq[to] == EMPTY) {
        p.sq[idx(fileOf(to), rankOf(from))] = EMPTY;
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

    p.whiteToMove = !p.whiteToMove;
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

bool ChessGame::hasAnyLegalMove(const Position& p) {
    uint8_t buf[MAX_MOVES];
    for (uint8_t s = 0; s < 64; ++s) {
        if (p.sq[s] == EMPTY || isWhite(p.sq[s]) != p.whiteToMove) continue;
        if (legalMoves(p, s, buf) > 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------- layout

/* Square, and sized from the SHORTER axis so the whole board always fits.
 * Portrait is the better orientation here: 240x320 leaves a 240px board and
 * 30px squares, against 26px in landscape. */
Rect ChessGame::boardRect(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const int16_t w = static_cast<int16_t>(tft.width());
    const int16_t h = static_cast<int16_t>(tft.height());
    const int16_t top = static_cast<int16_t>(TOP_BAR_H + MARGIN);
    const int16_t availH = static_cast<int16_t>(h - top - STATUS_H - MARGIN);
    const int16_t availW = static_cast<int16_t>(w - MARGIN * 2);
    /* A multiple of 8, so every square is the same size and the grid has no
     * one-pixel-wider column where the division left a remainder. */
    int16_t side = (availW < availH ? availW : availH);
    side = static_cast<int16_t>((side / 8) * 8);
    return Rect{static_cast<int16_t>((w - side) / 2), top, side, side};
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

void ChessGame::begin(AppContext& host) {
    (void)host;
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

    selected_ = NO_SQ;
    targetCount_ = 0;
    dirtyCount_ = 0;
    status_ = Status::Playing;
    statusStale_ = true;
    markFullDirty();
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
    if (!hasAnyLegalMove(pos_)) {
        status_ = inCheck ? Status::Checkmate : Status::Stalemate;
    } else {
        status_ = inCheck ? Status::Check : Status::Playing;
    }
    statusStale_ = true;
}

void ChessGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;
    if (status_ == Status::Checkmate || status_ == Status::Stalemate) return;

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

        applyMove(pos_, from, hit);
        selected_ = NO_SQ;
        targetCount_ = 0;
        refreshStatus();
        host.playSound(status_ == Status::Checkmate ? Sound::Victory
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

void ChessGame::drawStatus(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const int16_t h = static_cast<int16_t>(tft.height());
    const int16_t y = static_cast<int16_t>(h - STATUS_H);
    tft.fillRect(0, y, static_cast<int16_t>(tft.width()), STATUS_H, Ui::bg());

    char line[40];
    const char* side = pos_.whiteToMove ? "White" : "Black";
    switch (status_) {
        case Status::Checkmate:
            /* The side to move is the one that is mated, so the winner is the
             * other one -- stating the loser here would be a small cruelty and
             * a large confusion. */
            snprintf(line, sizeof(line), "Checkmate - %s wins",
                     pos_.whiteToMove ? "Black" : "White");
            break;
        case Status::Stalemate:
            snprintf(line, sizeof(line), "Stalemate - a draw");
            break;
        case Status::Check:
            snprintf(line, sizeof(line), "%s to move - check!", side);
            break;
        default:
            snprintf(line, sizeof(line), "%s to move", side);
            break;
    }
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(status_ == Status::Check || status_ == Status::Checkmate
                         ? Ui::warning() : Ui::text(),
                     Ui::bg());
    tft.drawString(line, static_cast<int16_t>(tft.width() / 2),
                   static_cast<int16_t>(y + STATUS_H / 2), 2);
    tft.setTextDatum(TL_DATUM);
}

void ChessGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    for (uint8_t s = 0; s < 64; ++s) drawSquare(host, s);
    const Rect b = boardRect(host);
    tft.drawRect(b.x, b.y, b.w, b.h, Ui::outline());
    drawStatus(host);
    dirtyCount_ = 0;
    statusStale_ = false;
}

void ChessGame::renderDynamic(AppContext& host) {
    /* Only the squares that changed. A whole board is 64 fills and up to 32
     * pieces; a move touches a handful, and at 26px a square the difference is
     * the whole frame budget. */
    for (uint8_t i = 0; i < dirtyCount_; ++i) drawSquare(host, dirtySq_[i]);
    dirtyCount_ = 0;
    if (statusStale_) {
        drawStatus(host);
        statusStale_ = false;
    }
}
