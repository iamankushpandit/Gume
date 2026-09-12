#include "ChessRules.h"

#include <string.h>

/* The computer player. Pure C++ -- see ChessRules.h for why that matters and
 * how it is enforced.
 *
 * Two levels, and the difference between them is meant to be felt rather than
 * measured. Easy looks one move ahead and checks whether the thing it just
 * took can be taken straight back; Medium searches three plies with alpha-beta
 * and will see a fork coming. Neither is strong, and neither is trying to be:
 * this console is for players who are learning what the pieces do. An engine
 * that wins every game against a seven-year-old is a worse product than one
 * that loses half of them.
 *
 * THE FRAME BUDGET IS THE CONSTRAINT, not the strength. 20ms holds touch,
 * logic and drawing, and a whole three-ply search does not fit in it, so
 * Medium is split at the root and runs across frames -- see stepSearch() in
 * the header. Nothing here blocks, allocates, or knows what a screen is.
 */
namespace Ch {
namespace {

/* Centipawns. The classical values, because they are the ones a child will
 * later read in any book, and this is a teaching device before it is an
 * engine. The king is worth more than every other piece together so that
 * losing it dominates any material count; it is never actually captured, but
 * the search reaches positions where it is missing from a line it is pruning. */
constexpr int16_t VALUE[7] = {0, 100, 320, 330, 500, 900, 20000};

/* Where a piece would rather be, in centipawns, from White's point of view and
 * mirrored by rank for Black.
 *
 * Only three tables, and that is a flash decision as much as a chess one:
 * every one of these costs 64 bytes and the budget is past 80%. Pawns, knights
 * and the king are the three where position matters most to a beginner's game
 * -- pawns want to advance, knights are dreadful in the corners, and a king
 * wants to be behind its pawns in the opening. Bishops, rooks and queens are
 * left on material alone, which makes the computer a little aimless with them
 * and is an acceptable price. */
constexpr int8_t PST_PAWN[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0,
};
constexpr int8_t PST_KNIGHT[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50,
};
constexpr int8_t PST_KING[64] = {
    20, 30, 10,  0,  0, 10, 30, 20,
    20, 20,  0,  0,  0,  0, 20, 20,
   -10,-20,-20,-20,-20,-20,-20,-10,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
};

/** A square seen from the other side of the board. */
inline uint8_t mirror(uint8_t s) {
    return idx(fileOf(s), static_cast<int8_t>(7 - rankOf(s)));
}

int16_t placement(int8_t piece, uint8_t square) {
    const uint8_t s = isWhite(piece) ? square : mirror(square);
    switch (kind(piece)) {
        case PAWN:   return PST_PAWN[s];
        case KNIGHT: return PST_KNIGHT[s];
        case KING:   return PST_KING[s];
        default:     return 0;
    }
}

/* Mate and the edge of the search. Kept well inside int16_t so that adding a
 * ply's worth of adjustment to one cannot wrap. */
constexpr int16_t INF = 30000;
constexpr int16_t MATE = 29000;

uint32_t nodes = 0;

/* Negamax with alpha-beta, from the point of view of the side to move.
 *
 * `ply` counts down. At zero the position is evaluated statically -- there is
 * no quiescence search, which is the honest weakness here: the computer will
 * happily stop counting in the middle of an exchange and mis-value it. Adding
 * one would cost more than the ply it is worth on this device, and Medium is
 * not trying to be strong.
 *
 * The node ceiling is checked rather than the clock, because this runs inside
 * one root move and the caller's frame budget is checked between them. */
int16_t search(const Position& p, uint8_t ply, int16_t alpha, int16_t beta) {
    if (++nodes > NODE_LIMIT) {
        return evaluate(p);
    }
    if (ply == 0) {
        return evaluate(p);
    }

    Move moves[MAX_ROOT_MOVES];
    const uint8_t n = allMoves(p, moves, MAX_ROOT_MOVES);
    if (n == 0) {
        /* No legal move: mate if the king is attacked, stalemate if not. The
         * mate score is offset by the remaining depth so that a mate found
         * sooner is preferred to the same mate found later -- without it the
         * computer will shuffle in front of a forced mate rather than play it. */
        const uint8_t k = kingSquare(p, p.whiteToMove);
        if (k != NO_SQ && attacked(p, k, !p.whiteToMove)) {
            return static_cast<int16_t>(-(MATE + ply));
        }
        return 0;
    }

    int16_t best = -INF;
    for (uint8_t i = 0; i < n; ++i) {
        Position child = p;
        applyMove(child, moves[i].from, moves[i].to);
        const int16_t score =
            static_cast<int16_t>(-search(child, static_cast<uint8_t>(ply - 1),
                                         static_cast<int16_t>(-beta),
                                         static_cast<int16_t>(-alpha)));
        if (score > best) {
            best = score;
        }
        if (best > alpha) {
            alpha = best;
        }
        if (alpha >= beta) {
            break;      // the opponent would never allow this line
        }
    }
    return best;
}

}   // namespace

uint32_t Rng::next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

uint16_t Rng::below(uint16_t n) {
    return n == 0 ? 0 : static_cast<uint16_t>(next() % n);
}

int16_t evaluate(const Position& p) {
    int16_t white = 0;
    int16_t black = 0;
    for (uint8_t s = 0; s < 64; ++s) {
        const int8_t piece = p.sq[s];
        if (piece == EMPTY) {
            continue;
        }
        const int16_t worth =
            static_cast<int16_t>(VALUE[kind(piece)] + placement(piece, s));
        if (isWhite(piece)) {
            white = static_cast<int16_t>(white + worth);
        } else {
            black = static_cast<int16_t>(black + worth);
        }
    }
    const int16_t diff = static_cast<int16_t>(white - black);
    return p.whiteToMove ? diff : static_cast<int16_t>(-diff);
}

uint8_t allMoves(const Position& p, Move* out, uint8_t cap) {
    uint8_t n = 0;
    for (uint8_t from = 0; from < 64 && n < cap; ++from) {
        const int8_t piece = p.sq[from];
        if (piece == EMPTY || isWhite(piece) != p.whiteToMove) {
            continue;
        }
        uint8_t to[MAX_MOVES];
        const uint8_t count = legalMoves(p, from, to);
        for (uint8_t i = 0; i < count && n < cap; ++i) {
            out[n].from = from;
            out[n].to = to[i];
            ++n;
        }
    }
    return n;
}

Move chooseEasy(const Position& p, Rng& rng) {
    Move moves[MAX_ROOT_MOVES];
    const uint8_t n = allMoves(p, moves, MAX_ROOT_MOVES);
    if (n == 0) {
        return Move{};
    }

    int16_t best = -INF;
    /* Every move within a pawn of the best one, so the choice among them can
     * be random. A computer that always plays the first move it generated is
     * recognisably a machine within three games -- it opens identically every
     * time, and a child stops being able to lose to it by accident. */
    constexpr int16_t SLACK = 100;
    uint8_t pool[MAX_ROOT_MOVES];
    uint8_t poolCount = 0;
    int16_t scored[MAX_ROOT_MOVES];

    for (uint8_t i = 0; i < n; ++i) {
        Position child = p;
        const int8_t taken = applyMove(child, moves[i].from, moves[i].to);
        /* What the move wins, less what the opponent wins straight back. One
         * ply of reply and no more: enough to stop it dropping a queen onto a
         * defended square, not enough to see a fork. */
        int16_t score = static_cast<int16_t>(VALUE[kind(taken)]);
        score = static_cast<int16_t>(score + placement(p.sq[moves[i].from], moves[i].to)
                                     - placement(p.sq[moves[i].from], moves[i].from));

        Move replies[MAX_ROOT_MOVES];
        const uint8_t rn = allMoves(child, replies, MAX_ROOT_MOVES);
        int16_t worst = 0;
        for (uint8_t j = 0; j < rn; ++j) {
            const int8_t back = child.sq[replies[j].to];
            if (back != EMPTY && VALUE[kind(back)] > worst) {
                worst = VALUE[kind(back)];
            }
        }
        score = static_cast<int16_t>(score - worst);

        scored[i] = score;
        if (score > best) {
            best = score;
        }
    }

    for (uint8_t i = 0; i < n; ++i) {
        if (scored[i] >= static_cast<int16_t>(best - SLACK)) {
            pool[poolCount++] = i;
        }
    }
    return moves[pool[rng.below(poolCount)]];
}

void beginSearch(Search& s, const Position& p, uint32_t seed) {
    s.root = p;
    s.rng.s = seed != 0 ? seed : 1u;    // xorshift cannot be seeded with zero
    s.count = allMoves(p, s.move, MAX_ROOT_MOVES);
    s.done = 0;
    for (uint8_t i = 0; i < s.count; ++i) {
        s.score[i] = -INF;
    }
}

void stepOne(Search& s) {
    if (s.done >= s.count) {
        return;
    }
    const uint8_t i = s.done;
    Position child = s.root;
    applyMove(child, s.move[i].from, s.move[i].to);
    nodes = 0;
    s.score[i] = static_cast<int16_t>(
        -search(child, static_cast<uint8_t>(MEDIUM_DEPTH - 1), -INF, INF));
    ++s.done;
}

Move bestMove(const Search& s) {
    if (s.count == 0) {
        return Move{};
    }
    /* Among equals, the earliest -- but the root list itself was built in
     * square order, so ties would always favour the a-file. The seeded jitter
     * breaks that without making the search non-deterministic: the same seed
     * gives the same game. */
    int16_t best = -INF;
    uint8_t pick = 0;
    Rng rng = s.rng;
    uint8_t seen = 0;
    for (uint8_t i = 0; i < s.done; ++i) {
        if (s.score[i] > best) {
            best = s.score[i];
            pick = i;
            seen = 1;
        } else if (s.score[i] == best) {
            ++seen;
            if (rng.below(seen) == 0) {
                pick = i;
            }
        }
    }
    return s.move[pick];
}

}   // namespace Ch
